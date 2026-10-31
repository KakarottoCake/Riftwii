// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcaram.hpp"

namespace riftwii {
namespace {

// The game's code as words by address.
class Words {
public:
    explicit Words(const std::vector<CodeRange>& text) : text_(text) {}
    bool has(std::uint32_t a) const { return find(a) != nullptr; }
    std::uint32_t at(std::uint32_t a) const {
        const std::uint8_t* p = find(a);
        if (p == nullptr) return 0;
        return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
    }
    template <class F>
    void each(F f) const {
        for (const CodeRange& r : text_)
            for (std::uint32_t i = 0; i + 4 <= r.size; i += 4) f(r.address + i, at(r.address + i));
    }

private:
    const std::uint8_t* find(std::uint32_t a) const {
        for (const CodeRange& r : text_)
            if (a >= r.address && a + 4 <= r.address + r.size) return r.bytes + (a - r.address);
        return nullptr;
    }
    const std::vector<CodeRange>& text_;
};

constexpr std::uint32_t kBlr = 0x4E800020;
constexpr std::uint32_t kBleSkipOne = 0x4081000C;  // ble +12: over the second call and its store

// Where a `bl` at `a` goes, 0 if the word is not one.
std::uint32_t bl_target(std::uint32_t a, std::uint32_t w) {
    if ((w & 0xFC000003) != 0x48000001) return 0;
    std::uint32_t offset = w & 0x03FFFFFC;
    if (offset & 0x02000000) offset |= 0xFC000000;
    return a + offset;
}

// ARGetSize: `lwz r3,d(r13)`, `blr`.
bool is_r13_word_getter(const Words& code, std::uint32_t f) {
    return code.has(f + 4) && (code.at(f) & 0xFFFF0000) == 0x806D0000 && code.at(f + 4) == kBlr;
}

}  // namespace

std::vector<GcAramClamp> find_gc_aram_size_clamps(const std::vector<CodeRange>& text) {
    const Words code(text);
    std::vector<GcAramClamp> found;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        const std::uint32_t getter = bl_target(a, w);
        if (getter == 0 || !is_r13_word_getter(code, getter) || !code.has(a + 20)) return;

        // lwz rA,top(r13): the pool's top, which the clamp then stores.
        const std::uint32_t load = code.at(a + 4);
        if ((load & 0xFC1F0000) != 0x800D0000) return;
        const std::uint32_t top_reg = (load >> 21) & 31;
        const std::uint32_t top_offset = load & 0xFFFF;

        const std::uint32_t compare = 0x7C001840 | (top_reg << 16);  // cmplw rA,r3
        const std::uint32_t store = 0x906D0000 | top_offset;          // stw r3,top(r13)
        if (code.at(a + 8) != compare || code.at(a + 12) != kBleSkipOne) return;
        if (bl_target(a + 16, code.at(a + 16)) != getter || code.at(a + 20) != store) return;

        found.push_back({a, a + 16});
    });
    return found;
}

std::vector<std::uint32_t> find_gc_aram_ends_after_base(const std::vector<CodeRange>& text) {
    const Words code(text);
    std::vector<std::uint32_t> found;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        const std::uint32_t getter = bl_target(a, w);
        const bool base_getter = getter != 0 && code.has(getter + 4) && code.at(getter) == 0x38604000 &&
                                 code.at(getter + 4) == kBlr;  // li r3,0x4000; blr
        if (!base_getter) return;
        // The words after the call, followed value by value: which
        // registers hold the base (r3, or one made from it by addi or mr),
        // and the end's `lis` with its register left alone up to the subf.
        std::uint32_t from_base = 1u << 3;
        std::uint32_t lis_at = 0;
        unsigned end_reg = 0;
        for (std::uint32_t b = a + 4; b <= a + 24 && code.has(b); b += 4) {
            const std::uint32_t x = code.at(b);
            const unsigned opcode = x >> 26, field_d = (x >> 21) & 31, field_a = (x >> 16) & 31;
            const unsigned field_b = (x >> 11) & 31;
            const std::uint32_t end = x & 0xFFFF;
            const bool subf = (x & 0xFC0007FE) == 0x7C000050;  // subf rD,rA,rB: rB - rA
            if (subf && lis_at != 0 && field_b == end_reg && (from_base >> field_a) & 1) {
                found.push_back(lis_at);
                return;
            }
            const bool lis = opcode == 15 && field_a == 0;
            if (lis && lis_at == 0 && end != 0 && end <= 0x100 && b <= a + 12) {
                lis_at = b;
                end_reg = field_d;
                from_base &= ~(1u << field_d);
                continue;
            }
            // Only addi/li, lis, ori and mr may come between: anything else
            // (a branch, a compare, a load, a store, other arithmetic) ends
            // the search, so what the words do is never guessed.
            const bool mr = opcode == 31 && ((x >> 1) & 0x3FF) == 444 && field_d == field_b;  // or rA,rS,rS
            const bool addi = opcode == 14, ori = opcode == 24;
            if (!mr && !addi && !ori && !lis) return;
            const unsigned written = mr || ori ? field_a : field_d;  // or/ori write rA
            if (lis_at != 0 && written == end_reg) return;  // the end changed before the subtraction
            const bool derived = (addi && field_a != 0 && ((from_base >> field_a) & 1)) ||
                                 (mr && ((from_base >> field_d) & 1));
            if (derived) from_base |= 1u << written;
            else from_base &= ~(1u << written);
        }
    });
    return found;
}

std::vector<std::uint32_t> find_gc_aram_tops_below_size(const std::vector<CodeRange>& text) {
    const Words code(text);
    std::vector<std::uint32_t> found;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        const std::uint32_t getter = bl_target(a, w);
        if (getter == 0 || !is_r13_word_getter(code, getter) || !code.has(a + 4)) return;

        // addi rD,r3,-N: the top, N below what ARGetSize answered.
        const std::uint32_t below = code.at(a + 4);
        const bool addi_from_r3 = (below >> 26) == 14 && ((below >> 16) & 31) == 3;
        const std::int32_t delta = static_cast<std::int16_t>(below & 0xFFFF);
        if (!addi_from_r3 || delta >= 0 || delta < -0x10000 || (delta & 31) != 0) return;
        const std::uint32_t top_reg = (below >> 21) & 31;

        // stw rD,top(r13) within the next two words. Only an mr that leaves
        // rD alone may come before it (NightFire's mr r3,r0); anything else
        // (a branch, a word that writes rD) ends the search.
        const std::uint32_t store = 0x900D0000 | (top_reg << 21);  // stw rD,d(r13), d masked out
        bool stored = false;
        for (std::uint32_t b = a + 8; b <= a + 12 && code.has(b); b += 4) {
            const std::uint32_t x = code.at(b);
            if ((x & 0xFFFF0000) == store) {
                stored = true;
                break;
            }
            const bool mr = (x >> 26) == 31 && ((x >> 1) & 0x3FF) == 444 && ((x >> 21) & 31) == ((x >> 11) & 31);
            const bool writes_top = ((x >> 16) & 31) == top_reg;
            if (!mr || writes_top) break;
        }
        if (stored) found.push_back(a);
    });
    return found;
}

}  // namespace riftwii
