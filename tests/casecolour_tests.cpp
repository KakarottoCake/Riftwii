// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The case colours (riftwii/casecolour.hpp): GameTDB's, the series', the
// consoles' and the player's own.
#include <cstring>
#include <iostream>
#include <string>

#include "riftwii/casecolour.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))

static const char kColours[] =
#include "../src/art/casecolours.inc"
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

    // The player's file: comments, blank lines, "=" or tabs between, any
    // case; lines that are not an ID and a colour are skipped and counted.
    riftwii::CaseColourOverrides overrides;
    const int skipped = riftwii::parse_case_colour_overrides(
        "# my cases\n"
        "\n"
        "rmce01 00ff00\r\n"
        "RMC=0x0000ff  # every Mario Kart Wii\n"
        "SMNE01\t112233\n"
        "RS 123456\n"
        "RSBE01 12345\n"
        "RSBE01 gg0000\n"
        "RSBE01 ff0000 extra\n"
        "R$BE01 ff0000\n",
        overrides);
    EXPECT_TRUE(skipped == 5);
    EXPECT_TRUE(overrides.size() == 3);
    EXPECT_TRUE(overrides.size() == 3 && overrides[0].id == "RMCE01" && overrides[1].id == "RMC");
    // The longest matching line wins over the shorter one and over GameTDB.
    const auto resolves = [&](const char* id, bool gamecube, riftwii::CaseColourSource source, int r, int g, int b) {
        std::uint8_t got[3] = {1, 2, 3};
        return riftwii::resolve_case_colour(id, gamecube, overrides, got) == source && got[0] == r && got[1] == g &&
               got[2] == b;
    };
    EXPECT_TRUE(resolves("RMCE01", false, riftwii::CaseColourSource::Player, 0, 0xff, 0));
    EXPECT_TRUE(resolves("RMCP01", false, riftwii::CaseColourSource::Player, 0, 0, 0xff));
    EXPECT_TRUE(resolves("smne01", false, riftwii::CaseColourSource::Player, 0x11, 0x22, 0x33));
    // Without the file: GameTDB, then the series, then the console's.
    const riftwii::CaseColourOverrides none;
    std::uint8_t got[3];
    EXPECT_TRUE(riftwii::resolve_case_colour("RZZJEL", false, none, got) == riftwii::CaseColourSource::GameTdb);
    EXPECT_TRUE(got[0] == 0 && got[1] == 0 && got[2] == 0);
    // A New Super Mario Bros. Wii mod with its own maker code GameTDB
    // doesn't list is still red; a listed one keeps its own colour.
    EXPECT_TRUE(riftwii::resolve_case_colour("SMNEZZ", false, none, got) == riftwii::CaseColourSource::Series);
    EXPECT_TRUE(got[0] == 0xff && got[1] == 0 && got[2] == 0);
    EXPECT_TRUE(riftwii::resolve_case_colour("SMNE40", false, none, got) == riftwii::CaseColourSource::GameTdb);
    EXPECT_TRUE(got[0] == 0 && got[1] == 0x78 && got[2] == 0xff);
    EXPECT_TRUE(riftwii::resolve_case_colour("RMCE01", false, none, got) == riftwii::CaseColourSource::Console);
    EXPECT_TRUE(got[0] == 0xff && got[1] == 0xff && got[2] == 0xff);
    EXPECT_TRUE(riftwii::resolve_case_colour("GZLE01", true, none, got) == riftwii::CaseColourSource::Console);
    EXPECT_TRUE(got[0] == 0 && got[1] == 0 && got[2] == 0);
    EXPECT_TRUE(riftwii::resolve_case_colour("", false, overrides, got) == riftwii::CaseColourSource::Console);
    // The file holds at most kMaxCaseColourOverrides lines.
    std::string many;
    for (std::size_t i = 0; i < riftwii::kMaxCaseColourOverrides + 3; ++i) many += "RMCE01 000000\n";
    EXPECT_TRUE(riftwii::parse_case_colour_overrides(many, overrides) == 3);
    EXPECT_TRUE(overrides.size() == riftwii::kMaxCaseColourOverrides);

    if (g_failures == 0) std::cout << "casecolour: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
