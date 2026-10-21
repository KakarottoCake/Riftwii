// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcdsp.hpp"

#include <cstring>
#include <vector>

#include "riftwii/sha1.hpp"

namespace riftwii {

namespace {

// A DSP word: 16 bits, big-endian. A word address is a byte offset halved.
void store_word(std::uint8_t* p, std::uint16_t value) {
    p[0] = static_cast<std::uint8_t>(value >> 8);
    p[1] = static_cast<std::uint8_t>(value & 0xFF);
}

std::uint32_t load_pair(const std::uint8_t* base, std::uint32_t word_at) {
    const std::uint8_t* p = base + 2 * word_at;
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void store_pair(std::uint8_t* base, std::uint32_t word_at, std::uint32_t value) {
    std::uint8_t* p = base + 2 * word_at;
    p[0] = static_cast<std::uint8_t>(value >> 24);
    p[1] = static_cast<std::uint8_t>(value >> 16);
    p[2] = static_cast<std::uint8_t>(value >> 8);
    p[3] = static_cast<std::uint8_t>(value & 0xFF);
}

bool fits(std::size_t available, std::uint32_t word_at, std::uint32_t words) {
    return 2 * (std::size_t(word_at) + words) <= available;
}

GcDspFix make_simple(const char* name, std::uint32_t length, const std::uint8_t pattern[16],
                     const std::uint8_t sha1[20], std::uint16_t free_at, std::uint16_t call_a,
                     std::uint16_t call_b) {
    GcDspFix fix;
    fix.name = name;
    fix.length = length;
    std::memcpy(fix.pattern.data(), pattern, 16);
    std::memcpy(fix.sha1.data(), sha1, 20);
    fix.kind = GcDspKind::Simple;
    fix.simple.free_at = free_at;
    fix.simple.calls[0] = call_a;
    fix.simple.calls[1] = call_b;
    return fix;
}

GcDspFix make_ax(const char* name, std::uint32_t length, const std::uint8_t pattern[16], const std::uint8_t sha1[20],
                 std::uint16_t dup1, std::uint16_t dup2, std::uint16_t dup3, std::uint16_t dup2_call) {
    GcDspFix fix;
    fix.name = name;
    fix.length = length;
    std::memcpy(fix.pattern.data(), pattern, 16);
    std::memcpy(fix.sha1.data(), sha1, 20);
    fix.kind = GcDspKind::Ax;
    fix.ax.dup1 = dup1;
    fix.ax.dup2 = dup2;
    fix.ax.dup3 = dup3;
    fix.ax.dup2_call = dup2_call;
    return fix;
}

// call target and jmp target: 0x02BF/0x029F followed by the word address.
std::uint32_t call_word(std::uint32_t target) { return 0x02BF0000u | (target & 0xFFFFu); }
std::uint32_t jump_word(std::uint32_t target) { return 0x029F0000u | (target & 0xFFFFu); }

}  // namespace

std::size_t gcdsp_fix_count() {
    return 7;
}

const GcDspFix& gcdsp_fix_at(std::size_t index) {
    // Patterns are each program's first 16 bytes (its jump table); simple
    // programs start with a jump (0x029F), AX ones with a zero word and
    // then jumps. Lengths, hashes and AX word addresses are Nintendont's
    // DSP table values (kernel/Patch.c); Mario Party 4's program is the
    // one Nintendont lists for Sonic Mega Collection and Melee (its hash
    // and length match).
    static const std::uint8_t kSimpleSunshinePattern[16] = {0x02, 0x9F, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00,
                                                            0x02, 0xFF, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00};
    static const std::uint8_t kSimpleSunshineSha1[20] = {0x21, 0xD0, 0xC0, 0xEE, 0x25, 0x3D, 0x8C, 0x9E, 0x02, 0x58,
                                                         0x66, 0x7F, 0x3C, 0x1B, 0x11, 0xBC, 0x90, 0x1F, 0x33, 0xE2};
    static const std::uint8_t kSimpleLuigiPattern[16] = {0x02, 0x9F, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
                                                         0x02, 0xFF, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00};
    static const std::uint8_t kSimpleLuigiSha1[20] = {0x09, 0xF1, 0x6B, 0x48, 0x57, 0x15, 0xEB, 0x3F, 0x67, 0x3E,
                                                      0x19, 0xEF, 0x7A, 0xCF, 0xE3, 0x60, 0x7D, 0x2E, 0x4F, 0x02};
    static const std::uint8_t kAxSonicPattern[16] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x9F, 0x0C, 0x10,
                                                     0x02, 0x9F, 0x0C, 0x1F, 0x02, 0x9F, 0x0C, 0x3B};
    static const std::uint8_t kAxSonicSha1[20] = {0xBA, 0xDC, 0x60, 0x15, 0x33, 0x33, 0x28, 0xED, 0xB1, 0x0E,
                                                  0x72, 0xF2, 0x5B, 0x5A, 0xFB, 0xF3, 0xEF, 0x90, 0x30, 0x90};
    static const std::uint8_t kAxMp5Pattern[16] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x9F, 0x0E, 0x76,
                                                   0x02, 0x9F, 0x0E, 0x85, 0x02, 0x9F, 0x0E, 0xA1};
    static const std::uint8_t kAxMp5Sha1[20] = {0xBC, 0x17, 0x36, 0x81, 0x7A, 0x14, 0xB2, 0x1C, 0xCB, 0xF7,
                                                0x3A, 0xD6, 0x8F, 0xDA, 0x57, 0xF8, 0x43, 0x78, 0x1A, 0xAE};
    static const std::uint8_t kAxBgePattern[16] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x9F, 0x0E, 0xB3,
                                                   0x02, 0x9F, 0x0E, 0xC2, 0x02, 0x9F, 0x0E, 0xDE};
    static const std::uint8_t kAxBgeSha1[20] = {0xD9, 0x39, 0x63, 0xE3, 0x91, 0xD1, 0xA8, 0x5E, 0x4D, 0x5F,
                                                0xD9, 0xC2, 0x9A, 0xF9, 0x3A, 0xA9, 0xAF, 0x8D, 0x4E, 0xF2};
    static const std::uint8_t kAxSfaPattern[16] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x9F, 0x0E, 0x88,
                                                   0x02, 0x9F, 0x0E, 0x97, 0x02, 0x9F, 0x0E, 0xB3};
    static const std::uint8_t kAxSfaSha1[20] = {0x37, 0x44, 0xC3, 0x82, 0xC8, 0x98, 0x42, 0xD4, 0x9D, 0x68,
                                                0x83, 0x1C, 0x2B, 0x06, 0x7E, 0xC7, 0xE8, 0x64, 0x32, 0x44};
    static const std::uint8_t kAxPmPattern[16] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x9F, 0x0E, 0x71,
                                                  0x02, 0x9F, 0x0E, 0x80, 0x02, 0x9F, 0x0E, 0x9C};
    static const std::uint8_t kAxPmSha1[20] = {0xBC, 0x1E, 0x0A, 0x75, 0x09, 0xA6, 0x3E, 0x7C, 0xE6, 0x30,
                                               0x44, 0xBE, 0xCC, 0x8D, 0x67, 0x1F, 0xA7, 0xC7, 0x44, 0xE5};
    static const GcDspFix kFixes[] = {
        make_simple("Super Mario Sunshine", 0x1CE0, kSimpleSunshinePattern, kSimpleSunshineSha1, 0x0E66, 0x0531,
                    0x057C),
        make_simple("Luigi's Mansion", 0x17E0, kSimpleLuigiPattern, kSimpleLuigiSha1, 0x0BE8, 0x00BE, 0x0109),
        make_ax("AX (Melee, Sonic Mega Collection, Mario Party 4)", 0x19E0, kAxSonicPattern, kAxSonicSha1, 0x05A8, 0x065D, 0x0707, 0x008F),
        make_ax("Mario Party 5", 0x1EC0, kAxMp5Pattern, kAxMp5Sha1, 0x06A3, 0x0758, 0x0802, 0x008F),
        make_ax("Beyond Good and Evil", 0x1F20, kAxBgePattern, kAxBgeSha1, 0x06E0, 0x0795, 0x083F, 0x008F),
        make_ax("Star Fox Assault", 0x1F00, kAxSfaPattern, kAxSfaSha1, 0x069E, 0x0753, 0x0814, 0x00A4),
        make_ax("Paper Mario: The Thousand-Year Door", 0x1EC0, kAxPmPattern, kAxPmSha1, 0x069E, 0x0753, 0x07FD,
                0x008F),
    };
    return kFixes[index];
}

bool gcdsp_matches(const std::uint8_t* program, std::size_t available, const GcDspFix& fix) {
    if (available < fix.length) return false;
    if (std::memcmp(program, fix.pattern.data(), fix.pattern.size()) != 0) return false;
    const Sha1Digest digest = sha1(program, fix.length);
    return std::memcmp(digest.data(), fix.sha1.data(), fix.sha1.size()) == 0;
}

const GcDspFix* gcdsp_find(const std::uint8_t* program, std::size_t available) {
    for (std::size_t i = 0; i < gcdsp_fix_count(); ++i) {
        const GcDspFix& fix = gcdsp_fix_at(i);
        if (gcdsp_matches(program, available, fix)) return &fix;
    }
    return nullptr;
}

bool gcdsp_apply_simple(std::uint8_t* program, std::size_t available, std::uint16_t free_at,
                        std::uint16_t call_a, std::uint16_t call_b) {
    if (!fits(available, free_at, 6) || !fits(available, call_a, 2) || !fits(available, call_b, 2)) return false;
    const std::uint32_t original = load_pair(program, call_a);
    store_pair(program, free_at, 0x02601000u);  // ori $AC0.M, #0x1000
    store_pair(program, free_at + 2, original);  // the two words the calls replace
    store_pair(program, free_at + 4, 0x02DF02DFu);  // ret, ret
    store_pair(program, call_a, call_word(free_at));
    store_pair(program, call_b, call_word(free_at));
    return true;
}

bool gcdsp_apply_ax(std::uint8_t* program, std::size_t available, std::uint16_t dup1, std::uint16_t dup2,
                    std::uint16_t dup3, std::uint16_t dup2_call) {
    constexpr std::uint32_t kMoveWords = 0x22;  // words relocated from dup1
    constexpr std::uint32_t kCopyWords = 0x12;  // upper half kept at dup2
    constexpr std::uint32_t kCallWords = 0x04;  // call plus jmp heading each copy
    constexpr std::uint32_t kNew1Words = 0x0C;  // what the lower half grows by: three orc, a jmp, the seven format words
    const std::uint32_t helper = dup2 + kCallWords + kCopyWords + 1;  // the 11-word helper past the moved tail
    if (!fits(available, dup1, kMoveWords) || !fits(available, dup2, kMoveWords) ||
        !fits(available, dup1 + 0x98, 2) || !fits(available, dup2 + dup2_call, 2) ||
        !fits(available, helper, 11)) {
        return false;
    }
    if (dup3 != kGcDspNoDup3 &&
        (!fits(available, dup3, kCallWords + kNew1Words) || !fits(available, dup3 + 0x5D, 4))) {
        return false;
    }
    // The moved tail ends with a ret; the helper starts right after it.
    std::uint32_t source = dup1 + kMoveWords;
    std::uint32_t target = helper;
    store_word(program + 2 * (target - 1), 0x02DF);
    --target;
    while (source >= std::uint32_t(dup1) + 1) {
        source -= 2;
        if (!fits(available, source, 2)) return false;
        std::uint32_t pair = load_pair(program, source);
        if (pair == 0x195E2ED1u) {
            // A format-dependent load: make room for the seven format words
            // and retarget the load at the second accumulator.
            target -= 7;
            if (!fits(available, target, 9)) return false;
            store_pair(program, target, 0x03400003u);
            store_pair(program, target + 2, 0x8100009Eu);
            store_pair(program, target + 4, 0x200002CAu);
            store_word(program + 2 * (target + 6), 0x1FFEu);
            pair |= 0x00010100u;
        }
        target -= 2;
        if (!fits(available, target, 2)) return false;
        store_pair(program, target, pair);
        if ((pair & 0x0000FFF1u) == 0x00002ED0u) {
            // An address store: slip the OR with the format-built bit
            // between its two words.
            target -= 1;
            if (!fits(available, target, 2)) return false;
            store_pair(program, target, (pair & 0xFFFF0000u) | 0x3E00u);
        }
        if (target == dup2 + kCallWords) {
            // The upper half is full: the rest goes to dup1, jumping back.
            target = dup1 + kCallWords + kMoveWords - kCopyWords + kNew1Words;
            target -= 2;
            if (!fits(available, target, 2)) return false;
            store_pair(program, target, jump_word(dup2 + kCallWords));
        }
    }
    store_pair(program, dup1, call_word(dup1 + kCallWords));
    store_pair(program, dup1 + 2, jump_word(dup1 + kMoveWords));
    store_pair(program, dup2, call_word(dup1 + kCallWords));
    store_pair(program, dup2 + 2, jump_word(dup2 + kMoveWords));
    const std::uint32_t moved_load = load_pair(program, dup1 + 0x98);
    store_pair(program, dup1 + 0x98, call_word(helper));
    store_pair(program, dup2 + dup2_call, call_word(helper));
    store_word(program + 2 * helper, static_cast<std::uint16_t>(moved_load >> 16));
    store_pair(program, helper + 1, 0x27D10340u);
    store_pair(program, helper + 3, 0x00038100u);
    store_pair(program, helper + 5, 0x009E1FFFu);
    store_word(program + 2 * (helper + 7), 0x02CAu);
    store_word(program + 2 * (helper + 8), static_cast<std::uint16_t>(moved_load & 0xFFFFu));
    store_pair(program, helper + 9, 0x3D0002DFu);
    if (dup3 == kGcDspNoDup3) return true;
    store_pair(program, dup3, call_word(dup1 + kCallWords));
    store_pair(program, dup3 + 2, jump_word(dup3 + kMoveWords));
    store_pair(program, dup3 + 4, 0x27D10340u);
    store_pair(program, dup3 + 6, 0x00038100u);
    store_pair(program, dup3 + 8, 0x009E1FFFu);
    store_word(program + 2 * (dup3 + 0x0A), 0x02CAu);
    const std::uint32_t third_first = load_pair(program, dup3 + 0x5D);
    store_word(program + 2 * (dup3 + 0x0B), static_cast<std::uint16_t>(third_first >> 16));
    store_word(program + 2 * (dup3 + 0x0C), 0x3D00u);
    store_word(program + 2 * (dup3 + 0x0D), static_cast<std::uint16_t>(third_first & 0xFFFFu));
    const std::uint32_t third_second = load_pair(program, dup3 + 0x5F);
    store_pair(program, dup3 + 0x0E, third_second);
    store_pair(program, dup3 + 0x5D, jump_word(dup3 + kCallWords));
    return true;
}

bool gcdsp_apply(const GcDspFix& fix, std::uint8_t* program, std::size_t available) {
    if (available < fix.length) return false;
    // The rewrite runs on a copy: a bound found wrong halfway would
    // otherwise leave the game a half-patched sound program.
    std::vector<std::uint8_t> copy(program, program + fix.length);
    const bool done =
        fix.kind == GcDspKind::Ax
            ? gcdsp_apply_ax(copy.data(), copy.size(), fix.ax.dup1, fix.ax.dup2, fix.ax.dup3, fix.ax.dup2_call)
            : gcdsp_apply_simple(copy.data(), copy.size(), fix.simple.free_at, fix.simple.calls[0],
                                 fix.simple.calls[1]);
    if (done) std::memcpy(program, copy.data(), copy.size());
    return done;
}

}  // namespace riftwii
