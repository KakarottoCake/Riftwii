// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/sysfont.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    v[at] = x >> 24;
    v[at + 1] = x >> 16;
    v[at + 2] = x >> 8;
    v[at + 3] = x;
}

// content.map: a banner content, then the font.
static void test_map() {
    std::vector<std::uint8_t> map(56, 0);
    std::memcpy(map.data(), "00000001", 8);
    std::memcpy(map.data() + 28, "0000000f", 8);
    std::memcpy(map.data() + 36, kWiiFontHash, 20);
    EXPECT_EQ(shared_content_name(map.data(), map.size(), kWiiFontHash), std::string("0000000f"));
    EXPECT_EQ(shared_content_name(map.data(), map.size(), kWiiKoreanFontHash), std::string(""));
    // A cut-off last entry is not read.
    EXPECT_EQ(shared_content_name(map.data(), 55, kWiiFontHash), std::string(""));
    // A name that is not hex (a damaged map) is refused.
    map[28] = '/';
    EXPECT_EQ(shared_content_name(map.data(), map.size(), kWiiFontHash), std::string(""));
}

// The shape the Wii's font content has: the root, one file.
static std::vector<std::uint8_t> archive(const char* file, std::uint32_t data_size) {
    const std::string names = std::string("\0", 1) + file + std::string("\0", 1);
    const std::size_t root = 32, nodes = 2 * 12, data = 96;
    std::vector<std::uint8_t> a(data + data_size, 0);
    put32(a, 0, 0x55AA382D);
    put32(a, 4, root);
    put32(a, 8, static_cast<std::uint32_t>(nodes + names.size()));
    put32(a, 12, data);
    put32(a, root, 0x01000000);
    put32(a, root + 8, 2);
    put32(a, root + 12, 1);  // a file, its name at 1
    put32(a, root + 16, data);
    put32(a, root + 20, data_size);
    std::memcpy(a.data() + root + nodes, names.data(), names.size());
    std::memcpy(a.data() + data, "ttcf", 4);
    return a;
}

static void test_u8() {
    std::size_t at = 0, n = 0;
    std::vector<std::uint8_t> a = archive("WiiNTLG-Regular.ttc", 100);
    EXPECT_TRUE(u8_find_font(a.data(), a.size(), at, n));
    EXPECT_EQ(at, std::size_t(96));
    EXPECT_EQ(n, std::size_t(100));
    // Upper-case extension too.
    a = archive("KOREAN.TTF", 8);
    EXPECT_TRUE(u8_find_font(a.data(), a.size(), at, n));
    // No font in it.
    a = archive("wbf1.brfna", 8);
    EXPECT_FALSE(u8_find_font(a.data(), a.size(), at, n));
    // A file past the end of what was read.
    a = archive("WiiNTLG-Regular.ttc", 100);
    EXPECT_FALSE(u8_find_font(a.data(), a.size() - 1, at, n));
    // Not U8.
    a[0] = 0;
    EXPECT_FALSE(u8_find_font(a.data(), a.size(), at, n));
    // A node count the archive can't hold.
    a = archive("WiiNTLG-Regular.ttc", 100);
    put32(a, 32 + 8, 0x10000000);
    EXPECT_FALSE(u8_find_font(a.data(), a.size(), at, n));
    EXPECT_FALSE(u8_find_font(a.data(), 10, at, n));
}

int main() {
    test_map();
    test_u8();
    if (g_failures == 0) {
        std::cout << "ALL SYSFONT TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
