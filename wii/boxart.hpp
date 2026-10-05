// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <string>

#include "covers.hpp"

// The boxes on the Home shelf (riftwii/coverart.hpp): each game's spine
// and front from GameTDB's full cover, fetched into
// sd:/riftwii/boxes/<ID>.rwb and read back into a small pool in MEM2.
namespace riftwii::wii {

// True when the game has no stored box and GameTDB was not found without
// one in the last week.
bool BoxWanted(const std::string& game_id);

// Downloads, cuts and stores the game's box on the network's background
// thread, as StartCoverFetch does; one at a time.
bool StartBoxFetch(const std::string& game_id);
const std::string& BoxFetchGame();
bool TakeBoxFetch(std::string& game_id, CoverFetch& got, std::string& error);

// The game's box as an RGB5A3 texture of kBoxWidth x kBoxHeight (the
// spine in its first kBoxSpineWidth columns), or nullptr while there is
// none in memory. Reads at most one box from the card a frame, so a shelf
// fills in over a few frames instead of stopping. GUI thread only.
const u8* BoxTexture(const std::string& game_id);
// Drops what the pool remembers about the game (after a new download).
void ForgetBox(const std::string& game_id);

}  // namespace riftwii::wii
