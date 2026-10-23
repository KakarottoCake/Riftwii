// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The menu font in two parts (issue #242, item 4). The DOL's start-up stash
// carries the core (wii/font/core.ttf: Latin, symbols, kana, full-width
// forms and every character of the translations); the whole font
// (wii/font/rounded.ttf, 1.6 MB) ships as font.ttf next to boot.dol. A
// character the core lacks (a kanji in a Japanese game name) is drawn from
// that file: opened the first time one is needed, through a FreeType stream
// that reads only the bytes of the glyphs drawn, never the whole file.
// FreeTypeGX caches each glyph drawn, so each is read from the card once.
//
// Memory: FreeType's allocations for this font (its face, stream frames and
// glyph slot) and the drawn glyphs' textures come from a pool in MEM2
// (skin::Mem2Alloc), taken the first time the file is found; the heap's
// only when the pool is full. The file's stdio buffer is in that pool too.
namespace riftwii::wii::fullfont {

// With RiftWii's own font in use (not the Wii Menu's, Settings > Menu
// font): characters the core lacks come from the full font from now on.
void Enable();

// Closes the file, so the card can be unmounted or font.ttf rewritten
// (an update); the next character the core lacks opens it again. Every
// unmount calls it (covers.cpp's dvmUnmountVolume wrap).
void Close();

// While one lives the file stays closed and is not opened (an update
// rewriting font.ttf, on the network's thread); afterwards it is opened
// again when a character needs it, and a missing file is looked for again.
class RewriteHold {
public:
    RewriteHold();
    ~RewriteHold();
    RewriteHold(const RewriteHold&) = delete;
    RewriteHold& operator=(const RewriteHold&) = delete;
};

// The menu has closed for a launch, which hands the card to the game:
// closed, and not opened again in this run.
void EndMenu();

// True once a session, the first time the file was found missing or
// unreadable: the menu then tells the player to copy it.
bool TakeMissingNote();

// Where the full font is read from and an update writes it: font.ttf next
// to the running boot.dol, as its plugins/ folder is.
std::string FullFontPath();

}  // namespace riftwii::wii::fullfont
