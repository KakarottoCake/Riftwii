// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/codehook.hpp"

namespace riftwii {
namespace {

std::uint32_t word(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void put(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

// In the retrace handler: mr r3,r7; addi r4,r7,0x34; addi r5,r7,0x38;
// addi r6,r7,0x4C (its callback arguments).
constexpr std::uint32_t kRetrace[4] = {0x7CE33B78, 0x38870034, 0x38A70038, 0x38C7004C};
// In AXNextFrame: li r0,14; mr r3,r31; sth r0,0(r5); li r4,0x80.
constexpr std::uint32_t kAudioFrame[4] = {0x3800000E, 0x7FE3FB78, 0xB0050000, 0x38800080};
constexpr std::uint32_t kBlr = 0x4E800020;
constexpr std::size_t kMaxDistance = 0x1000;  // bytes from the pattern to the function's end

// The handler's `lis r15, 0x8000` / `ori r15, r15, 0x22A8` (the list at
// kCodeListAddress), 0x104 bytes in.
constexpr std::size_t kListLoadOffset = 0x104;
constexpr std::uint32_t kLisR15 = 0x3DE00000, kOriR15 = 0x61EF0000;

}  // namespace

std::uint32_t find_code_hook(const std::vector<CodeRange>& text, CodeHook hook) {
    const std::uint32_t* pattern = hook == CodeHook::AudioFrame ? kAudioFrame : kRetrace;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr || r.size < 16) continue;
        for (std::size_t at = 0; at + 16 <= r.size; at += 4) {
            bool match = true;
            for (std::size_t i = 0; i < 4 && match; ++i) match = word(r.bytes + at + 4 * i) == pattern[i];
            if (!match) continue;
            for (std::size_t b = at + 16; b + 4 <= r.size && b - at < kMaxDistance; b += 4) {
                if (word(r.bytes + b) == kBlr) return r.address + static_cast<std::uint32_t>(b);
            }
        }
    }
    return 0;
}

const std::uint32_t* code_hook_pattern(CodeHook hook) {
    return hook == CodeHook::AudioFrame ? kAudioFrame : kRetrace;
}

std::vector<std::uint32_t> find_dol_jumps(const std::vector<CodeRange>& text) {
    static const std::uint32_t kJump[4] = {0x7C0004AC, 0x4C00012C, 0x7FE903A6, 0x4E800420};
    std::vector<std::uint32_t> found;
    for (const CodeRange& r : text) {
        if (r.bytes == nullptr || r.size < 16) continue;
        for (std::size_t at = 0; at + 16 <= r.size; at += 4) {
            bool match = true;
            for (std::size_t i = 0; i < 4 && match; ++i) match = word(r.bytes + at + 4 * i) == kJump[i];
            if (match) found.push_back(r.address + static_cast<std::uint32_t>(at + 12));
        }
    }
    return found;
}

std::uint32_t encode_b(std::uint32_t from, std::uint32_t to) {
    const std::int64_t delta = std::int64_t(to) - std::int64_t(from);
    if (delta < -0x2000000 || delta >= 0x2000000 || (delta & 3) != 0) return 0;
    return 0x48000000u | (static_cast<std::uint32_t>(delta) & 0x03FFFFFCu);
}

bool relocate_code_list(std::uint8_t* handler, std::size_t size, std::uint32_t address) {
    if (handler == nullptr || size < kListLoadOffset + 8) return false;
    std::uint8_t* lis = handler + kListLoadOffset;
    std::uint8_t* ori = lis + 4;
    const std::uint32_t expected_hi = kCodeListAddress >> 16, expected_lo = kCodeListAddress & 0xFFFF;
    if (word(lis) != (kLisR15 | expected_hi) || word(ori) != (kOriR15 | expected_lo)) return false;
    put(lis, kLisR15 | (address >> 16));
    put(ori, kOriR15 | (address & 0xFFFF));
    return true;
}

}  // namespace riftwii
