// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/modart.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static void test_paths() {
    std::vector<std::string> p = mod_art_paths("sd:/riivolution/Newer.xml", "", "");
    EXPECT_EQ(p.size(), std::size_t(1));
    if (!p.empty()) EXPECT_EQ(p[0], std::string("sd:/riivolution/Newer.png"));
    p = mod_art_paths("usb:/riivolution/SMG 63.v1.3.xml", "", "");
    if (!p.empty()) EXPECT_EQ(p[0], std::string("usb:/riivolution/SMG 63.v1.3.png"));
    // A build in an image: the image's picture, whatever the code file's path.
    p = mod_art_paths("vsd:/codes", "vsd:/codes/RSBE01.gct", "sd:/riftwii/pm.raw");
    EXPECT_EQ(p.size(), std::size_t(1));
    if (!p.empty()) EXPECT_EQ(p[0], std::string("sd:/riftwii/pm.png"));
    // A build on the card: its folder, then the ones above.
    p = mod_art_paths("sd:/Brawl Minus/codes", "sd:/Brawl Minus/codes/RSBE01.gct", "");
    EXPECT_EQ(p.size(), std::size_t(2));
    if (p.size() == 2) {
        EXPECT_EQ(p[0], std::string("sd:/Brawl Minus/codes/cover.png"));
        EXPECT_EQ(p[1], std::string("sd:/Brawl Minus/cover.png"));
    }
    p = mod_art_paths("", "sd:/codes/RSBE01.gct", "");
    EXPECT_EQ(p.size(), std::size_t(1));
    EXPECT_TRUE(mod_art_paths("", "", "").empty());
}

static void test_fit() {
    // A wide 4x2 picture in a 2x4 box: 2x1, in the middle rows.
    std::vector<std::uint8_t> wide(4 * 2 * 4, 255);
    std::vector<std::uint8_t> box = fit_art(wide.data(), 4, 2, 2, 4);
    EXPECT_EQ(box.size(), std::size_t(2 * 4 * 4));
    const auto alpha = [&](int x, int y) { return static_cast<int>(box[(y * 2 + x) * 4 + 3]); };
    EXPECT_EQ(alpha(0, 0), 0);
    EXPECT_EQ(alpha(0, 1), 255);
    EXPECT_EQ(alpha(1, 1), 255);
    EXPECT_EQ(alpha(0, 2), 0);
    EXPECT_EQ(alpha(0, 3), 0);
    // A tall 2x8 one in a 4x4 box: 1x4, the full height, centred across.
    std::vector<std::uint8_t> tall(2 * 8 * 4, 255);
    box = fit_art(tall.data(), 2, 8, 4, 4);
    EXPECT_EQ(static_cast<int>(box[(0 * 4 + 0) * 4 + 3]), 0);
    EXPECT_EQ(static_cast<int>(box[(0 * 4 + 1) * 4 + 3]), 255);
    EXPECT_EQ(static_cast<int>(box[(3 * 4 + 1) * 4 + 3]), 255);
    EXPECT_EQ(static_cast<int>(box[(3 * 4 + 2) * 4 + 3]), 0);
}

static void test_fit_inside() {
    // A wide 128x48 icon in a 100x90 tile inside: full width, centred down.
    FitRect r = fit_inside(128, 48, 10, 20, 100, 90);
    EXPECT_TRUE(r.x >= 10.0f - 0.001f && r.x + r.w <= 110.0f + 0.001f);
    EXPECT_TRUE(r.y >= 20.0f - 0.001f && r.y + r.h <= 110.0f + 0.001f);
    EXPECT_TRUE(r.w / r.h > 2.66f && r.w / r.h < 2.67f);
    EXPECT_TRUE(r.y - 20.0f > 0.0f && r.y - 20.0f - (110.0f - r.y - r.h) < 0.001f);
    // A tall icon is fitted by height; a zero area gives nothing.
    r = fit_inside(48, 128, 0, 0, 100, 90);
    EXPECT_TRUE(r.h <= 90.001f && r.w <= 100.0f);
    EXPECT_TRUE(fit_inside(128, 48, 0, 0, 0, 90).scale == 0.0f);
}

int main() {
    test_fit_inside();
    test_paths();
    test_fit();
    if (g_failures == 0) {
        std::cout << "ALL MODART TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
