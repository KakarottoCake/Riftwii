// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// font.ttf beside the running boot.dol (SD or USB) is the entire main
// menu face (M PLUS 1 SemiBold, with rare legacy glyphs retained). It is
// checked and copied into MEM2 before the GUI starts;
// no font bytes are embedded in the DOL. The file is closed immediately.
// FreeType reads the copy throughout the menu, including launches, IOS
// reloads, remounts and updates replacing the file. The next menu start
// reads the updated font. There is no face or buffer to close on unmount.
//
// korean.ttf keeps its separate path. On first use it too is copied into
// MEM2, so Korean text remains drawable during those same windows. Its
// FreeType face, temporary glyph bitmap and missing-glyph textures share
// the existing 384 KiB MEM2 pool, with heap overflow as before. The main
// face's normal FreeType allocations and glyph cache use the heap as before.
namespace riftwii::wii::fullfont {

// Initialize the main face from the card. False: log why, arrange the
// copy-font notice, and let main.cpp try the Wii Menu's NAND font.
bool Load();

bool HasKoreanCharacters();

// Prevent new Korean file reads for the entire unmount or update. Already
// loaded faces and every glyph they contain stay usable. The main face
// only reads its file at startup, before these holds can exist.
class ClosedHold {
public:
    explicit ClosedHold(const char* why);
    ~ClosedHold();
    ClosedHold(const ClosedHold&) = delete;
    ClosedHold& operator=(const ClosedHold&) = delete;
};

// No more card reads after the menu hands the card to the game.
void EndMenu();

// Once per session: tell the player to copy font.ttf from the release zip.
bool TakeMissingNote();

// The old updater already writes this single file beside boot.dol.
std::string FullFontPath();

}  // namespace riftwii::wii::fullfont
