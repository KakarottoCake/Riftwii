// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace riftwii {

constexpr std::size_t kEmulatorCoverMax = 2u << 20;
constexpr std::size_t kEmulatorIndexMax = 2u << 20;
constexpr std::size_t kEmulatorIndexNamesMax = 16384;
constexpr std::size_t kEmulatorArtNameMax = 255;
constexpr std::size_t kSteamGridJsonMax = 512u << 10;
constexpr std::int64_t kEmulatorArtWeek = 7 * 24 * 60 * 60;

// A single folder/file component, never a relative path. Used for local
// covers and for the optional libretro system folder in the manifest.
bool emulator_art_component(const std::string& name);
bool emulator_cover_id(const std::string& id);
std::string libretro_cover_name(const std::string& game);
std::string libretro_cover_url(const std::string& system, const std::string& name = "");

// Only relative, percent-encoded PNG links from the directory listing are
// accepted. A malformed/oversized list leaves no partial result.
bool parse_libretro_index(const std::string& html, std::vector<std::string>& names);
bool serialize_emulator_index(const std::vector<std::string>& names, std::string& text);
bool parse_emulator_index(const std::string& text, std::vector<std::string>& names);
// Returns a PNG filename. Close sequels and ambiguous loose matches are
// refused; equivalent region variants prefer the game's region, then US/EU/JP.
std::string match_emulator_cover(const std::string& game, const std::vector<std::string>& names);

// Separate miss files for each source mean enabling a fallback (or changing
// its system/key) does not inherit a miss from another source.
bool emulator_art_miss_fresh(const std::string& text, const std::string& source, std::int64_t now);
std::string emulator_art_miss(const std::string& source, std::int64_t now);

struct SteamGridGame {
    std::uint32_t id = 0;
    std::string name;
};
bool steamgrid_key_valid(const std::string& key);
bool steamgrid_image_url(const std::string& url);
bool parse_steamgrid_search(const std::string& json, std::vector<SteamGridGame>& games);
bool parse_steamgrid_grids(const std::string& json, std::vector<std::string>& urls, bool wide = false);
std::uint32_t match_steamgrid_game(const std::string& game, const std::vector<SteamGridGame>& games);

}  // namespace riftwii
