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

}  // namespace riftwii
