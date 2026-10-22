// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <string>

#include "covers.hpp"

// GameTDB's 3D covers for Home's 3D covers view (riftwii/coverart.hpp):
// each game's case as a picture, standing turned a little, fetched into
// sd:/riftwii/covers3d/<ID>.rw3 and read back into a small pool in MEM2.
namespace riftwii::wii {

// True when the game has no stored 3D cover and GameTDB was not found
// without one in the last week.
bool Cover3dWanted(const std::string& game_id);
// Forgets that GameTDB had no 3D cover for the game, so the next look asks
// again instead of waiting out the week (Settings > Download missing covers).
void ForgetCover3dMiss(const std::string& game_id);

// Downloads and stores the game's 3D cover on the network's background
// thread, as StartCoverFetch does; one at a time.
bool StartCover3dFetch(const std::string& game_id);
const std::string& Cover3dFetchGame();
bool TakeCover3dFetch(std::string& game_id, CoverFetch& got, std::string& error);

// The game's 3D cover as an RGB5A3 texture of kCover3dWidth x
// kCover3dHeight, or nullptr while there is none in memory. Reads at most
// one from the card a frame. GUI thread only.
const u8* Cover3dTexture(const std::string& game_id);
// Drops what the pool remembers about the game (after a new download).
void ForgetCover3d(const std::string& game_id);

}  // namespace riftwii::wii
