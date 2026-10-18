// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gclaunch.hpp"
#include "riftwii/gcoverlay.hpp"

namespace riftwii::wii {

// Starts a GameCube game in Wii mode with RiftWii's own code: the game is
// loaded from its image on the SD card, its DVDLow* functions are replaced
// by runtime/gc (served from the card's sectors), and RiftWii jumps to it.
// No disc drive. `pack`, when given, is the packs' plan: their files are
// served from its window past the image's end (riftwii/gcoverlay.hpp).
// Returns only on failure.
bool LaunchGcNative(const GcImage& image, const GcOverlayPlan* pack, std::string& error);

// Where holding HOME in a GameCube game, and HOME or A on its crash
// screen, go, as Settings > Return to says: on "riftwii", what started
// RiftWii (the Homebrew Channel, or the RiftWii channel when it is
// installed), else the Wii Menu; None when none of them has a ticket.
enum class GcWayBack { None, Channel, WiiMenu, HomebrewChannel };
GcWayBack FindGcWayBack();

// The crash screen, made by the menu in its own font and language: the
// card and the strip as runtime/gc/gc_crash.h lays them out
// (GC_CRASH_SCREEN_BYTES). The next GameCube launch shows it if the game
// crashes; without one a crash turns the screen plain blue. `way_back`:
// the hint says HOME or A goes back (a way back was found), false when it
// says to turn the Wii off.
void SetGcCrashScreen(std::vector<std::uint8_t> screen, bool way_back);

}  // namespace riftwii::wii
