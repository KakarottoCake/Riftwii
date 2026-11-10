// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// CTGP-R's constants and tables (riftwii/ctgptables.hpp): the package-disk key
// and chunk IV, the jelly key words and the row table are all built into
// RiftWii (the course table and cup layouts are baked into ctgpapply.cpp and
// covered by ctgpapply_tests).
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

int main() {
    test_constants();
    test_embedded_rows();
    if (g_failures == 0) std::cout << "ctgptables tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
