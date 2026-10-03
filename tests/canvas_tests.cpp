// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The menu's software canvas: anti-aliased shapes, straight-alpha
// blending and the GX RGBA8 tile layout the Wii textures use.
#include "riftwii/canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

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

// The distance to a rounded rectangle (negative inside), worked out here
// the plain way for the checks below.
static float ref_distance(float px, float py, float x, float y, float w, float h, float radius) {
    const float hw = w * 0.5f, hh = h * 0.5f;
    radius = std::min(radius, std::min(hw, hh));
    const float qx = std::fabs(px - (x + hw)) - (hw - radius);
    const float qy = std::fabs(py - (y + hh)) - (hh - radius);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

// The menu's shortcuts draw what the long way does.
static void test_shortcuts() {
    // Shapes on an empty canvas: every pixel as its distance says, the
    // parts the shortcuts skip included (odd sizes, small and large
    // corners, thin and thick borders).
    struct Box { float x, y, w, h, radius, thickness; };
    const Box boxes[] = {{3.3f, 2.7f, 51.4f, 23.2f, 3.0f, 1.5f}, {4, 4, 60, 30, 14, 3},
                         {2.5f, 5, 40, 40, 20, 2.5f}, {6, 3, 44, 12, 0, 4}};
    for (const Box& bx : boxes) {
        Canvas fill(72, 50), border(72, 50), shade(72, 50);
        fill.rounded_rect(bx.x, bx.y, bx.w, bx.h, bx.radius, rgba(0xFFFFFF));
        border.rounded_border(bx.x, bx.y, bx.w, bx.h, bx.radius, bx.thickness, rgba(0xFFFFFF));
        shade.shadow(bx.x, bx.y, bx.w, bx.h, bx.radius, 4, rgba(0x000000, 255));
        int bad = 0;
        for (int y = 0; y < 50; ++y)
            for (int x = 0; x < 72; ++x) {
                const float d = ref_distance(x + 0.5f, y + 0.5f, bx.x, bx.y, bx.w, bx.h, bx.radius);
                const int fillA = static_cast<int>(clamp01(0.5f - d) * 255.0f + 0.5f);
                const int borderA =
                    static_cast<int>(clamp01(0.5f - d) * clamp01(d + bx.thickness + 0.5f) * 255.0f + 0.5f);
                const float t = d <= 0.0f ? 1.0f : clamp01(1.0f - d / 4.0f);
                const int shadeA = static_cast<int>(t * t * 255.0f + 0.5f);
                if (std::abs(fill.at(x, y).a - fillA) > 1) ++bad;
                if (std::abs(border.at(x, y).a - borderA) > 1) ++bad;
                if (std::abs(shade.at(x, y).a - shadeA) > 1) ++bad;
            }
        EXPECT_EQ(bad, 0);
    }

    // A hollow shadow under a card: the card over it looks the same as
    // over a full shadow, and the shadow's inside was never touched.
    Canvas full(90, 60), hollow(90, 60);
    full.shadow(10, 12, 70, 40, 14, 4, rgba(0x000000, 40));
    hollow.shadow(10, 12, 70, 40, 14, 4, rgba(0x000000, 40), 3.0f);
    EXPECT_EQ(hollow.at(45, 32).a, 0);
    EXPECT_EQ(full.at(45, 32).a, 40);
    full.rounded_rect(10, 10, 70, 40, 14, rgba(0xFFFFFF));
    hollow.rounded_rect(10, 10, 70, 40, 14, rgba(0xFFFFFF));
    int differ = 0;
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < 90; ++x) {
            const Rgba a = full.at(x, y), b = hollow.at(x, y);
            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) ++differ;
        }
    EXPECT_EQ(differ, 0);

    // The menu bar's shade as a band: the bar over it looks the same.
    std::vector<float> top(64), shade(64);
    for (int x = 0; x < 64; ++x) {
        top[x] = 20.0f + 8.0f * std::sin(x * 0.1f);
        shade[x] = top[x] - 3.0f;
    }
    Canvas whole(64, 48), band(64, 48);
    whole.area_below(shade, rgba(0x000000, 18));
    band.area_below(shade, rgba(0x000000, 18), 5.0f);
    EXPECT_EQ(band.at(10, 45).a, 0);
    whole.area_below(top, rgba(0xF7F7F9));
    band.area_below(top, rgba(0xF7F7F9));
    int changed = 0;
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x) {
            const Rgba a = whole.at(x, y), b = band.at(x, y);
            if (a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a) ++changed;
        }
    EXPECT_EQ(changed, 0);

    // Stripes in one pass: the lines they stand for, give or take a step
    // of rounding. The lines end at y = 0 in round caps (6 across); the
    // stripes have none, so the top rows are left out (the banner draws
    // its top 8 off screen).
    Canvas lines(120, 48), stripes(120, 48);
    for (int k = -60; k < 200; k += 28) lines.line(static_cast<float>(k), 60, k + 60.0f, 0, 12, rgba(0xFFFFFF, 20));
    stripes.diagonal_stripes(28, 12, rgba(0xFFFFFF, 20));
    int worst = 0;
    for (int y = 7; y < 48; ++y)
        for (int x = 0; x < 120; ++x) worst = std::max(worst, std::abs(lines.at(x, y).a - stripes.at(x, y).a));
    EXPECT_TRUE(worst <= 1);
    EXPECT_TRUE(stripes.at(13, 14).a > 15);  // on a line (x + y + 1 = 28)
    EXPECT_EQ(stripes.at(27, 14).a, 0);      // between two
}

int main() {
    test_shapes();
    test_shortcuts();
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
