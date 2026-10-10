// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace riftwii {

// The colour of the game's case when GameTDB says it is not white (New
// Super Mario Bros. Wii's red, Project M's purple: tools/make_case_colours.py),
// by its whole six-character ID. False for white cases and games it does
// not know.
bool game_case_colour(const std::string& game_id, std::uint8_t (&rgb)[3]);

}  // namespace riftwii
