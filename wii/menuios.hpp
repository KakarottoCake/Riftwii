// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

// The IOS RiftWii's own menu runs under. The Homebrew Channel starts it on
// IOS58; a cIOS with an input module (fakemote turns USB DS3/DS4 pads into
// Wii Remotes) only helps while that cIOS is running, so the menu can be
// moved onto a d2x slot at startup. Games then launch under the same slot,
// disc games included, so the pads keep working in the game.
namespace riftwii::wii {

// The saved choice (sd:/riftwii/menu_ios.txt): 0 for the Homebrew
// Channel's IOS, else a cIOS slot.
int LoadMenuIos();
bool SaveMenuIos(int slot);

// 0 followed by every d2x-looking slot from 248 to 251 with a launchable
// title (the same test that gates game reloads).
std::vector<int> MenuIosChoices();

// Called once at startup with the SD card mounted and before video, pads,
// USB or DI are touched: reloads into the saved slot when it is installed,
// taking the card and the session log down and back up around the reload.
// Returns false after a failed reload (the menu stays on the old IOS).
// `fresh` (after a restart, wii/restart.hpp) reloads even into the IOS
// already running, IOS 58 when no slot is saved, so nothing the old run
// left open survives.
bool StartMenuIos(bool sd_mounted, bool fresh = false);

// The cIOS slot the menu is running under, or 0.
int MenuCiosSlot();

// Wii Remote pairings in SYSCONF (BT.DINF) and how many of them are
// fakemote's "Fake Wiimote" entries, which fakemote writes when its IOS
// starts. -1 when SYSCONF cannot be read.
struct PadPairings {
    int registered = -1;
    int fake = 0;
};
PadPairings ReadPadPairings();
std::string DescribePadPairings(const PadPairings& p);

}  // namespace riftwii::wii
