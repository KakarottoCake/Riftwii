// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The themes that come with a release (riftwii-themes.pack, one of its
// GitHub assets), for the in-app update: RiftWii fetches its own
// version's pack once and brings sd:/riftwii/themes up to date with it.
// Made by tools/make_theme_pack.py. Text lines, each file's bytes right
// after its line:
//
//     RIFTWII THEMES 1
//     retire Linen Bookshelf background.png theme.ini
//     file Bookshelf/theme.ini 1234
//     <1234 bytes>
//     end
//
// "retire" names a theme RiftWii no longer comes with, the theme that
// takes its place for a player using it (- for the default look), and the
// files it came with: only those are removed, so what a player added to
// its folder stays.
//
// riftwii-apps.pack has the same layout under "RIFTWII APPS 1": the files
// of sd:/apps the release zip has besides RiftWii's own boot.dol (the
// channel installer), made by tools/make_apps_pack.py.
namespace riftwii {

constexpr const char* kThemePackHeader = "RIFTWII THEMES 1";
constexpr const char* kAppsPackHeader = "RIFTWII APPS 1";

struct ThemePackFile {
    std::string path;  // "Theme/file.png", checked: one folder, plain names
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct RetiredTheme {
    std::string name;
    std::string replacement;  // empty for the default look
    std::vector<std::string> files;
};

struct ThemePack {
    std::vector<ThemePackFile> files;
    std::vector<RetiredTheme> retired;
};

// False (and `error` says why) for anything that is not a whole pack, or
// that names a path outside one theme's folder.
bool parse_theme_pack(const std::uint8_t* data, std::size_t size, ThemePack& out, std::string& error,
                      const char* header = kThemePackHeader);

// A theme's folder name or a file's name as a pack may use them: letters,
// digits, space, '_', '-' and (files only) '.', not starting with a dot.
bool theme_pack_name_ok(const std::string& name, bool file);

}  // namespace riftwii
