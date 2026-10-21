// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

// The menu's four sound effects: the pointer tick, the click and the HOME
// Menu's opening and closing. They ship zstd-compressed in the start-up
// stash (Makefile.wii, wii/core/stash.hpp) rather than linked into the DOL,
// whose end is limited (docs/MEMORY.md), and are unpacked once into MEM2,
// where the DSP reads them. The names are the ones libwiigui's filelist.h
// gave the linked files, so every GuiSound keeps its line.
extern const u8* button_over_pcm;
extern u32 button_over_pcm_size;
extern const u8* button_click_pcm;
extern u32 button_click_pcm_size;
extern const u8* home_open_pcm;
extern u32 home_open_pcm_size;
extern const u8* home_close_pcm;
extern u32 home_close_pcm_size;

namespace riftwii::wii {

// Unpacks the four sounds; before the first GuiSound and LoadThemeSounds.
// A sound that cannot be unpacked (no stash: riftwii.elf started directly,
// or MEM2 full) plays silence, and session.log says which.
void UnpackMenuSounds();

}  // namespace riftwii::wii
