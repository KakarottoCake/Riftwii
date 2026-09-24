// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <string>

// Box art (riftwii/coverart.hpp) on the Wii: fetched from GameTDB into
// sd:/riftwii/covers/<ID>.rwc, and read back into a fixed pool of
// textures in MEM2 (13 covers, 233 KB, taken once) for drawing.
namespace riftwii::wii {

// True when the game has no stored cover and GameTDB was not found
// without one in the last week.
bool CoverWanted(const std::string& game_id);
// True when the game's cover is on the card.
bool CoverStored(const std::string& game_id);

enum class CoverFetch { Stored, NotFound, Failed };
// Downloads, shrinks and stores the game's cover. NotFound: GameTDB has
// none (remembered for a week); Failed: no network, or the card could
// not be written (`error`).
CoverFetch FetchCover(const std::string& game_id, std::string& error);

// The game's cover as an RGB5A3 texture of kCoverWidth x kCoverHeight,
// or nullptr when there is none. Reads the card the first time it is
// asked for a cover not in the pool. Only from the GUI thread (or with
// it halted).
const u8* CoverTexture(const std::string& game_id);
// Drops what the pool remembers about the game (after a new download).
void ForgetCover(const std::string& game_id);

}  // namespace riftwii::wii
