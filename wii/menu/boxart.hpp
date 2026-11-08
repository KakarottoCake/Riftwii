// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <string>

#include "covers.hpp"

// The boxes on the Home shelf (riftwii/coverart.hpp): each game's spine
// and front from GameTDB's full cover, fetched into
// sd:/riftwii/boxes/<ID>.rw2 and read back into a small pool in MEM2.
namespace riftwii::wii {

// True when the game has no stored box and GameTDB was not found without
// one in the last week.
bool BoxWanted(const std::string& game_id);
// Forgets that GameTDB had no box for the game, so the next look asks
// again instead of waiting out the week (Settings > Download missing covers).
void ForgetBoxMiss(const std::string& game_id);

// Downloads, cuts and stores the game's box on the network's background
// thread, as StartCoverFetch does; one at a time.
bool StartBoxFetch(const std::string& game_id);
const std::string& BoxFetchGame();
// For a mod tile's box being downloaded: the GameTDB entry it is the
// picture of, as matched when the download started ("" for a game's).
const std::string& BoxFetchEntry();
bool TakeBoxFetch(std::string& game_id, CoverFetch& got, std::string& error);

// The game's box as an RGB5A3 texture of kBoxWidth x kBoxHeight (the
// spine in its first kBoxSpineWidth columns), or nullptr while there is
// none in memory. Reads at most one box from the card a frame, so a shelf
// fills in over a few frames instead of stopping. GUI thread only.
const u8* BoxTexture(const std::string& game_id);
// Drops what the pool remembers about the game (after a new download).
void ForgetBox(const std::string& game_id);
// Drops everything the pool remembers, pictures and "not on the card"
// alike: the SD card was put back and may be another one. GUI halted.
void ForgetAllBoxes();
// The full box a Home tile shows, or nullptr when its flat cover `front`
// (CoverTexture(cover_name)) is shown instead or no box is in memory yet.
// `cover_name` is the tile's (CoverName: a mod tile's key), `game_id` its
// game's. For a mod tile, in order: the player's own PNG for the tile, the
// mod's own box from GameTDB, the mod's own flat cover, then the game's
// box; for a game, its own PNG or chosen second set, then its box.
// `tile_box`, when given: set when the box is the mod tile's own.
const u8* TileBoxTexture(const std::string& cover_name, const std::string& game_id, const u8* front,
                         bool* tile_box = nullptr);

}  // namespace riftwii::wii
