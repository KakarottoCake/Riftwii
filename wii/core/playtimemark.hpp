// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The per-launch note for play time (#642, riftwii/playtime.hpp's
// PlayMarker): written when a tile is launched, read once at the next start.
namespace riftwii::wii {

// A launch of `key` now: writes the note (none for an emulator tile, and
// none with Play history off, which also removes an old one).
void PlayTimeLaunch(const std::string& key);
// The launch runs in this emulated NAND folder (before d2x takes the card
// over); adds it to the note, if there is one.
void PlayTimeLaunchNand(const std::string& folder);

}  // namespace riftwii::wii
