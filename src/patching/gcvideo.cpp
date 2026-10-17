// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcvideo.hpp"

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

unsigned opcode(std::uint32_t w) { return w >> 26; }
unsigned reg_d(std::uint32_t w) { return (w >> 21) & 31; }
unsigned reg_a(std::uint32_t w) { return (w >> 16) & 31; }
std::uint16_t immediate(std::uint32_t w) { return static_cast<std::uint16_t>(w & 0xFFFF); }

bool is_lis(std::uint32_t w, std::uint16_t value) { return opcode(w) == 15 && reg_a(w) == 0 && immediate(w) == value; }
bool is_addi_of(std::uint32_t w, unsigned source, std::uint16_t value) {
    return opcode(w) == 14 && reg_a(w) == source && immediate(w) == value;
}

// `beq` (bc 12, a CR field's eq bit, no link) and where it goes.
bool is_beq(std::uint32_t at, std::uint32_t w, std::uint32_t& target) {
    if (opcode(w) != 16 || reg_d(w) != 12 || reg_a(w) % 4 != 2 || (w & 3) != 0) return false;
    const std::int32_t offset = static_cast<std::int16_t>(w & 0xFFFC);
    target = at + static_cast<std::uint32_t>(offset);
    return true;
}

// The sharp weights' two BP register writes, high halves and low ones.
constexpr std::uint16_t kSharpHigh0 = 0x5359, kSharpLow0 = 0x5000;
constexpr std::uint16_t kSharpHigh1 = 0x5400, kSharpLow1 = 0x0015;
// How far before the sharp weights the test of vf may be: the function
// writes the sample pattern (four registers) between them.
constexpr std::uint32_t kTestReach = 0x80;

}  // namespace

GcCopyFilterSwitch find_gc_copy_filter_switch(const std::vector<CodeRange>& text) {
    const Words code(text);
    GcCopyFilterSwitch found;
    unsigned count = 0;
    code.each([&](std::uint32_t a, std::uint32_t w) {
        if (!is_lis(w, kSharpHigh0) || !is_lis(code.at(a + 4), kSharpHigh1)) return;
        // Both halves made into the two words within the next two
        // instructions (the compiler orders them as it likes).
        const unsigned high0 = reg_d(w), high1 = reg_d(code.at(a + 4));
        bool low0 = false, low1 = false;
        for (std::uint32_t b = a + 8; b <= a + 12; b += 4) {
            low0 = low0 || is_addi_of(code.at(b), high0, kSharpLow0);
            low1 = low1 || is_addi_of(code.at(b), high1, kSharpLow1);
        }
        if (!low0 || !low1) return;
        for (std::uint32_t b = a - 4; code.has(b) && a - b <= kTestReach; b -= 4) {
            std::uint32_t target = 0;
            if (is_beq(b, code.at(b), target) && target == a) {
                found.branch = b;
                found.sharp = a;
                ++count;
                return;
            }
        }
    });
    if (count != 1) return GcCopyFilterSwitch{};
    return found;
}

}  // namespace riftwii
