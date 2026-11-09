// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// sd:/riftwii/ctgpr/tables.bin, the CTGP-R constants read at run time
// (riftwii/ctgptables.hpp). Every value here is made up.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgptables.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static void put(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
}

static std::vector<std::uint8_t> made_file() {
    std::vector<std::uint8_t> b(kCtgpTablesSize, 0);
    std::memcpy(b.data(), "CTB0", 4);
    put(b, 0x004, 0x11111111);
    put(b, 0x008, 0x22222222);
    put(b, 0x00C, 0x33333333);
    put(b, 0x010, 0x44444444);
    put(b, 0x014, 0x55555555);
    b[0x018] = 0xA1;              // p's first byte
    b[0x018 + 64] = 0xB2;         // q's
    b[0x018 + 84] = 0xC3;         // g's
    b[0x018 + 148 + 63] = 0xD4;   // y's last
    for (std::uint32_t i = 0; i < 32; ++i) put(b, 0x0EC + 4 * i, 1000 + i);
    put(b, 0x16C, 54);
    b[0x170] = 7;
    b[0x170 + 215] = 9;
    put(b, 0x248, 2);
    b[0x24C] = 42;
    return b;
}

int main() {
    std::string error;
    CtgpTables t;
    const std::vector<std::uint8_t> good = made_file();
    EXPECT_TRUE(ctgp_parse_tables(good.data(), good.size(), t, error));
    EXPECT_TRUE(t.efa_iv.w0 == 0x11111111 && t.efa_iv.w1 == 0x22222222 && t.efa_iv.w3 == 0x33333333);
    EXPECT_TRUE(t.efb_key.w1 == 0x44444444 && t.efb_key.w3 == 0x55555555);
    EXPECT_TRUE(t.list_key.p[0] == 0xA1 && t.list_key.q[0] == 0xB2 && t.list_key.g[0] == 0xC3 && t.list_key.y[63] == 0xD4);
    EXPECT_TRUE(t.course[0] == 1000 && t.course[31] == 1031);
    EXPECT_TRUE(t.layouts[0].groups == 54 && t.layouts[0].tracks[0] == 7 && t.layouts[0].tracks[215] == 9);
    EXPECT_TRUE(t.layouts[1].groups == 2 && t.layouts[1].tracks[0] == 42);

    // refused: wrong magic, wrong size, too many groups, nothing at all
    std::vector<std::uint8_t> bad = good;
    bad[0] = 'X';
    EXPECT_TRUE(!ctgp_parse_tables(bad.data(), bad.size(), t, error));
    EXPECT_TRUE(error.find("tables.bin") != std::string::npos);
    EXPECT_TRUE(!ctgp_parse_tables(good.data(), good.size() - 1, t, error));
    bad = good;
    bad.push_back(0);
    EXPECT_TRUE(!ctgp_parse_tables(bad.data(), bad.size(), t, error));
    bad = good;
    put(bad, 0x248, 55);
    EXPECT_TRUE(!ctgp_parse_tables(bad.data(), bad.size(), t, error));
    EXPECT_TRUE(!ctgp_parse_tables(nullptr, 0, t, error));

    if (g_failures == 0) std::cout << "ctgptables tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
