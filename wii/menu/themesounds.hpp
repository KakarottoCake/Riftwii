// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A menu theme's own sound effects (docs/THEMES.md): hover.wav, click.wav,
// home_open.wav and home_close.wav in the theme's folder replace RiftWii's
// pointer tick, click and HOME Menu sounds wherever the menu plays them.
// A WAV or AIFF, 16-bit, mono or stereo, 4 000 to 48 000 Hz (the banner
// sound decoder, riftwii/bnrsound.hpp); a file that is missing, too big or
// unreadable uses the selected Wii Menu source or RiftWii's own sound.
// Invalid files get a reason in session.log.
namespace riftwii::wii {

// Reads the chosen theme's sounds into MEM2 (once a start: the menu
// restarts for another theme) and resolves each GuiSound at playback,
// trying the theme before the selected Wii Menu source. Call after
// LoadMenuTheme, before the first GuiSound.
void LoadThemeSounds();
bool ThemeClickSoundFound();

}  // namespace riftwii::wii
