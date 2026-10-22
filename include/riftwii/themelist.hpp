// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The list of downloadable themes behind Settings > Theme > Get more themes
// (wii/menu/themestore.hpp): themes.json on riftwii.github.io, schema 1,
//
//   {"schema":1,"commit":"...","themes":[{"folder","name","author",
//    "license","size","sha256","files","unpacked_size","has_music",
//    "zip","preview"}]}
//
// It comes from the internet, so it is read as untrusted: its size, its
// theme count and the length of every string are bounded, and a theme that
// breaks a rule (a folder name that is not one plain name, an address off
// riftwii.github.io, a size over the caps) is left out, never trusted.
// The records are fixed-size so the Wii keeps the whole list in one block
// of MEM2 instead of one heap piece per string.
namespace riftwii {

constexpr const char* kThemeSiteUrl = "https://riftwii.github.io/";
constexpr const char* kThemeListUrl = "https://riftwii.github.io/themes/themes.json";

constexpr std::size_t kThemeListMaxBytes = 256u << 10;
constexpr std::size_t kThemeListMaxThemes = 500;
constexpr std::uint32_t kThemeZipMaxBytes = 16u << 20;
constexpr std::size_t kThemePreviewMaxBytes = 256u << 10;
constexpr std::uint32_t kThemeMaxFiles = 64;
constexpr std::uint32_t kThemeMaxUnpackedBytes = 24u << 20;

struct ThemeListing {
    char folder[48];     // one plain folder name (theme_folder_ok)
    char name[64];       // for the menu: printable, no control characters
    char author[64];
    char license[32];
    char sha256[65];     // of the zip, 64 lower-case hex digits
    char zip[200];       // https://riftwii.github.io/...
    char preview[200];
    // No initializers, so a block of these is plain memory a caller zeroes.
    std::uint32_t size;           // the zip's bytes
    std::uint32_t files;
    std::uint32_t unpacked_size;
    bool has_music;
};

// Whether `folder` may be a theme's folder under sd:/riftwii/themes: one
// plain name (themepack.hpp's theme_pack_name_ok: no '/', '\', '.', leading
// dot or space, control character), and at most 47 bytes.
bool theme_folder_ok(const std::string& folder);

// Whether `url` is an address RiftWii may fetch for a theme: https:// on
// riftwii.github.io, a path of plain characters, no user info, no port, no
// query or fragment, at most 199 bytes.
bool theme_url_ok(const std::string& url);

// Reads the list into `out` (room for `max` records). `count` is how many
// themes it holds; `skipped` how many entries were left out for breaking a
// rule. False with `error` when the text is not a schema 1 list at all or
// is over the size cap.
bool parse_theme_list(const std::string& text, ThemeListing* out, std::size_t max, std::size_t& count,
                      std::size_t& skipped, std::string& error);

// A theme's lines in sd:/riftwii/themes/.browser.txt, which records what the
// browser installed ("<folder> <sha256>" per line): the theme the browser
// put there, and the zip it came from, so an Update is offered when the list
// has another one and a Delete only for these.
std::string theme_record_text(const std::string& records, const std::string& folder, const std::string& sha256);
// `records` without `folder`'s line.
std::string theme_record_without(const std::string& records, const std::string& folder);
// The sha256 recorded for `folder`; false when the browser did not install it.
bool theme_record_find(const std::string& records, const std::string& folder, std::string& sha256);

}  // namespace riftwii
