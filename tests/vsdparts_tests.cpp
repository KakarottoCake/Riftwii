// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/vsdparts.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static void test_names() {
    EXPECT_EQ(vsd_part_name("rex.raw", 1), std::string("rex.raw.001"));
    EXPECT_EQ(vsd_part_name("rex.raw", 12), std::string("rex.raw.012"));
    EXPECT_EQ(vsd_split_image("rex.raw.001"), std::string("rex.raw"));
    EXPECT_EQ(vsd_split_image("REX.RAW.001"), std::string("REX.RAW"));
    EXPECT_EQ(vsd_split_image("rex.raw.002"), std::string(""));
    EXPECT_EQ(vsd_split_image("rex.raw"), std::string(""));
    EXPECT_EQ(vsd_split_image(".001"), std::string(""));
}

static void test_join() {
    std::vector<Fragment> out;
    std::uint64_t bytes = 0;
    std::string why;
    // One whole file: its last cluster runs past the size and is cut.
    EXPECT_TRUE(join_vsd_parts({{512 * 10, {{100, 8}, {200, 8}}}}, out, bytes, why));
    EXPECT_EQ(out.size(), std::size_t(2));
    if (out.size() == 2) {
        EXPECT_EQ(out[1].sector, 200u);
        EXPECT_EQ(out[1].sector_count, 2u);
    }
    EXPECT_EQ(bytes, 5120u);
    // Two parts back to back on the drive become one piece.
    EXPECT_TRUE(join_vsd_parts({{512 * 4, {{100, 4}}}, {512 * 4, {{104, 8}}}}, out, bytes, why));
    EXPECT_EQ(out.size(), std::size_t(1));
    if (out.size() == 1) EXPECT_EQ(out[0].sector_count, 8u);
    EXPECT_EQ(bytes, 4096u);
    // Three parts apart, the middle one in two pieces.
    EXPECT_TRUE(join_vsd_parts({{1024, {{10, 2}}}, {2048, {{50, 1}, {60, 3}}}, {512, {{90, 4}}}}, out, bytes, why));
    EXPECT_EQ(out.size(), std::size_t(4));
    if (out.size() == 4) {
        EXPECT_EQ(out[2].sector, 60u);
        EXPECT_EQ(out[2].sector_count, 3u);
        EXPECT_EQ(out[3].sector_count, 1u);
    }
    EXPECT_EQ(bytes, 3584u);
    // A part that isn't whole 512-byte sectors.
    EXPECT_FALSE(join_vsd_parts({{512, {{1, 1}}}, {1000, {{2, 2}}}}, out, bytes, why));
    EXPECT_TRUE(why.find("part 2") != std::string::npos);
    // Fragments shorter than the part.
    EXPECT_FALSE(join_vsd_parts({{512 * 4, {{1, 3}}}}, out, bytes, why));
    EXPECT_TRUE(why.find("the image") != std::string::npos);
}

int main() {
    test_names();
    test_join();
    if (g_failures == 0) {
        std::cout << "ALL VSDPARTS TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
