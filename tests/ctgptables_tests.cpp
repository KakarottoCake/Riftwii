// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// CTGP-R's constants and tables (riftwii/ctgptables.hpp): the package-disk key
// and chunk IV and the jelly key words are built into RiftWii; the course and
// cup tables are read at run time from CTGP-R's own CTGP.CRS and .cup files, so
// the synthetic files here stand in for them.
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpimage.hpp"
#include "riftwii/ctgptables.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static void put_be32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
}

static void test_constants() {
    // The package-disk key is four PPC instruction words, big endian.
    const auto key = ctgp_blob_key();
    EXPECT_EQ((unsigned)key[0], 0x90u);
    EXPECT_EQ((unsigned)key[3], 0x04u);
    EXPECT_EQ((unsigned)key[15], 0x20u);
    const CtgpChunkIv iv = ctgp_blob_chunk_iv();
    EXPECT_EQ(iv.w0, 0x80630004u);
    EXPECT_EQ(iv.w1, 0x90830004u);
    EXPECT_EQ(iv.w3, 0x4e800020u);
    // jelly.bin's key reuses the package key's 2nd and 4th words.
    const CtgpEfbKeyWords words = ctgp_jelly_key_words();
    EXPECT_EQ(words.w1, 0x90a30008u);
    EXPECT_EQ(words.w3, 0x4e800020u);
}

static void test_embedded_rows() {
    std::size_t size = 0;
    const std::uint8_t* data = ctgp_embedded_row_table(size);
    EXPECT_TRUE(data != nullptr && size > 8);
    EXPECT_TRUE(std::memcmp(data, "CRT0", 4) == 0);
    CtgpRowTable rows;
    std::string error;
    EXPECT_TRUE(ctgp_parse_row_table(data, size, rows, error));
    EXPECT_EQ(rows.rows(), (std::size_t)kCtgpRowCount);
}

// A CRS file: "CRS\0", three header words, then entries of {index, course-id,
// name, NUL}. Course word i is entry i's course-id byte.
static std::vector<std::uint8_t> made_crs() {
    std::vector<std::uint8_t> b(0x10, 0);
    std::memcpy(b.data(), "CRS\0", 4);
    for (std::size_t i = 0; i < kCtgpCourseWords; ++i) {
        b.push_back(static_cast<std::uint8_t>(i));         // index
        b.push_back(static_cast<std::uint8_t>(0x80 + i));  // course id
        b.push_back('t');
        b.push_back(0);
    }
    return b;
}

static void test_course_from_crs() {
    std::string error;
    std::array<std::uint32_t, kCtgpCourseWords> course{};
    const std::vector<std::uint8_t> crs = made_crs();
    EXPECT_TRUE(ctgp_course_from_crs(crs.data(), crs.size(), course, error));
    EXPECT_EQ(course[0], 0x80u);
    EXPECT_EQ(course[31], 0x80u + 31u);
    // refused: wrong magic, truncated before 32 entries, nothing at all
    std::vector<std::uint8_t> bad = crs;
    bad[0] = 'X';
    EXPECT_TRUE(!ctgp_course_from_crs(bad.data(), bad.size(), course, error));
    EXPECT_TRUE(!ctgp_course_from_crs(crs.data(), 0x10 + 8, course, error));
    EXPECT_TRUE(!ctgp_course_from_crs(nullptr, 0, course, error));
}

// A .cup file: "CUP2", a group count, then count * 4 course-id bytes.
static std::vector<std::uint8_t> made_cup(std::uint32_t groups) {
    std::vector<std::uint8_t> b(8 + groups * 4, 0);
    std::memcpy(b.data(), "CUP2", 4);
    put_be32(b, 4, groups);
    for (std::uint32_t i = 0; i < groups * 4; ++i) b[8 + i] = static_cast<std::uint8_t>(1 + i);
    return b;
}

static void test_layout_from_cup() {
    std::string error;
    CtgpLayout layout;
    const std::vector<std::uint8_t> cup = made_cup(5);
    EXPECT_TRUE(ctgp_layout_from_cup(cup.data(), cup.size(), layout, error));
    EXPECT_EQ(layout.groups, 5u);
    EXPECT_EQ((unsigned)layout.tracks[0], 1u);
    EXPECT_EQ((unsigned)layout.tracks[19], 20u);
    // the full 54 groups fit
    const std::vector<std::uint8_t> full = made_cup(kCtgpLayoutMaxGroups);
    EXPECT_TRUE(ctgp_layout_from_cup(full.data(), full.size(), layout, error));
    EXPECT_EQ(layout.groups, kCtgpLayoutMaxGroups);
    // refused: wrong magic, too many groups, a count the file is too short for
    std::vector<std::uint8_t> bad = cup;
    bad[0] = 'X';
    EXPECT_TRUE(!ctgp_layout_from_cup(bad.data(), bad.size(), layout, error));
    std::vector<std::uint8_t> toomany(8, 0);
    std::memcpy(toomany.data(), "CUP2", 4);
    put_be32(toomany, 4, kCtgpLayoutMaxGroups + 1);
    EXPECT_TRUE(!ctgp_layout_from_cup(toomany.data(), toomany.size(), layout, error));
    std::vector<std::uint8_t> shortcup(8, 0);
    std::memcpy(shortcup.data(), "CUP2", 4);
    put_be32(shortcup, 4, 3);  // claims 3 groups but has no track bytes
    EXPECT_TRUE(!ctgp_layout_from_cup(shortcup.data(), shortcup.size(), layout, error));
}

int main() {
    test_constants();
    test_embedded_rows();
    test_course_from_crs();
    test_layout_from_cup();
    if (g_failures == 0) std::cout << "ctgptables tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
