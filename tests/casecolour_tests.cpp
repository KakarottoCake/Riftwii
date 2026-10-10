// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The case colours (riftwii/casecolour.hpp), from GameTDB.
#include <cstring>
#include <iostream>
#include <string>

#include "riftwii/casecolour.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

static const char kColours[] =
#include "../src/casecolours.inc"
    ;

static bool Is(const char* id, int r, int g, int b) {
    std::uint8_t rgb[3] = {1, 2, 3};
    return riftwii::game_case_colour(id, rgb) && rgb[0] == r && rgb[1] == g && rgb[2] == b;
}

int main() {
    // New Super Mario Bros. Wii's red case, in every region, any case of ID.
    EXPECT_TRUE(Is("SMNE01", 0xff, 0, 0));
    EXPECT_TRUE(Is("SMNP01", 0xff, 0, 0));
    EXPECT_TRUE(Is("smnj01", 0xff, 0, 0));
    // A black Japanese case (MadWorld), a mod's own ID (Project M).
    EXPECT_TRUE(Is("RZZJEL", 0, 0, 0));
    EXPECT_TRUE(Is("RSBEPM", 0x41, 0x32, 0x92));
    // White cases, unknown games and IDs that are not six characters.
    std::uint8_t rgb[3];
    EXPECT_FALSE(riftwii::game_case_colour("RMCE01", rgb));
    EXPECT_FALSE(riftwii::game_case_colour("RSBE01", rgb));
    EXPECT_FALSE(riftwii::game_case_colour("ZZZZ99", rgb));
    EXPECT_FALSE(riftwii::game_case_colour("SMNE0", rgb));
    EXPECT_FALSE(riftwii::game_case_colour("", rgb));
    // Twelve characters a game, sorted by ID, lower-case hex.
    const std::size_t n = (sizeof(kColours) - 1) / 12;
    EXPECT_TRUE((sizeof(kColours) - 1) % 12 == 0);
    for (std::size_t i = 1; i < n; ++i) EXPECT_TRUE(std::memcmp(kColours + (i - 1) * 12, kColours + i * 12, 6) < 0);
    for (std::size_t i = 0; i < n; ++i)
        for (int k = 6; k < 12; ++k) EXPECT_TRUE(std::strchr("0123456789abcdef", kColours[i * 12 + k]) != nullptr);
    if (g_failures == 0) std::cout << "casecolour: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
