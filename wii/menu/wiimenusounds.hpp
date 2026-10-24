// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <cstddef>

namespace riftwii::wii {

enum class MenuSoundEvent { Hover, Click, Tile, Back, Popup, PageNext, PagePrevious, Start, HomeOpen, HomeClose, Count };

// Called on the menu thread, before GUI construction or with the GUI halted.
// Each resource is attempted once per session and kept in memory only.
void LoadWiiMenuSounds();
// The theme resolver calls this after trying the theme's replacement.
void SubstituteWiiMenuSound(const u8*& sound, s32& length, s32& rate);
// Context-specific events. Uses the theme's click (or RiftWii's) as fallback.
void PlayMenuSound(MenuSoundEvent event);
std::size_t WiiMenuSoundBytes();

bool WiiMenuMusicFound();
bool WiiMenuMusicStart();
// Consumes a producer failure on the menu/GUI thread, allowing Ogg fallback.
bool WiiMenuMusicFailed();
void WiiMenuMusicStop();
void WiiMenuMusicPause(bool pause);
void WiiMenuMusicVolume(int volume);

}  // namespace riftwii::wii
