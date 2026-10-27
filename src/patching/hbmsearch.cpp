// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/hbmsearch.hpp"

#include <algorithm>
#include <cstring>

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::int32_t simm(std::uint32_t insn) { return static_cast<std::int16_t>(insn & 0xFFFFu); }

std::uint32_t opcode(std::uint32_t insn) { return insn >> 26; }
std::uint32_t reg_d(std::uint32_t insn) { return (insn >> 21) & 31u; }
std::uint32_t reg_a(std::uint32_t insn) { return (insn >> 16) & 31u; }

constexpr std::uint32_t kStwuR1Mask = 0xFFFF0000u;
constexpr std::uint32_t kStwuR1 = 0x94210000u;  // stwu r1,d(r1)
constexpr std::uint32_t kBlr = 0x4E800020u;
constexpr std::uint32_t kNop = 0x60000000u;
constexpr std::uint32_t kMtlrR0 = 0x7C0803A6u;

bool is_stwu_r1(std::uint32_t insn) { return (insn & kStwuR1Mask) == kStwuR1; }
bool is_call(std::uint32_t insn) { return opcode(insn) == 18 && (insn & 3u) == 1u; }  // bl

// The word at `address` in any of `ranges`.
bool word_at(const std::vector<CodeRange>& ranges, std::uint32_t address, std::uint32_t& out) {
    for (const CodeRange& r : ranges) {
        if (r.bytes == nullptr || address < r.address || address - r.address + 4 > r.size) continue;
        out = be32(r.bytes + (address - r.address));
        return true;
    }
    return false;
}

// The function holding the word at `address`: its `stwu r1` going back, or
// the first word after the `blr` that ends the function before it (a leaf
// with no frame), past padding. 0 when neither comes within `limit` words.
std::uint32_t function_start(const std::vector<CodeRange>& text, std::uint32_t address, unsigned limit) {
    for (unsigned back = 0; back <= limit && back * 4 <= address; ++back) {
        std::uint32_t insn = 0;
        if (!word_at(text, address - back * 4, insn)) return 0;
        if (is_stwu_r1(insn)) return address - back * 4;
        if (back == 0 || insn != kBlr) continue;
        std::uint32_t first = address - back * 4 + 4;
        std::uint32_t next = 0;
        while (first < address && word_at(text, first, next) && (next == 0 || next == kNop)) first += 4;
        return first;
    }
    return 0;
}

// Calls `visit(address, insn)` for every word from `start` to its function's
// `blr` (at most `limit` words); stops early when `visit` returns false.
template <typename Visit>
void each_word(const std::vector<CodeRange>& text, std::uint32_t start, unsigned limit, Visit visit) {
    for (unsigned i = 0; i < limit; ++i) {
        std::uint32_t insn = 0;
        if (!word_at(text, start + i * 4, insn)) return;
        if (!visit(start + i * 4, insn)) return;
        if (insn == kBlr) return;
    }
}

// The KPAD status's fields the ring walker stores: the device type and the
// controller's error, as every SDK seen lays them out.
constexpr std::int32_t kDevTypeOffset = 0x5C;
constexpr std::int32_t kWpadErrOffset = 0x5D;
constexpr std::int32_t kMinStatusBytes = 0x80;
constexpr std::int32_t kMaxStatusBytes = 0x200;
constexpr unsigned kMinBackSteps = 3;
constexpr unsigned kErrorCopyWithin = 24;  // words from the read's start

// The function right after the one at `start` (up to `limit` words long,
// to its first `blr`), past any padding; 0 when there is none.
std::uint32_t next_function(const std::vector<CodeRange>& text, std::uint32_t start, unsigned limit) {
    std::uint32_t end = 0;
    each_word(text, start, limit, [&](std::uint32_t at, std::uint32_t w) {
        if (w == kBlr) end = at;
        return true;
    });
    if (end == 0) return 0;
    std::uint32_t next = end + 4, insn = 0;
    while (word_at(text, next, insn) && (insn == 0 || insn == kNop) && next < end + 64) next += 4;
    return word_at(text, next, insn) ? next : 0;
}

// Whether the wrapper at `at` keeps r3 (`mr rN,r3`) before its first call:
// it takes an argument, which HBMGetSelectBtnNum doesn't (Dr. Mario Online
// Rx has HBMSetAdjustFlag(BOOL) right after HBMDraw).
bool keeps_argument(const std::vector<CodeRange>& text, std::uint32_t at) {
    bool keeps = false;
    each_word(text, at, 16, [&](std::uint32_t, std::uint32_t w) {
        if (is_call(w)) return false;
        const bool mr_from_r3 = opcode(w) == 31 && ((w >> 1) & 0x3FFu) == 444u && reg_d(w) == 3 && ((w >> 11) & 31u) == 3u;
        keeps = keeps || mr_from_r3;
        return true;
    });
    return keeps;
}

// HBMGetSelectBtnNum, given where it should start: a short wrapper (a
// `stwu`, a call within 16 words, ended within 40, taking no argument;
// 2009 on), or a short leaf that answers -1 while the menu runs (`li
// r3,-1` within 12 words; the 2007 builds: Brawl, Mario Kart Wii, Cruis'n
// USA, and the C++ menu's inner one).
bool looks_like_select(const std::vector<CodeRange>& text, std::uint32_t at) {
    constexpr std::uint32_t kLiR3MinusOne = 0x3860FFFFu;
    std::uint32_t first = 0;
    if (at == 0 || !word_at(text, at, first)) return false;
    bool calls = false, ended = false, minus_one = false;
    unsigned words = 0;
    each_word(text, at, 40, [&](std::uint32_t where, std::uint32_t w) {
        ++words;
        calls = calls || (is_call(w) && where < at + 64);
        minus_one = minus_one || (w == kLiR3MinusOne && where < at + 48);
        if (w == kBlr) ended = true;
        return true;
    });
    if (!ended) return false;
    if (is_stwu_r1(first)) return calls && !keeps_argument(text, at);
    return minus_one && words <= 12;
}

// HBMCalc's answer, where it returns the button the menu ended on (the C++
// HOME Menu, 2008 on: Dr. Mario Online Rx): the leaf it calls last, right
// before its epilogue (`lwz` from the frame, `mtlr r0`, `addi r1`), whose
// r3 it passes back untouched. 0 when the last call isn't such a leaf.
std::uint32_t calc_answer(const std::vector<CodeRange>& text, std::uint32_t calc) {
    std::uint32_t last_call = 0, last_call_insn = 0;
    bool epilogue_only = false;
    each_word(text, calc, 64, [&](std::uint32_t at, std::uint32_t w) {
        if (is_call(w)) {
            last_call = at;
            last_call_insn = w;
            epilogue_only = true;
            return true;
        }
        const bool restores = opcode(w) == 32 && reg_a(w) == 1 && reg_d(w) != 3;  // lwz rN,d(r1)
        const bool frame_end = w == kMtlrR0 || (opcode(w) == 14 && reg_d(w) == 1 && reg_a(w) == 1);
        if (!(restores || frame_end || w == kBlr)) epilogue_only = false;
        return true;
    });
    if (last_call == 0 || !epilogue_only) return 0;
    std::int32_t offset = static_cast<std::int32_t>(last_call_insn & 0x03FFFFFCu);
    if (offset & 0x02000000) offset -= 0x04000000;
    const std::uint32_t leaf = last_call + static_cast<std::uint32_t>(offset);
    std::uint32_t first = 0;
    if (!word_at(text, leaf, first) || is_stwu_r1(first)) return 0;
    return looks_like_select(text, leaf) ? leaf : 0;
}

}  // namespace

KpadRead find_kpad_read(const std::vector<CodeRange>& text) {
    KpadRead found;
    unsigned matches = 0;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        const std::size_t words = r.size / 4;
        std::size_t i = 0;
        // One function at a time, from one `stwu r1` to the next.
        while (i < words) {
            while (i < words && !is_stwu_r1(be32(r.bytes + 4 * i))) ++i;
            if (i >= words) break;
            const std::size_t start = i++;
            while (i < words && !is_stwu_r1(be32(r.bytes + 4 * i))) ++i;
            // The sizes multiplied and stepped back by, and the two stores.
            std::int32_t sizes_multiplied[8] = {};
            unsigned multiplied = 0;
            unsigned back_steps[8] = {};
            bool dev_type = false, wpad_err = false;
            for (std::size_t k = start; k < i; ++k) {
                const std::uint32_t insn = be32(r.bytes + 4 * k);
                const std::int32_t value = simm(insn);
                if (opcode(insn) == 7 && value >= kMinStatusBytes && value <= kMaxStatusBytes && multiplied < 8) {
                    sizes_multiplied[multiplied++] = value;  // mulli
                } else if (opcode(insn) == 38) {           // stb
                    dev_type = dev_type || value == kDevTypeOffset;
                    wpad_err = wpad_err || value == kWpadErrOffset;
                }
            }
            if (!dev_type || !wpad_err || multiplied == 0) continue;
            for (std::size_t k = start; k < i; ++k) {
                const std::uint32_t insn = be32(r.bytes + 4 * k);
                if (opcode(insn) != 14) continue;  // addi rD,rA,-S
                for (unsigned m = 0; m < multiplied; ++m)
                    if (simm(insn) == -sizes_multiplied[m]) ++back_steps[m];
            }
            std::int32_t size = 0;
            for (unsigned m = 0; m < multiplied; ++m)
                if (back_steps[m] >= kMinBackSteps) size = sizes_multiplied[m];
            if (size == 0) continue;
            found.address = r.address + static_cast<std::uint32_t>(4 * start);
            found.status_bytes = static_cast<std::uint32_t>(size);
            ++matches;
        }
    }
    if (matches != 1) return KpadRead{};
    // The newer read keeps its fourth argument, the error's address, for
    // later: `mr rN,r6` among its first words, next to its copies of r3 to
    // r5 (Naruto, Kamen Rider, Ranger Cross). The 2007 read takes three
    // (Brawl, Mario Kart Wii), and r6 is whatever its caller left there.
    each_word(text, found.address, kErrorCopyWithin, [&](std::uint32_t, std::uint32_t w) {
        const bool or_form = opcode(w) == 31 && ((w >> 1) & 0x3FFu) == 444 && (w & 1u) == 0;  // or rA,rS,rB
        if (or_form && reg_d(w) == 6 && ((w >> 11) & 31u) == 6 && reg_a(w) != 6) found.takes_error = true;
        return !found.takes_error;
    });
    return found;
}

std::uint32_t find_wpad_probe(const std::vector<CodeRange>& text) {
    constexpr std::uint32_t kNoDevice = 253;     // WPAD's "not known yet"
    constexpr std::uint32_t kIndexChannel = 0x5460103Au;  // slwi r0,r3,2
    std::uint32_t found = 0;
    unsigned matches = 0;
    std::vector<std::uint32_t> checked;  // a function with several compares is looked at once
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        for (std::size_t k = 0; k * 4 + 4 <= r.size; ++k) {
            const std::uint32_t insn = be32(r.bytes + 4 * k);
            if (opcode(insn) != 10 || (insn & 0xFFFFu) != kNoDevice) continue;  // cmplwi rX,253
            const std::uint32_t start = function_start(text, r.address + static_cast<std::uint32_t>(4 * k), 64);
            if (start == 0 || std::find(checked.begin(), checked.end(), start) != checked.end()) continue;
            checked.push_back(start);
            bool index = false, lwzx = false, store_type = false, minus_one = false, minus_two = false;
            std::int32_t type_offsets[8] = {};
            unsigned type_loads = 0;
            bool repeated_type = false;
            each_word(text, start, 80, [&](std::uint32_t, std::uint32_t w) {
                index = index || w == kIndexChannel;
                lwzx = lwzx || (opcode(w) == 31 && ((w >> 1) & 0x3FFu) == 23);
                if (opcode(w) == 34 && reg_d(w) == 0) {  // lbz r0,D(rA): the device type
                    for (unsigned t = 0; t < type_loads; ++t) repeated_type = repeated_type || type_offsets[t] == simm(w);
                    if (type_loads < 8) type_offsets[type_loads++] = simm(w);
                }
                store_type = store_type || (opcode(w) == 36 && reg_d(w) == 0 && (w & 0xFFFFu) == 0);  // stw r0,0(rA)
                minus_one = minus_one || (opcode(w) == 14 && reg_a(w) == 0 && (w & 0xFFFFu) == 0xFFFFu);
                minus_two = minus_two || (opcode(w) == 14 && reg_a(w) == 0 && (w & 0xFFFFu) == 0xFFFEu);
                return true;
            });
            if (!index || !lwzx || !store_type || !minus_one || !minus_two || !repeated_type) continue;
            found = start;
            ++matches;
        }
    }
    return matches == 1 ? found : 0;
}

std::uint32_t find_small_data_base(const std::vector<CodeRange>& sections, std::uint32_t entry) {
    std::uint32_t high = 0;
    bool have_high = false;
    for (unsigned i = 0; i < 256; ++i) {
        std::uint32_t insn = 0;
        if (!word_at(sections, entry + i * 4, insn)) return 0;
        if (opcode(insn) == 15 && reg_d(insn) == 13 && reg_a(insn) == 0) {  // lis r13,hi
            high = (insn & 0xFFFFu) << 16;
            have_high = true;
        } else if (have_high && opcode(insn) == 24 && reg_d(insn) == 13 && reg_a(insn) == 13) {  // ori r13,r13,lo
            return high | (insn & 0xFFFFu);
        } else if (have_high && opcode(insn) == 14 && reg_d(insn) == 13 && reg_a(insn) == 13) {  // addi r13,r13,lo
            return high + static_cast<std::uint32_t>(simm(insn));
        }
    }
    return 0;
}

HbmFunctions find_hbm_functions(const std::vector<CodeRange>& text, const std::vector<CodeRange>& sections,
                                std::uint32_t small_data_base) {
    static const char kBanner[] = "<< RVL_SDK - HBM";
    const std::size_t banner_length = sizeof(kBanner) - 1;
    // 1. The banner, in any section.
    std::uint32_t banner = 0;
    unsigned banners = 0;
    for (const CodeRange& r : sections) {
        if (r.bytes == nullptr || r.size < banner_length) continue;
        for (std::size_t k = 0; k + banner_length <= r.size; ++k) {
            if (std::memcmp(r.bytes + k, kBanner, banner_length) != 0) continue;
            banner = r.address + static_cast<std::uint32_t>(k);
            ++banners;
        }
    }
    if (banners != 1) return HbmFunctions{};
    // 2. The word pointing at it (__HBMVersion), outside the code.
    std::uint32_t pointer = 0;
    unsigned pointers = 0;
    for (const CodeRange& r : sections) {
        if (r.bytes == nullptr) continue;
        bool is_text = false;
        for (const CodeRange& t : text) is_text = is_text || t.address == r.address;
        if (is_text) continue;
        for (std::size_t k = 0; k * 4 + 4 <= r.size; ++k) {
            if (be32(r.bytes + 4 * k) != banner) continue;
            pointer = r.address + static_cast<std::uint32_t>(4 * k);
            ++pointers;
        }
    }
    if (pointers != 1) return HbmFunctions{};
    // 3. The function that loads that word: `lwz rD,d(r13)` with the
    // small-data base, or `lis rX,ha` and `lwz rD,lo(rX)` a few words on.
    const std::uint32_t high_adjusted = ((pointer + 0x8000u) >> 16) & 0xFFFFu;
    const std::uint32_t low = pointer & 0xFFFFu;
    std::uint32_t init = 0;
    unsigned loaders = 0;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr) continue;
        const std::size_t words = r.size / 4;
        for (std::size_t k = 0; k < words; ++k) {
            const std::uint32_t insn = be32(r.bytes + 4 * k);
            if (opcode(insn) != 32) continue;  // lwz
            bool loads_pointer = false;
            if (reg_a(insn) == 13 && small_data_base != 0) {
                loads_pointer = small_data_base + static_cast<std::uint32_t>(simm(insn)) == pointer;
            } else if (reg_a(insn) != 0 && (insn & 0xFFFFu) == low) {
                for (std::size_t back = 1; back <= 6 && back <= k && !loads_pointer; ++back) {
                    const std::uint32_t prior = be32(r.bytes + 4 * (k - back));
                    loads_pointer = opcode(prior) == 15 && reg_a(prior) == 0 && reg_d(prior) == reg_a(insn) &&
                                    (prior & 0xFFFFu) == high_adjusted;
                }
            }
            if (!loads_pointer) continue;
            const std::uint32_t start = function_start(text, r.address + static_cast<std::uint32_t>(4 * k), 32);
            if (start == 0) continue;
            bool calls = false;
            each_word(text, start, 48, [&](std::uint32_t, std::uint32_t w) {
                calls = calls || is_call(w);
                return true;
            });
            if (!calls || start == init) continue;
            init = start;
            ++loaders;
        }
    }
    if (loaders != 1) return HbmFunctions{};
    // 4. HBMCalc: the next function, past HBMInit's `blr` and any padding.
    std::uint32_t end = 0;
    each_word(text, init, 64, [&](std::uint32_t at, std::uint32_t w) {
        if (w == kBlr) end = at;
        return true;
    });
    if (end == 0) return HbmFunctions{};
    std::uint32_t calc = end + 4;
    std::uint32_t insn = 0;
    while (word_at(text, calc, insn) && (insn == 0 || insn == kNop) && calc < end + 64) calc += 4;
    if (!word_at(text, calc, insn) || !is_stwu_r1(insn)) return HbmFunctions{};
    bool calls = false;
    each_word(text, calc, 32, [&](std::uint32_t, std::uint32_t w) {
        calls = calls || is_call(w);
        return true;
    });
    if (!calls) return HbmFunctions{};
    // The button the menu ends on: what HBMCalc returns (2008 on), else
    // HBMGetSelectBtnNum, the function after HBMDraw (the one after HBMCalc).
    std::uint32_t select = calc_answer(text, calc);
    const std::uint32_t draw = next_function(text, calc, 64);
    std::uint32_t draw_first = 0;
    if (select == 0 && draw != 0 && word_at(text, draw, draw_first) && is_stwu_r1(draw_first)) {
        const std::uint32_t candidate = next_function(text, draw, 256);
        if (looks_like_select(text, candidate)) select = candidate;
    }
    return HbmFunctions{init, calc, select};
}

}  // namespace riftwii
