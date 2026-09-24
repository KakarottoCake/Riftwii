// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/codehook.hpp"

namespace riftwii {
namespace {

std::uint32_t word(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

// In the retrace handler: mr r3,r7; addi r4,r7,0x34; addi r5,r7,0x38;
// addi r6,r7,0x4C (its callback arguments).
constexpr std::uint32_t kRetrace[4] = {0x7CE33B78, 0x38870034, 0x38A70038, 0x38C7004C};
constexpr std::uint32_t kBlr = 0x4E800020;
constexpr std::size_t kMaxDistance = 0x1000;  // bytes from the pattern to the function's end

}  // namespace

std::uint32_t find_cheat_hook(const std::vector<CodeRange>& text) {
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr || r.size < 16) continue;
        for (std::size_t at = 0; at + 16 <= r.size; at += 4) {
            bool match = true;
            for (std::size_t i = 0; i < 4 && match; ++i) match = word(r.bytes + at + 4 * i) == kRetrace[i];
            if (!match) continue;
            for (std::size_t b = at + 16; b + 4 <= r.size && b - at < kMaxDistance; b += 4) {
                if (word(r.bytes + b) == kBlr) return r.address + static_cast<std::uint32_t>(b);
            }
        }
    }
    return 0;
}

std::uint32_t encode_b(std::uint32_t from, std::uint32_t to) {
    const std::int64_t delta = std::int64_t(to) - std::int64_t(from);
    if (delta < -0x2000000 || delta >= 0x2000000 || (delta & 3) != 0) return 0;
    return 0x48000000u | (static_cast<std::uint32_t>(delta) & 0x03FFFFFCu);
}

}  // namespace riftwii
