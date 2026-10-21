// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The GameCube sound-program patches (riftwii/gcdsp.hpp) on made-up data:
// recognition of both start kinds against synthetic table entries (never
// with Nintendo bytes), refusal of a near miss, the known table's values,
// and both rewrites word for word against values worked out from the
// algorithm. No Nintendo data lives here.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/gcdsp.hpp"
#include "riftwii/sha1.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; \
            g_failures++; \
        } \
    } while (0)
#define EXPECT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ \
                      << std::endl; \
            g_failures++; \
        } \
    } while (0)

using namespace riftwii;

namespace {

std::uint16_t get_word(const std::vector<std::uint8_t>& program, std::uint32_t word_at) {
    return static_cast<std::uint16_t>((std::uint16_t(program[2 * word_at]) << 8) | program[2 * word_at + 1]);
}

void set_word(std::vector<std::uint8_t>& program, std::uint32_t word_at, std::uint16_t value) {
    program[2 * word_at] = static_cast<std::uint8_t>(value >> 8);
    program[2 * word_at + 1] = static_cast<std::uint8_t>(value & 0xFF);
}

// A synthetic table entry for `program`: its first 16 bytes as the
// pattern, its SHA-1 as the hash. Recognition works on any start kind.
GcDspFix entry_for(const std::vector<std::uint8_t>& program, GcDspKind kind) {
    GcDspFix fix;
    fix.name = "synthetic";
    fix.length = static_cast<std::uint32_t>(program.size());
    std::memcpy(fix.pattern.data(), program.data(), 16);
    const Sha1Digest digest = sha1(program.data(), program.size());
    std::memcpy(fix.sha1.data(), digest.data(), 20);
    fix.kind = kind;
    return fix;
}

void test_recognition() {
    // A simple-kind start: a jump (0x029F) then halts, like the real ones.
    std::vector<std::uint8_t> simple(64, 0xAA);
    const std::uint8_t simple_head[16] = {0x02, 0x9F, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00,
                                          0x02, 0xFF, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00};
    std::memcpy(simple.data(), simple_head, 16);
    const GcDspFix simple_fix = entry_for(simple, GcDspKind::Simple);
    EXPECT_TRUE(gcdsp_matches(simple.data(), simple.size(), simple_fix));
    EXPECT_TRUE(gcdsp_find(simple.data(), simple.size()) == nullptr);  // no built-in false positive

    // An AX-kind start: a zero word and then jumps.
    std::vector<std::uint8_t> ax(64, 0xBB);
    const std::uint8_t ax_head[16] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x9F, 0x0C, 0x10,
                                      0x02, 0x9F, 0x0C, 0x1F, 0x02, 0x9F, 0x0C, 0x3B};
    std::memcpy(ax.data(), ax_head, 16);
    const GcDspFix ax_fix = entry_for(ax, GcDspKind::Ax);
    EXPECT_TRUE(gcdsp_matches(ax.data(), ax.size(), ax_fix));
    EXPECT_TRUE(gcdsp_find(ax.data(), ax.size()) == nullptr);

    // A near miss: the pattern matches but one byte past it differs, so the
    // SHA-1 does not. Never matched on the jump words alone.
    std::vector<std::uint8_t> near = simple;
    near[32] ^= 0x01;
    EXPECT_TRUE(!gcdsp_matches(near.data(), near.size(), simple_fix));
    EXPECT_TRUE(gcdsp_find(near.data(), near.size()) == nullptr);

    // Another near miss for the AX kind.
    std::vector<std::uint8_t> near_ax = ax;
    near_ax[63] ^= 0x80;
    EXPECT_TRUE(!gcdsp_matches(near_ax.data(), near_ax.size(), ax_fix));

    // A wrong pattern and a truncated program.
    std::vector<std::uint8_t> wrong = simple;
    wrong[0] = 0x03;
    EXPECT_TRUE(!gcdsp_matches(wrong.data(), wrong.size(), simple_fix));
    EXPECT_TRUE(!gcdsp_matches(simple.data(), simple.size() - 1, simple_fix));
}

void test_table_values() {
    EXPECT_EQ(gcdsp_fix_count(), 7u);
    const GcDspFix& sunshine = gcdsp_fix_at(0);
    EXPECT_TRUE(std::string(sunshine.name) == "Super Mario Sunshine");
    EXPECT_EQ(sunshine.length, 0x1CE0u);
    EXPECT_TRUE(sunshine.kind == GcDspKind::Simple);
    EXPECT_EQ(sunshine.simple.free_at, 0x0E66u);
    EXPECT_EQ(sunshine.simple.calls[0], 0x0531u);
    EXPECT_EQ(sunshine.simple.calls[1], 0x057Cu);
    const GcDspFix& luigi = gcdsp_fix_at(1);
    EXPECT_TRUE(std::string(luigi.name) == "Luigi's Mansion");
    EXPECT_EQ(luigi.length, 0x17E0u);
    EXPECT_EQ(luigi.simple.free_at, 0x0BE8u);
    const GcDspFix& sonic = gcdsp_fix_at(2);
    EXPECT_TRUE(std::string(sonic.name) == "AX (Melee, Sonic Mega Collection, Mario Party 4)");
    EXPECT_EQ(sonic.length, 0x19E0u);
    EXPECT_TRUE(sonic.kind == GcDspKind::Ax);
    EXPECT_EQ(sonic.ax.dup1, 0x05A8u);
    EXPECT_EQ(sonic.ax.dup2, 0x065Du);
    EXPECT_EQ(sonic.ax.dup3, 0x0707u);
    EXPECT_EQ(sonic.ax.dup2_call, 0x008Fu);
    EXPECT_EQ(sonic.pattern[0], 0x00u);  // the AX start kind
    EXPECT_EQ(sunshine.pattern[0], 0x02u);  // the simple start kind
    const GcDspFix& mp5 = gcdsp_fix_at(3);
    EXPECT_EQ(mp5.length, 0x1EC0u);
    EXPECT_EQ(mp5.ax.dup1, 0x06A3u);
    EXPECT_EQ(mp5.ax.dup2, 0x0758u);
    EXPECT_EQ(mp5.ax.dup3, 0x0802u);
    const GcDspFix& bge = gcdsp_fix_at(4);
    EXPECT_EQ(bge.length, 0x1F20u);
    EXPECT_EQ(bge.ax.dup1, 0x06E0u);
    EXPECT_EQ(bge.ax.dup2, 0x0795u);
    EXPECT_EQ(bge.ax.dup3, 0x083Fu);
    const GcDspFix& sfa = gcdsp_fix_at(5);
    EXPECT_EQ(sfa.length, 0x1F00u);
    EXPECT_EQ(sfa.ax.dup2_call, 0x00A4u);
    const GcDspFix& pm = gcdsp_fix_at(6);
    EXPECT_EQ(pm.length, 0x1EC0u);
    EXPECT_EQ(pm.ax.dup3, 0x07FDu);
}

void test_simple_rewrite() {
    std::vector<std::uint8_t> program(128, 0);
    set_word(program, 20, 0x1234);
    set_word(program, 21, 0x5678);
    EXPECT_TRUE(gcdsp_apply_simple(program.data(), program.size(), 10, 20, 30));
    EXPECT_EQ(get_word(program, 10), 0x0260u);  // ori $AC0.M ...
    EXPECT_EQ(get_word(program, 11), 0x1000u);  // ... #0x1000
    EXPECT_EQ(get_word(program, 12), 0x1234u);  // the replaced words
    EXPECT_EQ(get_word(program, 13), 0x5678u);
    EXPECT_EQ(get_word(program, 14), 0x02DFu);  // ret, ret
    EXPECT_EQ(get_word(program, 15), 0x02DFu);
    EXPECT_EQ(get_word(program, 20), 0x02BFu);  // call free_at
    EXPECT_EQ(get_word(program, 21), 10u);
    EXPECT_EQ(get_word(program, 30), 0x02BFu);
    EXPECT_EQ(get_word(program, 31), 10u);
    EXPECT_TRUE(!gcdsp_apply_simple(program.data(), 20, 10, 20, 30));  // past the end
}

void test_ax_rewrite() {
    constexpr std::uint16_t kDup1 = 0x40, kDup2 = 0xA0, kDup3 = 0x100, kOff = 0x8F;
    constexpr std::uint32_t kWords = 0x200;
    std::vector<std::uint8_t> program(2 * kWords, 0);
    for (std::uint32_t i = 0; i < kWords; ++i) set_word(program, i, static_cast<std::uint16_t>(0x4000 | (i & 0xFFF)));
    // Distinctive original pairs the helpers keep.
    set_word(program, kDup1 + 0x98, 0x5555);
    set_word(program, kDup1 + 0x99, 0x6666);
    set_word(program, kDup3 + 0x5D, 0x7777);
    set_word(program, kDup3 + 0x5E, 0x1111);  // second half of the 0x5D pair
    set_word(program, kDup3 + 0x5F, 0x8888);
    set_word(program, kDup3 + 0x60, 0x9999);
    const std::uint16_t top_hi = get_word(program, kDup1 + 0x20);
    const std::uint16_t top_lo = get_word(program, kDup1 + 0x21);
    EXPECT_TRUE(gcdsp_apply_ax(program.data(), program.size(), kDup1, kDup2, kDup3, kOff));

    // Both copies head for the moved code.
    EXPECT_EQ(get_word(program, kDup1), 0x02BFu);
    EXPECT_EQ(get_word(program, kDup1 + 1), std::uint16_t(kDup1 + 4));
    EXPECT_EQ(get_word(program, kDup1 + 2), 0x029Fu);
    EXPECT_EQ(get_word(program, kDup1 + 3), std::uint16_t(kDup1 + 0x22));
    EXPECT_EQ(get_word(program, kDup2), 0x02BFu);
    EXPECT_EQ(get_word(program, kDup2 + 1), std::uint16_t(kDup1 + 4));
    EXPECT_EQ(get_word(program, kDup2 + 2), 0x029Fu);
    EXPECT_EQ(get_word(program, kDup2 + 3), std::uint16_t(kDup2 + 0x22));
    // The moved tail's ret and the link between the halves.
    EXPECT_EQ(get_word(program, kDup2 + 0x16), 0x02DFu);
    EXPECT_EQ(get_word(program, kDup1 + 0x1E), 0x029Fu);
    EXPECT_EQ(get_word(program, kDup1 + 0x1F), std::uint16_t(kDup2 + 4));
    // The first moved pair lands at the top of the upper half.
    EXPECT_EQ(get_word(program, kDup2 + 0x14), top_hi);
    EXPECT_EQ(get_word(program, kDup2 + 0x15), top_lo);
    // The helper past the tail keeps the dup1+0x98 pair around the format read.
    const std::uint32_t helper = kDup2 + 0x17;
    EXPECT_EQ(get_word(program, helper), 0x5555u);
    EXPECT_EQ(get_word(program, helper + 1), 0x27D1u);
    EXPECT_EQ(get_word(program, helper + 2), 0x0340u);
    EXPECT_EQ(get_word(program, helper + 3), 0x0003u);
    EXPECT_EQ(get_word(program, helper + 4), 0x8100u);
    EXPECT_EQ(get_word(program, helper + 5), 0x009Eu);
    EXPECT_EQ(get_word(program, helper + 6), 0x1FFFu);
    EXPECT_EQ(get_word(program, helper + 7), 0x02CAu);
    EXPECT_EQ(get_word(program, helper + 8), 0x6666u);
    EXPECT_EQ(get_word(program, helper + 9), 0x3D00u);
    EXPECT_EQ(get_word(program, helper + 10), 0x02DFu);
    EXPECT_EQ(get_word(program, kDup1 + 0x98), 0x02BFu);
    EXPECT_EQ(get_word(program, kDup1 + 0x99), helper);
    EXPECT_EQ(get_word(program, kDup2 + kOff), 0x02BFu);
    EXPECT_EQ(get_word(program, kDup2 + kOff + 1), helper);
    // Dup3's own helper and its jump back to it.
    EXPECT_EQ(get_word(program, kDup3), 0x02BFu);
    EXPECT_EQ(get_word(program, kDup3 + 1), std::uint16_t(kDup1 + 4));
    EXPECT_EQ(get_word(program, kDup3 + 2), 0x029Fu);
    EXPECT_EQ(get_word(program, kDup3 + 3), std::uint16_t(kDup3 + 0x22));
    EXPECT_EQ(get_word(program, kDup3 + 4), 0x27D1u);
    EXPECT_EQ(get_word(program, kDup3 + 5), 0x0340u);
    EXPECT_EQ(get_word(program, kDup3 + 6), 0x0003u);
    EXPECT_EQ(get_word(program, kDup3 + 7), 0x8100u);
    EXPECT_EQ(get_word(program, kDup3 + 8), 0x009Eu);
    EXPECT_EQ(get_word(program, kDup3 + 9), 0x1FFFu);
    EXPECT_EQ(get_word(program, kDup3 + 0x0A), 0x02CAu);
    EXPECT_EQ(get_word(program, kDup3 + 0x0B), 0x7777u);
    EXPECT_EQ(get_word(program, kDup3 + 0x0C), 0x3D00u);
    EXPECT_EQ(get_word(program, kDup3 + 0x0D), 0x1111u);
    EXPECT_EQ(get_word(program, kDup3 + 0x0E), 0x8888u);
    EXPECT_EQ(get_word(program, kDup3 + 0x0F), 0x9999u);
    EXPECT_EQ(get_word(program, kDup3 + 0x5D), 0x029Fu);
    EXPECT_EQ(get_word(program, kDup3 + 0x5E), std::uint16_t(kDup3 + 4));

    // Without a third copy the third area stays untouched.
    std::vector<std::uint8_t> two(2 * kWords, 0);
    for (std::uint32_t i = 0; i < kWords; ++i) set_word(two, i, static_cast<std::uint16_t>(0x4000 | (i & 0xFFF)));
    set_word(two, kDup1 + 0x98, 0x5555);
    set_word(two, kDup1 + 0x99, 0x6666);
    EXPECT_TRUE(gcdsp_apply_ax(two.data(), two.size(), kDup1, kDup2, kGcDspNoDup3, kOff));
    EXPECT_EQ(get_word(two, kDup3), std::uint16_t(0x4000 | (kDup3 & 0xFFF)));
    EXPECT_TRUE(!gcdsp_apply_ax(two.data(), 32, kDup1, kDup2, kDup3, kOff));  // past the end
}

// The format-dependent insertions: a format load grows seven words and is
// retargeted, an address store gains the OR word between its halves.
void test_ax_format_insertions() {
    constexpr std::uint16_t kDup1 = 0x40, kDup2 = 0xA0, kOff = 0x8F;
    constexpr std::uint32_t kWords = 0x200;
    std::vector<std::uint8_t> program(2 * kWords, 0);
    for (std::uint32_t i = 0; i < kWords; ++i) set_word(program, i, static_cast<std::uint16_t>(0x4000 | (i & 0xFFF)));
    set_word(program, kDup1 + 0x98, 0x5555);
    set_word(program, kDup1 + 0x99, 0x6666);
    // Inside the moved 0x22 words, past the upper half that fills the
    // second copy (so the halves still join where they should): one format
    // load and one address store.
    set_word(program, kDup1 + 0x0A, 0x195E);
    set_word(program, kDup1 + 0x0B, 0x2ED1);
    set_word(program, kDup1 + 0x06, 0x1111);
    set_word(program, kDup1 + 0x07, 0x2ED0);
    EXPECT_TRUE(gcdsp_apply_ax(program.data(), program.size(), kDup1, kDup2, kGcDspNoDup3, kOff));

    // The retargeted load and then the seven format words, exactly once:
    // the move writes the pair first and the format words after it (as the
    // real programs' patched bytes show).
    const std::uint16_t kPrefix[] = {0x195F, 0x2FD1, 0x0340, 0x0003, 0x8100, 0x009E, 0x2000, 0x02CA, 0x1FFE};
    unsigned prefix_hits = 0;
    for (std::uint32_t i = 0; i + 9 <= kWords; ++i) {
        bool hit = true;
        for (int k = 0; k < 9; ++k) hit = hit && get_word(program, i + k) == kPrefix[k];
        if (hit) ++prefix_hits;
    }
    EXPECT_EQ(prefix_hits, 1u);
    // The address store split around the OR word, exactly once.
    unsigned or_hits = 0;
    for (std::uint32_t i = 0; i + 3 <= kWords; ++i) {
        if (get_word(program, i) == 0x1111 && get_word(program, i + 1) == 0x3E00 &&
            get_word(program, i + 2) == 0x2ED0)
            ++or_hits;
    }
    EXPECT_EQ(or_hits, 1u);
}

}  // namespace

int main() {
    test_recognition();
    test_table_values();
    test_simple_rewrite();
    test_ax_rewrite();
    test_ax_format_insertions();
    if (g_failures == 0) {
        std::cout << "ALL GCDSP TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
