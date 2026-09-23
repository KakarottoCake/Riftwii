// SPDX-License-Identifier: GPL-3.0-or-later
// The menu's software canvas: anti-aliased shapes, straight-alpha
// blending and the GX RGBA8 tile layout the Wii textures use.
#include "riftwii/canvas.hpp"

#include <cstdlib>
#include <iostream>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << +(a) << " != " << +(b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_NEAR(a, b, tol) do { if (std::abs(int(a) - int(b)) > (tol)) { std::cerr << "FAILED: " #a " ~ " #b " (" << +(a) << " vs " << +(b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using riftwii::Canvas;
using riftwii::Rgba;
using riftwii::rgba;

static void test_shapes() {
    Canvas c(40, 20);
    EXPECT_EQ(c.at(0, 0).a, 0);
    c.rounded_rect(4, 4, 32, 12, 6, rgba(0xFFFFFF));
    EXPECT_EQ(c.at(20, 10).a, 255);   // inside
    EXPECT_EQ(c.at(20, 10).r, 255);
    EXPECT_EQ(c.at(1, 10).a, 0);      // outside
    EXPECT_EQ(c.at(4, 4).a, 0);       // the corner is cut away
    const int edge = c.at(4, 10).a;   // a pixel whose centre sits half a pixel inside the edge
    EXPECT_TRUE(edge > 200);
    EXPECT_EQ(c.at(3, 10).a, 0);

    // A border leaves the middle alone.
    Canvas b(40, 20);
    b.rounded_border(4, 4, 32, 12, 6, 2, rgba(0x2FB6E9));
    EXPECT_EQ(b.at(20, 10).a, 0);
    EXPECT_EQ(b.at(20, 4).a, 255);
    EXPECT_EQ(b.at(20, 4).b, 0xE9);

    // Shadows fade outwards.
    Canvas s(60, 60);
    s.shadow(20, 20, 20, 20, 4, 10, rgba(0x000000, 200));
    EXPECT_EQ(s.at(30, 30).a, 200);
    EXPECT_TRUE(s.at(30, 42).a > s.at(30, 46).a);
    EXPECT_EQ(s.at(30, 55).a, 0);

    Canvas r(20, 20);
    r.ring(10, 10, 8, 2, rgba(0xFF0000));
    EXPECT_EQ(r.at(10, 10).a, 0);
    EXPECT_EQ(r.at(10, 3).a, 255);
    r.circle(10, 10, 3, rgba(0x00FF00));
    EXPECT_EQ(r.at(10, 10).g, 255);

    Canvas l(20, 20);
    l.line(2, 10, 18, 10, 4, rgba(0x0000FF));
    EXPECT_EQ(l.at(10, 10).b, 255);
    EXPECT_EQ(l.at(10, 15).a, 0);

    Canvas g(8, 20);
    g.rounded_gradient(0, 0, 8, 20, 0, rgba(0x000000), rgba(0xFFFFFF));
    EXPECT_TRUE(g.at(4, 1).r < 30);
    EXPECT_TRUE(g.at(4, 18).r > 225);
}

static void test_curves() {
    std::vector<float> top(16, 8.0f);
    Canvas c(16, 16);
    c.area_below(top, rgba(0xFFFFFF));
    EXPECT_EQ(c.at(5, 4).a, 0);
    EXPECT_EQ(c.at(5, 12).a, 255);
    Canvas l(16, 16);
    l.curve(top, 2, rgba(0x2FB6E9));
    EXPECT_TRUE(l.at(5, 7).a > 200 || l.at(5, 8).a > 200);
    EXPECT_EQ(l.at(5, 13).a, 0);
}

static void test_blending() {
    Canvas c(4, 4);
    c.fill(rgba(0xFF0000));
    c.rect(0, 0, 4, 4, rgba(0x0000FF, 128));  // half blue over opaque red
    EXPECT_EQ(c.at(1, 1).a, 255);
    EXPECT_NEAR(c.at(1, 1).r, 127, 1);
    EXPECT_NEAR(c.at(1, 1).b, 128, 1);

    // Straight alpha over nothing keeps the colour, not a darkened one.
    Canvas t(4, 4);
    t.rect(0, 0, 4, 4, rgba(0xFFFFFF, 64));
    EXPECT_EQ(t.at(2, 2).r, 255);
    EXPECT_NEAR(t.at(2, 2).a, 64, 1);
}

static void test_gx_tiles() {
    Canvas c(8, 4);
    c.rect(5, 2, 1, 1, Rgba{10, 20, 30, 40});  // tile 1, row 2, column 1
    const std::vector<std::uint8_t> tex = riftwii::to_gx_rgba8(c);
    EXPECT_EQ(tex.size(), std::size_t(8 * 4 * 4));
    // Tile 1 starts at byte 64; AR pairs, then GB pairs 32 bytes later.
    const std::size_t pixel = 2 * 4 + 1;
    EXPECT_EQ(tex[64 + pixel * 2], 40);
    EXPECT_EQ(tex[64 + pixel * 2 + 1], 10);
    EXPECT_EQ(tex[64 + 32 + pixel * 2], 20);
    EXPECT_EQ(tex[64 + 32 + pixel * 2 + 1], 30);
    EXPECT_EQ(tex[0], 0);
    EXPECT_TRUE(riftwii::to_gx_rgba8(Canvas(6, 4)).empty());  // not a multiple of 4
}

int main() {
    test_shapes();
    test_curves();
    test_blending();
    test_gx_tiles();
    if (g_failures == 0) {
        std::cout << "ALL CANVAS TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
