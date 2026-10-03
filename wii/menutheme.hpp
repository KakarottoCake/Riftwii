// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/theme.hpp"

// The menu's theme on the Wii (docs/THEMES.md): the folder Settings names
// (theme = <folder>) in sd:/riftwii/themes, read once before the menu's
// art is painted (wii/skin.cpp). Not menutheme's job: drawing.
namespace riftwii::wii {

// Reads sd:/riftwii/themes/<Settings().theme>/theme.ini; the default look
// when the setting is "default" or the folder has none. Notes go to the log.
void LoadMenuTheme();
const Theme& MenuTheme();
// The theme's folder with a trailing "/", or "" for the default look.
const std::string& MenuThemeDir();

struct ThemeEntry {
    std::string folder;  // what settings.txt stores
    std::string name;    // what Settings shows
};
// The themes on the card, by name; Default is not in the list.
std::vector<ThemeEntry> ListMenuThemes();

// The theme's <name>.png as RGBA rows, when it has one of exactly w x h
// pixels. Anything else (none, unreadable, another size) is false; all
// but "none" are logged.
bool LoadThemeImage(const char* name, int w, int h, std::vector<std::uint8_t>& rgba);

// The theme's music.ogg path, or "" when the theme has none.
std::string MenuThemeMusic();

}  // namespace riftwii::wii
