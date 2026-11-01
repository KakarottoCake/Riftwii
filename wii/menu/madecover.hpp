// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <string>

// Covers made up for games GameTDB has none for (or none yet): the theme's
// blank cover (skin::blankCover) with the game's name printed on it, as an
// RGB5A3 texture of kCoverWidth x kCoverHeight (riftwii/coverart.hpp), so
// every view that shows a stored cover can show one of these instead of a
// bare case. They are drawn into the frame buffer before a frame's windows
// and copied out, so the name is set in the menu's own font.
namespace riftwii::wii {

// The made-up cover for `key` (the tile's CoverName), with `title` and
// `id` printed on it, or nullptr until it is made (MakeUpCovers, at the
// start of a coming frame). An empty `id` prints none (an emulator game's
// ID is only its cover key). GUI thread only.
const u8* MadeUpCover(const std::string& key, const std::string& title, const std::string& id);

// The made-up cover already made for `key`, whatever name is on it, or
// nullptr: for a box in flight, which knows only its game.
const u8* MadeUpCoverMade(const std::string& key);

// Draws a made-up cover as skin::DrawRgb5a3 draws a stored one, its
// corners rounded off as a stored cover's are.
void DrawMadeUpCover(const u8* cover, float x, float y, int alpha = 255, float scale = 1.0f);

// Called once a frame, before the windows draw: makes the covers asked
// for since the last frame, a few at a time.
void MakeUpCovers();

}  // namespace riftwii::wii
