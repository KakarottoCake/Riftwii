// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/riitag.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

// The file USB Loader GX writes (Wiinnertag::CreateExample), and a second
// server after it.
static void test_parse() {
    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Tag URL=\"https://tag.rc24.xyz/wii?game={ID6}&amp;key={KEY}\" Key=\"1234567890\" />\n"
        "<Tag URL=\"https://riitag.t0g3pii.de/wii?game={ID6}&amp;key={KEY}\" Key=\"abc\" />\n"
        "<Tag URL=\"ftp://example.com/{ID6}\" Key=\"x\" />\n"
        "<Tag URL=\"https://example.com/{ID6}\" />\n";
    const std::vector<RiiTagEntry> tags = parse_wiinnertag(xml);
    EXPECT_EQ(tags.size(), std::size_t(2));
    if (tags.size() == 2) {
        EXPECT_EQ(tags[0].url, std::string("https://tag.rc24.xyz/wii?game={ID6}&key={KEY}"));
        EXPECT_EQ(tags[0].key, std::string("1234567890"));
        EXPECT_EQ(tags[1].key, std::string("abc"));
    }
    EXPECT_TRUE(parse_wiinnertag("").empty());
    EXPECT_TRUE(parse_wiinnertag("<Tag URL=").empty());
}

static void test_url() {
    const RiiTagEntry def{kRiiTagUrl, "my key/1"};
    EXPECT_EQ(riitag_url(def, "RMCE01"), std::string("https://riitag.t0g3pii.de/wii?game=RMCE01&key=my%20key%2F1"));
    EXPECT_EQ(riitag_url(def, "HAXX"), std::string("https://riitag.t0g3pii.de/wii?game=HAXX&key=my%20key%2F1"));
    EXPECT_EQ(riitag_url(def, "RMC"), std::string(""));
    EXPECT_EQ(riitag_url(def, "RMCE0&"), std::string(""));
    const RiiTagEntry twice{"http://x/{ID6}/{ID6}?k={KEY}", "k"};
    EXPECT_EQ(riitag_url(twice, "SB4E01"), std::string("http://x/SB4E01/SB4E01?k=k"));
}

static void test_key() {
    EXPECT_TRUE(valid_riitag_key("a1B2c3"));
    EXPECT_FALSE(valid_riitag_key(""));
    EXPECT_FALSE(valid_riitag_key("has space"));
    EXPECT_FALSE(valid_riitag_key(std::string(129, 'a')));
}

static void test_id() {
    EXPECT_TRUE(valid_riitag_id("1202176528943681576"));
    EXPECT_TRUE(valid_riitag_id("123456789012345"));
    EXPECT_FALSE(valid_riitag_id(""));
    EXPECT_FALSE(valid_riitag_id("12345678901234"));          // too short for a Discord ID
    EXPECT_FALSE(valid_riitag_id("123456789012345678901"));   // too long
    EXPECT_FALSE(valid_riitag_id("12345678901234567a"));
    EXPECT_FALSE(valid_riitag_id("../12345678901234567"));
    EXPECT_EQ(riitag_image_url("1202176528943681576"),
              std::string("https://riitag.t0g3pii.de/1202176528943681576/tag.max.png"));
    EXPECT_EQ(riitag_image_url("x"), std::string(""));
}

static void test_fit() {
    // 4 x 2 opaque red halved into 2 x 1: still red, still opaque.
    std::vector<std::uint8_t> red(4 * 2 * 4);
    for (std::size_t i = 0; i < red.size(); i += 4) {
        red[i] = 255;
        red[i + 3] = 255;
    }
    std::vector<std::uint8_t> out(2 * 1 * 4, 7);
    fit_rgba_into(red.data(), 4, 2, out.data(), 2, 1);
    EXPECT_EQ(int(out[0]), 255);
    EXPECT_EQ(int(out[1]), 0);
    EXPECT_EQ(int(out[3]), 255);
    EXPECT_EQ(int(out[7]), 255);

    // A transparent black pixel beside an opaque white one: white at half
    // alpha, not grey.
    const std::uint8_t pair[] = {0, 0, 0, 0, 255, 255, 255, 255};
    std::uint8_t one[4] = {};
    fit_rgba_into(pair, 2, 1, one, 1, 1);
    EXPECT_EQ(int(one[0]), 255);
    EXPECT_EQ(int(one[3]), 127);

    // The menu's box has tag.max.png's shape, so the tag is never stretched.
    EXPECT_EQ(kRiiTagShownWidth * 450, kRiiTagShownHeight * 1200);
    // 1200 x 450 into the menu's box fills it edge to edge (same shape),
    // and a square one is centred with transparent sides.
    std::vector<std::uint8_t> tag(1200u * 450u * 4u, 255);
    std::vector<std::uint8_t> shown(kRiiTagShownWidth * kRiiTagShownHeight * 4);
    fit_rgba_into(tag.data(), 1200, 450, shown.data(), kRiiTagShownWidth, kRiiTagShownHeight);
    EXPECT_EQ(int(shown[3]), 255);
    EXPECT_EQ(int(shown[shown.size() - 1]), 255);
    std::vector<std::uint8_t> square(8 * 8 * 4, 255);
    std::vector<std::uint8_t> boxed(16 * 8 * 4, 9);
    fit_rgba_into(square.data(), 8, 8, boxed.data(), 16, 8);
    EXPECT_EQ(int(boxed[3]), 0);                    // left edge: transparent
    EXPECT_EQ(int(boxed[8 * 4 + 3]), 255);          // first row, middle: the picture
}

int main() {
    test_parse();
    test_url();
    test_key();
    test_id();
    test_fit();
    if (g_failures == 0) {
        std::cout << "ALL RIITAG TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
