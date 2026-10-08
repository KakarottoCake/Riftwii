// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "riftwii/brfnt.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using Bytes = std::vector<std::uint8_t>;

static void put32(Bytes& v, std::size_t at, std::uint32_t x) {
    v[at] = x >> 24;
    v[at + 1] = x >> 16;
    v[at + 2] = x >> 8;
    v[at + 3] = x;
}
static void put16(Bytes& v, std::size_t at, unsigned x) {
    v[at] = static_cast<std::uint8_t>(x >> 8);
    v[at + 1] = static_cast<std::uint8_t>(x);
}

static void test_huffman() {
    // Two symbols: 'A' on bit 0, 'B' on bit 1. "ABBA" is 0110.
    const Bytes packed = {0x28, 4, 0, 0, /* tree size */ 1, /* root */ 0xC0, 'A', 'B', 0x00, 0x00, 0x00, 0x60};
    std::uint8_t out[4] = {};
    EXPECT_TRUE(huff8_decode(packed.data(), packed.size(), out, 4));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(out), 4), std::string("ABBA"));
    // Not type 0x28, and bits that run out.
    Bytes other = packed;
    other[0] = 0x10;
    EXPECT_FALSE(huff8_decode(other.data(), other.size(), out, 4));
    std::uint8_t more[40] = {};
    EXPECT_FALSE(huff8_decode(packed.data(), packed.size(), more, sizeof more));
}

// A font of two glyphs on one 8x8 I4 sheet, cells 4 wide and 8 high: 'A'
// (glyph 0, all ink, left 1, width 4, advance 5) and 'B' (glyph 1, empty,
// width 0, advance 3). Baseline at row 6.
static Bytes tiny_font() {
    const std::size_t finf = 16, tglp = finf + 32, cwdh = tglp + 32, cmap = cwdh + 24, sheet = cmap + 24;
    Bytes f(sheet + 32, 0);
    put32(f, 0, 0x52464E54);
    put32(f, 4, 0xFEFF0104);
    put32(f, 8, static_cast<std::uint32_t>(f.size()));
    put16(f, 12, 16);
    put16(f, 14, 4);
    put32(f, finf, 0x46494E46);
    put32(f, finf + 4, 32);
    put32(f, tglp, 0x54474C50);
    put32(f, tglp + 4, 32);
    f[tglp + 8] = 3;   // cell 4 wide
    f[tglp + 9] = 7;   // 8 high
    f[tglp + 10] = 6;  // baseline
    put32(f, tglp + 12, 32);
    put16(f, tglp + 16, 1);
    put16(f, tglp + 18, 0);  // I4
    put16(f, tglp + 20, 2);
    put16(f, tglp + 22, 1);
    put16(f, tglp + 24, 8);
    put16(f, tglp + 26, 8);
    put32(f, tglp + 28, static_cast<std::uint32_t>(sheet));
    put32(f, cwdh, 0x43574448);
    put32(f, cwdh + 4, 24);
    put16(f, cwdh + 8, 0);
    put16(f, cwdh + 10, 1);
    f[cwdh + 16] = 1, f[cwdh + 17] = 4, f[cwdh + 18] = 5;
    f[cwdh + 19] = 0, f[cwdh + 20] = 0, f[cwdh + 21] = 3;
    put32(f, cmap, 0x434D4150);
    put32(f, cmap + 4, 24);
    put16(f, cmap + 8, 'A');
    put16(f, cmap + 10, 'B');
    put16(f, cmap + 12, 0);  // a run
    put16(f, cmap + 20, 0);
    // One 8x8 tile: each row 4 bytes, two texels each; the left cell (x 0-3) inked.
    for (int y = 0; y < 8; ++y) f[sheet + y * 4] = 0xFF, f[sheet + y * 4 + 1] = 0xFF;
    return f;
}

static void test_font() {
    const Bytes f = tiny_font();
    std::size_t sheet_bytes = 0;
    EXPECT_TRUE(BitmapFont::sheet_size(f.data(), f.size(), sheet_bytes));
    EXPECT_EQ(sheet_bytes, 32u);
    Bytes slot(32);
    BitmapFont font;
    std::string error;
    EXPECT_TRUE(font.load(f.data(), f.size(), slot.data(), slot.size(), 1, error));
    EXPECT_EQ(error, std::string());
    EXPECT_EQ(font.characters(), 2u);

    BitmapFont::Glyph g;
    // 65 is twice the font's size: everything doubles.
    EXPECT_TRUE(font.render('A', 65, g));
    EXPECT_EQ(g.width, 8);
    EXPECT_EQ(g.rows, 16);
    EXPECT_EQ(g.left, 2);
    EXPECT_EQ(g.advance, 10);
    EXPECT_EQ(g.top, 12);
    bool all_ink = !g.pixels.empty();
    for (std::uint8_t p : g.pixels) all_ink = all_ink && p == 255;
    EXPECT_TRUE(all_ink);
    // Half size: the glyph starts half a pixel in (left 1 at 0.49 scale), so
    // its first column is half covered and the next one full (the box filter
    // keeps the weight).
    EXPECT_TRUE(font.render('A', 16, g));
    EXPECT_EQ(g.left, 0);
    EXPECT_EQ(g.width, 3);
    EXPECT_TRUE(g.pixels[0] > 100 && g.pixels[0] < 160);
    EXPECT_EQ(g.pixels[g.width + 1], 255);  // row 1 (row 0 starts a little above the glyph)
    // A space: nothing to draw, an advance all the same.
    EXPECT_TRUE(font.render('B', 65, g));
    EXPECT_EQ(g.width, 0);
    EXPECT_EQ(g.advance, 6);
    // Not in the font: FreeType's turn.
    EXPECT_FALSE(font.render('C', 16, g));
    EXPECT_FALSE(font.render(0x1F600, 16, g));

    // No slot, or one too small: refused.
    BitmapFont small;
    EXPECT_FALSE(small.load(f.data(), f.size(), slot.data(), 16, 1, error));
    // Broken files are refused, not read past.
    Bytes cut = f;
    put32(cut, 16 + 4, 4000);
    BitmapFont bad;
    EXPECT_FALSE(bad.load(cut.data(), cut.size(), slot.data(), slot.size(), 1, error));
    Bytes wrong = f;
    wrong[0] = 'X';
    EXPECT_FALSE(bad.load(wrong.data(), wrong.size(), slot.data(), slot.size(), 1, error));
}

static void test_u8() {
    // Root, a folder, a file "wbf1.brfna" of 3 bytes at 0x40.
    Bytes a(0x48, 0);
    put32(a, 0, 0x55AA382D);
    put32(a, 4, 0x20);
    a[0x20] = 1;
    put32(a, 0x28, 3);  // three nodes
    put32(a, 0x2C, 0x01000001);
    put32(a, 0x38, 0x00000004);  // file, its name at 4
    put32(a, 0x3C, 0x40);
    put32(a, 0x40, 3);
    // The names start after the three nodes, at 0x44.
    a.resize(0x60, 0);
    const char names[] = "\0d\0\0wbf1.brfna";
    std::memcpy(a.data() + 0x44, names, sizeof names);
    // The file's data lives elsewhere: at 0x58, 3 bytes.
    put32(a, 0x3C, 0x58);
    put32(a, 0x40, 3);
    std::size_t at = 0, len = 0;
    EXPECT_TRUE(u8_find_file(a.data(), a.size(), "wbf1.brfna", at, len));
    EXPECT_EQ(at, 0x58u);
    EXPECT_EQ(len, 3u);
    EXPECT_FALSE(u8_find_file(a.data(), a.size(), "wbf2.brfna", at, len));
    EXPECT_FALSE(u8_find_file(a.data(), a.size(), "wbf1", at, len));
}

// With RIFTWII_WII_BITMAP_FONT set to the NAND content (a Wii's
// /shared1/<name>.app with kWiiBitmapFontHash): the real font.
static void test_real_font() {
    const char* path = std::getenv("RIFTWII_WII_BITMAP_FONT");
    if (!path) return;
    std::ifstream in(path, std::ios::binary);
    const Bytes arc((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t at = 0, len = 0;
    EXPECT_TRUE(u8_find_file(arc.data(), arc.size(), "wbf1.brfna", at, len));
    std::size_t sheet_bytes = 0;
    EXPECT_TRUE(BitmapFont::sheet_size(arc.data() + at, len, sheet_bytes));
    Bytes slots(sheet_bytes * 4);
    BitmapFont font;
    std::string error;
    EXPECT_TRUE(font.load(arc.data() + at, len, slots.data(), sheet_bytes, 4, error));
    EXPECT_EQ(error, std::string());
    BitmapFont::Glyph g;
    for (const char* s = "RiftWii 0123 gjpqy"; *s; ++s) EXPECT_TRUE(font.render(static_cast<unsigned char>(*s), 18, g));
    EXPECT_TRUE(font.render(0x3042, 18, g));  // hiragana a
    std::cout << "real font: " << font.characters() << " characters" << std::endl;
}

int main() {
    test_huffman();
    test_font();
    test_u8();
    test_real_font();
    if (g_failures == 0) std::cout << "brfnt tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
