// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The CTGP-R Channel's memory map for the game (riftwii/ctgpmmu.hpp).
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpmmu.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << std::hex << (a) << " != " << (b) << std::dec << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

// 54 made-up region words in the row table's order (E, J, P in threes).
// The memory map reads four triples: the main executable's code start (21)
// and end (24), the page CTGP copies (27) and the end of the code after it
// (30). The others only need to exist.
static std::vector<std::uint32_t> made_low() {
    std::vector<std::uint32_t> low(54, 0x80100000);
    for (int r = 0; r < 3; ++r) {
        low[21 + r] = 0x80004000;
        low[24 + r] = 0x80300000;
        low[27 + r] = 0x80600000 + 0x1000 * static_cast<std::uint32_t>(r);
        low[30 + r] = 0x80900000;
    }
    return low;
}
static const std::vector<std::uint32_t> kLow = made_low();

static void expect_map(const CtgpMmu& m, std::uint32_t ea, std::uint32_t want_pa, bool want_ro, int line) {
    std::uint32_t pa = 0;
    bool ro = false;
    if (!ctgp_mmu_translate(m, ea, pa, ro)) {
        std::cerr << "FAILED: 0x" << std::hex << ea << std::dec << " not mapped (line " << line << ")" << std::endl;
        g_failures++;
        return;
    }
    if (pa != want_pa || ro != want_ro) {
        std::cerr << "FAILED: 0x" << std::hex << ea << " -> 0x" << pa << (ro ? " ro" : " rw") << ", want 0x" << want_pa
                  << (want_ro ? " ro" : " rw") << std::dec << " (line " << line << ")" << std::endl;
        g_failures++;
    }
}
#define MAP(m, ea, pa, ro) expect_map(m, ea, pa, ro, __LINE__)

static void test_pal_map() {
    CtgpMmu m;
    std::string error;
    EXPECT_TRUE(ctgp_build_mmu(kLow, CtgpRegion::P, m, error));
    EXPECT_EQ(m.htab.size(), (std::size_t)kCtgpHtabSize);
    EXPECT_EQ(m.sdr1, 0x01600001u);
    EXPECT_EQ(m.sr[0], 0x80000000u);
    EXPECT_EQ(m.sr[15], 0x80000000u);
    EXPECT_EQ(m.sr[8] & 0xF0000000u, 0x10000000u);
    // Low memory: the core's copy at physical 0x01700000.
    MAP(m, 0x800000C0, 0x017000C0, false);
    MAP(m, 0x80001234, 0x01701234, false);
    MAP(m, 0x80002000, 0x00002000, false);
    MAP(m, 0x80003140, 0x00003140, false);
    // The game's code read-only, the page CTGP copies remapped.
    MAP(m, 0x80004000, 0x00004000, true);
    MAP(m, 0x802FFFFC, 0x002FFFFC, true);
    MAP(m, 0x80300000, 0x00300000, false);
    MAP(m, 0x80602010, kCtgpPageCopy + 0x10, false);
    MAP(m, 0x80603000, 0x00603000, true);
    MAP(m, 0x808FFFFC, 0x008FFFFC, true);
    MAP(m, 0x80900000, 0x00900000, false);
    // 0x80C00000-0x813FFFFF are the BATs' (DBAT0 and the core's DBAT7).
    std::uint32_t pa = 0;
    bool ro = false;
    EXPECT_TRUE(!ctgp_mmu_translate(m, 0x80C00000, pa, ro));
    EXPECT_TRUE(!ctgp_mmu_translate(m, 0x813FF000, pa, ro));
    MAP(m, 0x81400000, 0x01400000, false);
    // CTGP: BOOT data and the BOAT writable; the table itself unmapped past its first page.
    MAP(m, 0x81700000, 0x01700000, false);
    MAP(m, 0x81760000, 0x01760000, false);
    MAP(m, 0x81600000, 0x01600000, false);
    EXPECT_TRUE(!ctgp_mmu_translate(m, 0x81601000, pa, ro));
    MAP(m, 0x817FF000, 0x017FF000, false);
    // The uncached view, hardware, the locked cache.
    MAP(m, 0xC00000C0, 0x017000C0, false);
    MAP(m, 0xC0004000, 0x00004000, true);
    MAP(m, 0xC0C00000, 0x00C00000, false);
    MAP(m, 0xCC003004, 0x0C003004, false);
    MAP(m, 0xCD006000, 0x0D006000, false);
    MAP(m, 0xE0001000, 0xE0001000, false);
    EXPECT_TRUE(!ctgp_mmu_translate(m, 0x90000000, pa, ro));  // MEM2: BATs
    EXPECT_EQ(m.dbat7u, 0x800003FFu);
    EXPECT_EQ(m.core_dbat7u, 0x8100007Fu);
    EXPECT_EQ(m.entries, (std::size_t)(6144 - 31 - 2048 + 6144 - 31 + 11 + 4));
}

static void test_regions_and_refusal() {
    CtgpMmu m;
    std::string error;
    EXPECT_TRUE(ctgp_build_mmu(kLow, CtgpRegion::E, m, error));
    MAP(m, 0x80600000, kCtgpPageCopy, false);
    MAP(m, 0x80602000, 0x00602000, true);
    EXPECT_TRUE(ctgp_build_mmu(kLow, CtgpRegion::J, m, error));
    MAP(m, 0x80601000, kCtgpPageCopy, false);
    EXPECT_TRUE(!ctgp_build_mmu(std::vector<std::uint32_t>(10, 0), CtgpRegion::P, m, error));
}

int main() {
    test_pal_map();
    test_regions_and_refusal();
    if (g_failures == 0) std::cout << "ctgpmmu tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
