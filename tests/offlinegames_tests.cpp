// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The games with no online play (riftwii/offlinegames.hpp), from GameTDB.
#include <cstring>
#include <iostream>
#include <string>

#include "riftwii/offlinegames.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

static const char kCodes[] =
#include "../src/offlinegames.inc"
    ;

int main() {
    using riftwii::game_has_no_online;
    // No online play: Wii Sports Resort (WiiConnect24's Message Board
    // only), the Super Mario Galaxies, New Super Mario Bros. Wii.
    EXPECT_TRUE(game_has_no_online("RZTE01"));
    EXPECT_TRUE(game_has_no_online("RZTP01"));
    EXPECT_TRUE(game_has_no_online("rztj01"));
    EXPECT_TRUE(game_has_no_online("RMGP01"));
    EXPECT_TRUE(game_has_no_online("SB4E01"));
    EXPECT_TRUE(game_has_no_online("SMNP01"));
    // Online: Mario Kart Wii, Brawl, Boom Blox ("online" alone), Wii Music
    // (scores), Metroid Prime Trilogy.
    EXPECT_FALSE(game_has_no_online("RMCE01"));
    EXPECT_FALSE(game_has_no_online("RMCP01"));
    EXPECT_FALSE(game_has_no_online("RSBE01"));
    EXPECT_FALSE(game_has_no_online("RBKE69"));
    EXPECT_FALSE(game_has_no_online("R64E01"));
    EXPECT_FALSE(game_has_no_online("R3ME01"));
    // Unknown and too short: not said to be offline.
    EXPECT_FALSE(game_has_no_online("ZZZZ99"));
    EXPECT_FALSE(game_has_no_online("RZT"));
    EXPECT_FALSE(game_has_no_online(""));
    // The table is sorted, four characters a code, for the binary search.
    const std::size_t n = (sizeof(kCodes) - 1) / 4;
    EXPECT_TRUE((sizeof(kCodes) - 1) % 4 == 0);
    for (std::size_t i = 1; i < n; ++i) EXPECT_TRUE(std::memcmp(kCodes + (i - 1) * 4, kCodes + i * 4, 4) < 0);
    if (g_failures == 0) std::cout << "offlinegames: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
