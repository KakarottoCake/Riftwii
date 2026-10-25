// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The DOL carries core.ttf: Latin, symbols, kana and the Japanese menu
// text. The full menu font ships as font.ttf beside boot.dol; Korean is
// supplied by korean.ttf in the same folder. Missing characters are tried
// in that order, through FreeType streams that read glyphs on demand.
// FreeTypeGX caches every drawn glyph. Both external faces, their file
// buffers and glyph textures share the existing 384 KiB MEM2 pool; only
// allocations that do not fit use the heap.
namespace riftwii::wii::fullfont {

// With RiftWii's own font in use (not the Wii Menu's, Settings > Menu
// font): characters the core lacks come from the external fonts.
void Enable();

// The Korean font is a separate OFL file, korean.ttf beside boot.dol.
// Both streams share the existing MEM2 pool, glyph cache and close holds.
// Check Hangul coverage without drawing a glyph, for the language choice.
// With the Wii Menu font selected, its own coverage remains authoritative.
bool HasKoreanCharacters();

// While one lives both files stay closed: for the
// whole of every unmount (covers.cpp's dvmUnmountVolume wrap: a launch
// handing the card to the game, an IOS reload, a remount), so the GUI
// thread cannot open either on a card going away, or while an update rewrites
// the fonts (on the network's thread). Afterwards each opens again when a
// character the core lacks needs it, and a missing file is looked for
// again. `why` is for the log line of the close.
class ClosedHold {
public:
    explicit ClosedHold(const char* why);
    ~ClosedHold();
    ClosedHold(const ClosedHold&) = delete;
    ClosedHold& operator=(const ClosedHold&) = delete;
};

// The menu has closed for a launch, which hands the card to the game:
// closed, and not opened again in this run.
void EndMenu();

// True once a session, the first time the full font was found missing or
// unreadable: the menu then tells the player to copy it.
bool TakeMissingNote();

// Where the full font is read from and an update writes it: font.ttf next
// to the running boot.dol, as its plugins/ folder is.
std::string FullFontPath();

}  // namespace riftwii::wii::fullfont
