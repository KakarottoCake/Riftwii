// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The menu's background music: an OGG Vorbis file on the SD card,
// sd:/riftwii/music.ogg (the player's own) or else
// sd:/apps/riftwii/music.ogg (the one the release ships), looped while
// the menu is open. menu_music = off in settings.txt (the Settings row)
// keeps it quiet.
namespace riftwii::wii {

// Starts it when the setting is on and a file is there. The file is read
// once, into MEM2, and kept for the menu's life.
void MenuMusicStart();
// Stops it; before audio is shut down and before any launch.
void MenuMusicStop();
// Whether a music file was found (the Settings row's note says so).
bool MenuMusicFound();

}  // namespace riftwii::wii
