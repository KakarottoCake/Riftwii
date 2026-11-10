// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// CTGP-R's config.ini options as the in-game code reads them (riftwii/ctgpfields.hpp).
#include <cstdint>
#include <iostream>
#include <set>
#include <string>

#include "riftwii/ctgpfields.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static std::uint32_t word(const std::vector<CtgpWordWrite>& w, std::uint32_t address) {
    for (const auto& x : w) if (x.address == address) return x.value;
    std::cerr << "no word at " << std::hex << address << std::endl;
    g_failures++;
    return 0xDEADBEEF;
}

static void test_table_shape() {
    std::set<std::uint32_t> seen;
    for (const auto& f : ctgp_fields()) {
        EXPECT_TRUE(f.address >= 0x816ff4e0 && f.address < 0x816ffb00);
        EXPECT_EQ(f.address % 4, 0u);
        EXPECT_TRUE(seen.insert(f.address).second);
        if (!f.is_bool) EXPECT_TRUE(f.def >= f.min && f.def <= f.max);
    }
    EXPECT_EQ(ctgp_fields().size(), (std::size_t)26);
}

static void test_defaults() {
    const auto w = ctgp_field_writes("");
    EXPECT_EQ(word(w, 0x816ff9f4), 0u);          // lanmode off
    EXPECT_EQ(word(w, 0x816ffa2c), 12u);         // player_count_offline
    EXPECT_EQ(word(w, 0x816ffa30), 12u);
    EXPECT_EQ(word(w, 0x816ffa90), 1u);          // mii heads acceptable
    EXPECT_EQ(word(w, 0x816ffa98), 1u);          // repeat vote prevention
    EXPECT_EQ(word(w, 0x816ffa9c), 1u);          // shroom glitch
}

static void test_parse() {
    const std::string ini =
        "# Auto generated\r\n[ctgpr]\r\nlanmode = yes\r\nroom_shroom_glitch = no\r\n"
        "player_count_offline = 24\r\nmax_player_count_online=7\r\n"
        "room_repeat_vote_prevention = 2\r\nmax_spectators = 9\r\nunknown_key = 5\r\n"
        "[video]\r\nvideo_mode = -1\r\nconsistent_framerate = Yes\r\nmenu_transition_speed = x\r\n";
    const auto w = ctgp_field_writes(ini);
    EXPECT_EQ(word(w, 0x816ff9f4), 0u);          // lanmode = yes is ignored: no LAN play
    EXPECT_EQ(word(w, 0x816ffa9c), 0u);
    EXPECT_EQ(word(w, 0x816ffa2c), 24u);
    EXPECT_EQ(word(w, 0x816ffa30), 7u);
    EXPECT_EQ(word(w, 0x816ffa98), 2u);
    EXPECT_EQ(word(w, 0x816ffab0), 0u);          // 9 is out of range: default stays
    EXPECT_EQ(word(w, 0x816ffa20), 1u);          // values are case-insensitive
    EXPECT_EQ(word(w, 0x816ffae0), 0u);          // not a number
    // out of range for the player counts (min 2)
    const auto v = ctgp_field_writes("player_count_offline = 1\n");
    EXPECT_EQ(word(v, 0x816ffa2c), 12u);
}

int main() {
    test_table_shape();
    test_defaults();
    test_parse();
    if (g_failures == 0) std::cout << "ctgpfields tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
