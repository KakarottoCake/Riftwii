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

namespace table {
#include "../src/data/offlinegames.inc"
}  // namespace table

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
    // A region with no bit is looked up whole: Sports Island 3 in Europe (Z).
    EXPECT_TRUE(game_has_no_online("SEMZ01"));
    // A known game in a region it is not listed in.
    EXPECT_FALSE(game_has_no_online("RZTX01"));
    // Both tables are sorted, four bytes an entry, for the binary searches:
    // three characters and a byte of region bits, then whole codes.
    using table::kOtherCodes;
    using table::kPrefixes;
    EXPECT_TRUE((sizeof(kPrefixes) - 1) % 4 == 0);
    EXPECT_TRUE((sizeof(kOtherCodes) - 1) % 4 == 0);
    for (std::size_t i = 1; i < (sizeof(kPrefixes) - 1) / 4; ++i)
        EXPECT_TRUE(std::memcmp(kPrefixes + (i - 1) * 4, kPrefixes + i * 4, 3) < 0);
    for (std::size_t i = 1; i < (sizeof(kOtherCodes) - 1) / 4; ++i)
        EXPECT_TRUE(std::memcmp(kOtherCodes + (i - 1) * 4, kOtherCodes + i * 4, 4) < 0);
    if (g_failures == 0) std::cout << "offlinegames: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
