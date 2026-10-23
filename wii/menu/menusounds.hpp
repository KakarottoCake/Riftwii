// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The menu's own press sound and the two HOME Menu sounds are stored in the
// DOL as Ogg Vorbis (vendor-libgui/source/sounds/*.ogg, about a tenth of
// the raw audio they were). Playing one needs plain 48 kHz 16-bit stereo,
// so they are decoded here once, at start, into MEM2: the first click or
// the first HOME press then has nothing left to do but start the voice.
namespace riftwii::wii {

// Decodes button_click, home_open and home_close and points the
// button_click_pcm, home_open_pcm and home_close_pcm of libgui's filelist.h
// at the result. Call it after the audio starts and before any GuiSound is
// made. A sound that can't be decoded stays null and plays nothing; the log
// says which and why.
void DecodeMenuSounds();

}  // namespace riftwii::wii
