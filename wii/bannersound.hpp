// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

// A banner's sound while its channel is shown, as the Wii Menu plays it:
// decoded from sound.bin (riftwii/bnrsound.hpp) and played on a voice of
// its own, the menu's music paused meanwhile; a looping one goes round
// from its loop point after the first time through.
namespace riftwii::wii {

// Stops any sound playing, then starts this one. False when it cannot
// be decoded or played (the banner shows silent).
bool BannerSoundStart(const std::vector<std::uint8_t>& sound_bin);
// Once a frame or so: starts the loop when the first time through ends.
void BannerSoundUpdate();
// Stops it and lets the music play on.
void BannerSoundStop();

}  // namespace riftwii::wii
