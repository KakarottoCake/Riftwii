// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace riftwii {

// The colour of the game's case when GameTDB says it is not white (New
// Super Mario Bros. Wii's red, Project M's purple: tools/make_case_colours.py),
// by its whole six-character ID. False for white cases and games it does
// not know.
bool game_case_colour(const std::string& game_id, std::uint8_t (&rgb)[3]);

// The player's own case colours, from sd:/riftwii/case_colors.txt: one
// game a line, "<ID> <rrggbb>" ("SMNE01 ff0000"). A full six-character ID
// is one game; an ID of three to five characters covers every game it
// starts ("SMN" is New Super Mario Bros. Wii in every region). The longest
// match wins. "#" starts a comment.
struct CaseColourOverride {
    std::string id;  // upper case, three to six characters
    std::array<std::uint8_t, 3> rgb{};
};
using CaseColourOverrides = std::vector<CaseColourOverride>;

// Reads the file's lines into `out` (replacing it), skipping any that are
// not an ID and a colour; at most kMaxCaseColourOverrides of them. The
// number of lines skipped.
constexpr std::size_t kMaxCaseColourOverrides = 512;
int parse_case_colour_overrides(const std::string& text, CaseColourOverrides& out);

// Where a game's case colour came from, the first that knows it.
enum class CaseColourSource {
    Player,   // the player's file
    GameTdb,  // GameTDB, for this exact game (its whole six-character ID)
    Console,  // the console's own: white for the Wii, black for the GameCube
};

// The colour of the game's case, as a Wii case on a real shelf: the
// player's choice, then GameTDB's for that exact ID, then the console's.
CaseColourSource resolve_case_colour(const std::string& game_id, bool gamecube, const CaseColourOverrides& overrides,
                                     std::uint8_t (&rgb)[3]);

}  // namespace riftwii
