// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>
#include <string>

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

int main() {
    test_parse();
    test_url();
    test_key();
    if (g_failures == 0) {
        std::cout << "ALL RIITAG TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
