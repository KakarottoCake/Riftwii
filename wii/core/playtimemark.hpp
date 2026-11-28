// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The per-launch note for play time (#642, riftwii/playtime.hpp's
// PlayMarker): written when a tile is launched, read once at the next start.
namespace riftwii::wii {

// A launch of `key` now: writes the note (none for a GameCube or emulator
// tile, and none with Play history off; the old one goes either way). A
// launch that fails back to the menu leaves its note: the next start finds
// no record that began after it, or credits one the game really has.
void PlayTimeLaunch(const std::string& key, bool gamecube = false);
// The launch runs in this emulated NAND folder (before d2x takes the card
// over); adds it to the note, if there is one.
void PlayTimeLaunchNand(const std::string& folder);

}  // namespace riftwii::wii
