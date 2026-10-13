// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A menu theme's own sound effects (docs/THEMES.md): hover.wav, click.wav,
// home_open.wav and home_close.wav in the theme's folder replace RiftWii's
// pointer tick, click and HOME Menu sounds wherever the menu plays them.
// A WAV or AIFF, 16-bit, mono or stereo, 4 000 to 48 000 Hz (the banner
// sound decoder, riftwii/bnrsound.hpp); a file that is missing, too big or
// unreadable keeps RiftWii's own sound, and session.log says why.
namespace riftwii::wii {

// Reads the chosen theme's sounds into MEM2 (once a start: the menu
// restarts for another theme) and makes every GuiSound of a replaced
// sound play the theme's. After LoadMenuTheme, before the first GuiSound.
void LoadThemeSounds();

}  // namespace riftwii::wii
