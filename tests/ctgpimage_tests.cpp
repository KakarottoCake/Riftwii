// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Linking and hooks of CTGP-R's code image (riftwii/ctgpimage.hpp), on made-up data.
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpimage.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << std::hex << (a) << " != " << (b) << std::dec << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static void rec(CtgpBytes& v, unsigned skip, unsigned type, unsigned section, std::uint32_t addend) {
    v.push_back(skip >> 8); v.push_back(skip & 0xFF); v.push_back(type); v.push_back(section);
    v.push_back(addend >> 24); v.push_back(addend >> 16); v.push_back(addend >> 8); v.push_back(addend);
}
static std::uint32_t get(const CtgpBytes& b, std::size_t at) {
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}
static void set(CtgpBytes& b, std::size_t at, std::uint32_t v) {
    b[at] = v >> 24; b[at + 1] = v >> 16; b[at + 2] = v >> 8; b[at + 3] = v;
}

static void test_link() {
    CtgpBytes boot(0x40, 0), boat(0x40, 0);
    set(boat, 0x10, 0x48000001);   // bl
    set(boat, 0x14, 0x40820000);   // bne
    set(boot, 0x20, 0);
    CtgpBytes beat;
    rec(beat, 0, 0xCA, 1, 0);       // patch BOAT
    rec(beat, 0x10, 10, 1, 0x100);  // bl to BOAT+0x100 from BOAT+0x10: +0xF0
    rec(beat, 4, 11, 1, 0x80);      // bc to BOAT+0x80 from BOAT+0x14: +0x6C
    rec(beat, 4, 1, 0, 0x10);       // word at BOAT+0x18 = BOOT+0x10 (absolute)
    rec(beat, 4, 6, 0, 0x8000);     // ha16 at +0x1c
    rec(beat, 4, 4, 0, 0x8000);     // lo16 at +0x20
    rec(beat, 0, 0xCB, 0, 0);
    std::string error;
    EXPECT_TRUE(ctgp_link(boot, boat, beat.data(), beat.size(), 0, error));
    EXPECT_EQ(get(boat, 0x10), 0x480000F1u);
    EXPECT_EQ(get(boat, 0x14), 0x4082006Cu);
    EXPECT_EQ(get(boat, 0x18), kCtgpBootBase + 0x10);
    // 16-bit relocations patch the halfword at the record's own offset;
    // ha16 of 0x81628000 rounds the high half up
    EXPECT_EQ((boat[0x1C] << 8) | boat[0x1D], 0x8163);
    EXPECT_EQ((boat[0x20] << 8) | boat[0x21], 0x8000);
    // a branch relocation on something that is not a branch
    CtgpBytes b2(0x40, 0), c2(0x40, 0);
    CtgpBytes r2;
    rec(r2, 0, 0xCA, 1, 0);
    rec(r2, 0, 10, 1, 0x100);
    EXPECT_TRUE(!ctgp_link(b2, c2, r2.data(), r2.size(), 0, error));
    // outside the section
    CtgpBytes r3;
    rec(r3, 0, 0xCA, 1, 0);
    rec(r3, 0x100, 1, 0, 0);
    EXPECT_TRUE(!ctgp_link(b2, c2, r3.data(), r3.size(), 0, error));
}

static CtgpRowTable table() {
    CtgpRowTable t;
    t.words.resize(kCtgpRowCount * 3);
    for (std::size_t r = 0; r < kCtgpRowCount; ++r)
        for (unsigned c = 0; c < 3; ++c) t.words[r * 3 + c] = 0x80100000 + 0x100 * r + 0x10000 * c;
    return t;
}

static void test_image() {
    CtgpBytes boot(0x3000, 0), boat(0x1000, 0), beat;
    // hook destinations: BOOT+0x2814+4k hold addresses inside BOAT
    for (std::uint32_t k = 0; k < 0x30; ++k) set(boot, 0x2814 + 4 * k, kCtgpBoatBase + 0x100 + 4 * k);
    rec(beat, 0, 0xCB, 0, 0);
    const CtgpRowTable rows = table();
    CtgpImage img;
    std::string error;
    EXPECT_TRUE(ctgp_build_image(boot, boat, beat, rows, CtgpRegion::P, img, error));
    EXPECT_EQ(img.hooks.size(), (std::size_t)37);
    EXPECT_EQ(img.optional_hooks.size(), (std::size_t)2);
    // hook 0: a plain branch from row 0 (PAL column) to the first destination
    EXPECT_EQ(img.hooks[0].site, rows.row(0, CtgpRegion::P));
    EXPECT_EQ(img.hooks[0].dest, kCtgpBoatBase + 0x100);
    EXPECT_TRUE(!img.hooks[0].link);
    EXPECT_EQ(img.hooks[0].word, 0x48000000u | ((kCtgpBoatBase + 0x100 - rows.row(0, CtgpRegion::P)) & 0x3FFFFFC));
    // hook 28 goes 0x2a0 into its function with a bl
    EXPECT_EQ(img.hooks[28].site, rows.row(28, CtgpRegion::P) + 0x2A0);
    EXPECT_TRUE(img.hooks[28].link);
    // the row 2567 family
    EXPECT_EQ(img.hooks[31].site, rows.row(2567, CtgpRegion::P));
    EXPECT_EQ(img.hooks[32].site, rows.row(2567, CtgpRegion::P) - 0x208);
    EXPECT_EQ(img.hooks[36].site, rows.row(2567, CtgpRegion::P) + 0x1E4);
    // slot tables
    EXPECT_EQ(get(img.boot, 0), rows.row(0, CtgpRegion::P) + 4);
    EXPECT_EQ(get(img.boot, 4 * 27), rows.row(27, CtgpRegion::P) + 4);
    EXPECT_EQ(get(img.boot, 0x70), rows.row(28, CtgpRegion::P) + 0x2A0 + 4);
    EXPECT_EQ(get(img.boot, 0x50), rows.row(2565, CtgpRegion::P));
    EXPECT_EQ(get(img.boot, 0x68), rows.row(2566, CtgpRegion::P));
    EXPECT_EQ(get(img.boot, 0x7C), rows.row(31, CtgpRegion::P));
    EXPECT_EQ(get(img.boot, 0x7C + 4 * 2533), rows.row(2564, CtgpRegion::P));
    // another region picks another column
    CtgpImage j;
    EXPECT_TRUE(ctgp_build_image(boot, boat, beat, rows, CtgpRegion::J, j, error));
    EXPECT_EQ(j.hooks[0].site, rows.row(0, CtgpRegion::J));
    // a hook that cannot reach BOAT
    CtgpRowTable far = rows;
    far.words[0 * 3 + 2] = 0x80000100;
    CtgpBytes boot2 = boot;
    set(boot2, 0x2814, 0xC0000000);
    EXPECT_TRUE(!ctgp_build_image(boot2, boat, beat, far, CtgpRegion::P, j, error));
}

static void test_row_table() {
    CtgpBytes f = {'C', 'R', 'T', '0', 0, 0, 0x0A, 0x0A};  // 2570 rows
    f.resize(8 + 2570 * 12, 0);
    set(f, 8, 0x80001234);
    CtgpRowTable t;
    std::string error;
    EXPECT_TRUE(ctgp_parse_row_table(f.data(), f.size(), t, error));
    EXPECT_EQ(t.rows(), kCtgpRowCount);
    EXPECT_EQ(t.row(0, CtgpRegion::E), 0x80001234u);
    EXPECT_TRUE(t.low.empty());
    // with the 54 extra words
    CtgpBytes g = f;
    g.resize(g.size() + 54 * 4, 0);
    set(g, g.size() - 4, 0x80004444);
    EXPECT_TRUE(ctgp_parse_row_table(g.data(), g.size(), t, error));
    EXPECT_EQ(t.low.size(), (std::size_t)54);
    EXPECT_EQ(t.low[53], 0x80004444u);
    g.pop_back();
    EXPECT_TRUE(!ctgp_parse_row_table(g.data(), g.size(), t, error));
    f[0] = 'X';
    EXPECT_TRUE(!ctgp_parse_row_table(f.data(), f.size(), t, error));
    f[0] = 'C';
    f.pop_back();
    EXPECT_TRUE(!ctgp_parse_row_table(f.data(), f.size(), t, error));
}

int main() {
    test_link();
    test_image();
    test_row_table();
    if (g_failures == 0) std::cout << "ctgpimage tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
