// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcfont.hpp"

namespace riftwii {
namespace {

constexpr std::uint32_t kBlr = 0x4E800020;
constexpr unsigned kDecisionWords = 18;

class Words {
public:
    explicit Words(const std::uint8_t* bytes) : bytes_(bytes) {}

    std::uint32_t operator[](std::size_t index) const {
        const std::uint8_t* bytes = bytes_ + index * 4;
        return (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16) |
               (std::uint32_t(bytes[2]) << 8) | bytes[3];
    }

    Words after(std::size_t count) const { return Words(bytes_ + count * 4); }

private:
    const std::uint8_t* bytes_;
};

unsigned opcode(std::uint32_t word) { return word >> 26; }
unsigned reg_d(std::uint32_t word) { return (word >> 21) & 31; }
unsigned reg_a(std::uint32_t word) { return (word >> 16) & 31; }
unsigned immediate(std::uint32_t word) { return word & 0xFFFF; }

bool is_lis(std::uint32_t word, unsigned value) {
    return opcode(word) == 15 && reg_a(word) == 0 && immediate(word) == value;
}

bool is_li(std::uint32_t word, unsigned destination, unsigned value) {
    return opcode(word) == 14 && reg_d(word) == destination &&
           reg_a(word) == 0 && immediate(word) == value;
}

bool compares(std::uint32_t word, unsigned operation, unsigned source, unsigned value) {
    // 32-bit cmpi/cmpli, including the reserved bit beside L.
    return opcode(word) == operation && ((word >> 21) & 3) == 0 &&
           reg_a(word) == source && immediate(word) == value;
}

unsigned condition_field(std::uint32_t compare) { return (compare >> 23) & 7; }

bool branches(std::uint32_t word, unsigned bo, unsigned bi, int displacement) {
    return opcode(word) == 16 && reg_d(word) == bo && reg_a(word) == bi &&
           (word & 3) == 0 && static_cast<std::int16_t>(word & 0xFFFC) == displacement;
}

bool jumps(std::uint32_t word, unsigned displacement) {
    return opcode(word) == 18 && (word & 0x03FFFFFF) == displacement;
}

bool same_memory(std::uint32_t word, unsigned operation, std::uint32_t reference) {
    return opcode(word) == operation && (word & 0x001FFFFF) == (reference & 0x001FFFFF);
}

// Instructions seen between an inlined cache load, its comparison and
// its guard. They must preserve CR and, until the comparison, the cache
// value. Refuse unfamiliar instructions instead of guessing their effects.
bool preserves_guard(std::uint32_t word, unsigned cached, unsigned cache_base, bool compared) {
    const unsigned operation = opcode(word);
    if (operation == 36 || operation == 44) {
        return true;  // stw/sth do not update their base or CR
    }
    if (operation == 14 || operation == 32 || operation == 40) {
        return reg_d(word) != cache_base && (compared || reg_d(word) != cached);
    }
    const unsigned extended = (word >> 1) & 0x3FF;
    if (operation == 31 && (word & 1) == 0 && (extended == 235 || extended == 266)) {
        return reg_d(word) != cache_base && (compared || reg_d(word) != cached);  // mullw/add, no Rc or OE
    }
    return false;
}

bool has_cache_guard(const Words& words, std::size_t start, std::uint32_t cache) {
    if (start < 3) {
        return false;
    }
    const std::uint32_t guard = words[start - 1];
    const std::uint32_t compare = words[start - 2];
    const std::uint32_t load = words[start - 3];
    if (same_memory(load, 40, cache) && compares(compare, 10, reg_d(load), 1)) {
        // Standalone getter returns the already valid cached encoding.
        const unsigned gt = condition_field(compare) * 4 + 1;
        const std::uint32_t blelr = (19u << 26) | (4u << 21) | (gt << 16) | (16u << 1);
        if (reg_d(load) == 3 && reg_a(cache) != 3 && reg_d(words[start + 17]) == 3 &&
            guard == blelr && words[start + 18] == kBlr) {
            return true;
        }
    }

    // Inlined getters branch over the decision when the cache is valid.
    // Some callers schedule unrelated font bookkeeping inside the guard.
    if (!jumps(guard, 19 * 4)) {
        return false;
    }
    const std::size_t earliest = start > 20 ? start - 20 : 0;
    for (std::size_t index = earliest; index + 3 < start; ++index) {
        const std::uint32_t cached_load = words[index];
        if (!same_memory(cached_load, 40, cache)) {
            continue;
        }
        const unsigned cached = reg_d(cached_load);
        if (cached == reg_a(cache)) {
            continue;
        }
        bool compared = false;
        unsigned gt = 0;
        std::size_t next = index + 1;
        for (; next + 2 < start; ++next) {
            const std::uint32_t word = words[next];
            if (!compared && compares(word, 10, cached, 1)) {
                compared = true;
                gt = condition_field(word) * 4 + 1;
            } else if (!preserves_guard(word, cached, reg_a(cache), compared)) {
                break;
            }
        }
        if (next + 2 == start && compared && branches(words[next], 12, gt, 8)) {
            return true;
        }
    }
    return false;
}

// The decision after the cache guard, 18 words as the SDK builds it
// (registers vary; the branch offsets are in bytes):
//    0 lis   tv,0x8000          9 beq    +12 (bit clear: ANSI)
//    1 lwz   fmt,0xCC(tv)      10 li     enc,1 (Shift-JIS)
//    2 cmplwi fmt,0            11 b      +8
//    3 beq   +12 (NTSC)        12 li     enc,0
//    4 blt   +44 (to word 15)  13 stw    enc,cache
//    5 b     +40 (to word 15)  14 b      +12
//    6 lis   vi,0xCC00/0xCD00  15 li     enc,0
//    7 lhz   dtv,0x206E(vi)    16 stw    enc,cache
//    8 rlwinm./andi. dtv,2     17 lwz    ...,cache
// Word 7 is the one replaced. The guard before it either returns the
// cached value (blelr) or, inlined, jumps 19 words ahead past the decision.
bool matches_decision(const Words& words, std::size_t start) {
    const Words decision = words.after(start);
    const unsigned tv_base = reg_d(decision[0]);
    if (!is_lis(decision[0], 0x8000) || tv_base == 0 || opcode(decision[1]) != 32 ||
        reg_a(decision[1]) != tv_base || immediate(decision[1]) != 0xCC ||
        !compares(decision[2], 11, reg_d(decision[1]), 0)) {
        return false;
    }
    const unsigned tv_condition = condition_field(decision[2]) * 4;
    if (!branches(decision[3], 12, tv_condition + 2, 12) ||
        !branches(decision[4], 12, tv_condition, 44) || !jumps(decision[5], 40)) {
        return false;
    }
    const unsigned vi_base = reg_d(decision[6]);
    if ((!is_lis(decision[6], 0xCC00) && !is_lis(decision[6], 0xCD00)) || vi_base == 0 ||
        opcode(decision[7]) != 40 || reg_a(decision[7]) != vi_base ||
        immediate(decision[7]) != 0x206E) {
        return false;
    }
    const unsigned strap = reg_d(decision[7]);
    const std::uint32_t test = decision[8];
    const unsigned bit_two_mask = (30u << 6) | (30u << 1) | 1u;
    const bool rotate_test = opcode(test) == 21 && immediate(test) == bit_two_mask;
    const bool and_test = opcode(test) == 28 && immediate(test) == 2;
    if (reg_d(test) != strap || reg_a(test) != strap || (!rotate_test && !and_test) ||
        !branches(decision[9], 12, 2, 12)) {
        return false;
    }
    const unsigned encoding = reg_d(decision[10]);
    // The SDK reuses the DTV temporary for the bit test and the encoding.
    // Require that relationship so replacing the read cannot leave a
    // different DTV value live in the surrounding inlined caller.
    if (encoding != strap || !is_li(decision[10], encoding, 1) || !jumps(decision[11], 8) ||
        !is_li(decision[12], encoding, 0) || opcode(decision[13]) != 44 ||
        reg_d(decision[13]) != encoding || !jumps(decision[14], 12) ||
        !is_li(decision[15], encoding, 0) ||
        !same_memory(decision[16], 44, decision[13]) || reg_d(decision[16]) != encoding ||
        !same_memory(decision[17], 40, decision[13])) {
        return false;
    }
    // The cached global uses a stable base, not a temporary overwritten
    // by this decision. The inspected SDKs use r13 (small data).
    const unsigned cache_base = reg_a(decision[13]);
    if (cache_base == 0 || cache_base == tv_base || cache_base == reg_d(decision[1]) ||
        cache_base == vi_base || cache_base == strap || cache_base == reg_a(test) ||
        cache_base == encoding) {
        return false;
    }
    return has_cache_guard(words, start, decision[13]);
}

// Follow only simple constant construction in this basic block. An unknown
// instruction discards all knowledge, so it cannot hide a changed VI base.
bool is_dtv_load(const Words& words, std::size_t load) {
    if (opcode(words[load]) != 40 || reg_a(words[load]) == 0) {
        return false;
    }
    std::array<std::uint32_t, 32> values{};
    std::array<bool, 32> known{};
    const std::size_t first = load > 8 ? load - 8 : 0;
    for (std::size_t index = first; index < load; ++index) {
        const std::uint32_t word = words[index];
        const unsigned operation = opcode(word);
        const unsigned destination = reg_d(word);
        const unsigned base = reg_a(word);
        if (operation == 14 || operation == 15) {
            const bool source_known = base == 0 || known[base];
            const std::uint32_t source = base == 0 ? 0 : values[base];
            std::uint32_t offset = static_cast<std::uint32_t>(static_cast<std::int16_t>(word));
            if (operation == 15) {
                offset <<= 16;
            }
            values[destination] = source + offset;
            known[destination] = source_known;
        } else if (operation == 24 || operation == 25) {
            const std::uint32_t bits = immediate(word) << (operation == 25 ? 16 : 0);
            values[base] = values[destination] | bits;
            known[base] = known[destination];
        } else if (operation == 32 || operation == 34 || operation == 40 || operation == 42) {
            known[destination] = false;
        } else if (operation == 21 || operation == 28) {
            known[base] = false;
        } else if (operation != 36 && operation != 38 && operation != 44 &&
                   operation != 10 && operation != 11) {
            known.fill(false);
        }
    }
    const unsigned base = reg_a(words[load]);
    const std::uint32_t offset = static_cast<std::uint32_t>(static_cast<std::int16_t>(words[load]));
    const std::uint32_t address = values[base] + offset;
    return known[base] && (address == 0xCC00206E || address == 0xCD00206E);
}

// Scheduling is allowed only when the instruction neither reads nor writes
// the raw DTV value and leaves the condition register alone. In particular,
// a copy, a store of that value, a call or a branch ends the search.
bool preserves_strap(std::uint32_t word, unsigned strap) {
    const unsigned operation = opcode(word);
    const unsigned source = reg_d(word);
    const unsigned base = reg_a(word);
    if (operation == 14 || operation == 15 || operation == 32 || operation == 34 ||
        operation == 40 || operation == 42) {
        return source != strap && (base == 0 || base != strap);
    }
    if (operation == 24 || operation == 25 || operation == 36 || operation == 38 || operation == 44) {
        return source != strap && base != strap;
    }
    return false;
}

bool isolates_strap(std::uint32_t word, unsigned strap) {
    // Destructive extraction is essential: otherwise the unmasked DTV
    // register could still supply the component-cable bit to another user.
    if (reg_d(word) != strap || reg_a(word) != strap) {
        return false;
    }
    if (opcode(word) == 28) {
        return immediate(word) == 2;
    }
    if (opcode(word) != 21) {
        return false;
    }
    const unsigned shift = (word >> 11) & 31;
    const unsigned first = (word >> 6) & 31;
    const unsigned last = (word >> 1) & 31;
    // PowerPC numbers mask bits from the MSB. This also accepts extrwi
    // (rlwinm with a rotation), provided its sole source bit is bit 0x2.
    const unsigned source_bit = (31 - first + 32 - shift) & 31;
    return first == last && source_bit == 1;
}

bool matches_strap_test(const Words& words, std::size_t load, std::size_t count) {
    const unsigned strap = reg_d(words[load]);
    bool isolated = false;
    bool compared = false;
    unsigned equal_bit = 2;
    for (std::size_t index = load + 1; index < count && index - load <= 6; ++index) {
        const std::uint32_t word = words[index];
        if (!isolated && index - load <= 4 && isolates_strap(word, strap)) {
            isolated = true;
            compared = opcode(word) == 28 || (word & 1) != 0;
        } else if (isolated && (compares(word, 11, strap, 0) || compares(word, 10, strap, 0))) {
            compared = true;
            equal_bit = condition_field(word) * 4 + 2;
        } else if (isolated && compared && opcode(word) == 16 &&
                   (reg_d(word) == 4 || reg_d(word) == 12) && reg_a(word) == equal_bit && (word & 3) == 0) {
            return true;
        } else if (!preserves_strap(word, strap)) {
            return false;
        }
    }
    return false;
}

}  // namespace

std::vector<GcFontPatch> find_gc_font_patches(const std::vector<CodeRange>& text, const std::string& game_id,
                                           std::vector<GcFontRead>* unclassified) {
    std::vector<GcFontPatch> patches;
    if (game_id.size() < 4 || game_id[3] != 'J') {
        return patches;
    }
    for (const CodeRange& range : text) {
        if (range.bytes == nullptr || (range.address & 3) != 0) {
            continue;
        }
        // Stay within one section; do not borrow words from gaps. Read
        // big-endian bytes directly without a second copy of the game's code.
        const Words words(range.bytes);
        const std::size_t word_count = range.size / 4;
        for (std::size_t load = 0; load < word_count; ++load) {
            if (!is_dtv_load(words, load)) {
                continue;
            }
            const std::size_t offset = load * 4;
            if (offset > UINT32_MAX - range.address) {
                continue;
            }
            const std::uint32_t address = range.address + static_cast<std::uint32_t>(offset);
            const bool exact = load >= 7 && word_count - load > kDecisionWords - 7 &&
                               matches_decision(words, load - 7);
            if (exact || matches_strap_test(words, load, word_count)) {
                const unsigned destination = reg_d(words[load]);
                patches.push_back({address, (14u << 26) | (destination << 21) | 2u});
            } else if (unclassified != nullptr) {
                GcFontRead read;
                read.address = address;
                for (std::size_t index = load; index < word_count && read.word_count < read.words.size(); ++index) {
                    read.words[read.word_count++] = words[index];
                }
                unclassified->push_back(read);
            }
        }
    }
    return patches;
}

}  // namespace riftwii
