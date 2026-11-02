// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <vector>

// A mod's own cover from GameTDB (#385): many mods are listed there as
// custom games whose IDs keep the stock game's first four characters
// (Retro Rewind is RMCETO for Mario Kart Wii's RMCE01, Project+ RSBEPL for
// Super Smash Bros. Brawl's RSBE01). A mod run from the SD card has no ID
// of its own, so its entry is found by its name, strictly: one stock game
// can have hundreds of custom entries with near-identical titles, so a
// match is the mod's name as a whole phrase of the title, and only a
// single match counts. Never a guess: none, or more than one, is no cover.
namespace riftwii {

// One of GameTDB's custom games: its ID and its English title.
struct CustomGame {
    std::string id;
    std::string title;
};

// Network data, bounded: Mario Kart Wii alone had about 400 custom entries
// in 2026, of about 60 bytes a line in the file below.
constexpr std::size_t kMaxCustomGames = 4096;
constexpr std::size_t kMaxCustomTitleBytes = 96;
constexpr std::size_t kMaxCustomGamesBytes = 512u << 10;

// A stock game's first four characters, by which its mods are listed
// ("RMCE" for RMCE01); empty for anything but a 6-character game ID.
std::string custom_prefix(const std::string& stock_id);
// The prefixes of the player's games (`ids`), for the scan to keep.
std::set<std::string> custom_prefixes(const std::set<std::string>& ids);

// Reads one wiitdb.xml <game> block: true for a custom game with a
// 6-character ID and a title (the English one, else its name).
bool parse_wiitdb_custom(const std::string& block, CustomGame& out);

// sd:/riftwii/mod-titles.txt, made whenever the database is read: a
// comment line, then "<ID>", a tab and "<title>" a line. The writer leaves out what
// goes past the bounds; the reader takes at most those.
// `written`, when given: how many entries went in (fewer than given when
// some were not valid or the bounds cut the list).
std::string serialize_custom_games(const std::vector<CustomGame>& games, std::size_t* written = nullptr);
// `damaged`, when given: set when a line (not blank, not a comment) isn't
// one the writer makes, or comes after the first kMaxCustomGames. The rest is still read, but a list with a line
// missing could make one mod look like the only match.
std::vector<CustomGame> parse_custom_games(const std::string& text, bool* damaged = nullptr);

// The words of a title or a mod's name, lowercased, each run of anything
// but letters, digits and '+' made one space ("Super Smash Bros. Project+"
// -> "super smash bros project+"): '+' is part of a name (Project+ is not
// Project M).
std::string mod_title_words(const std::string& text);

// Whether `name`'s words are a whole run of `title`'s words.
bool title_has_mod_name(const std::string& title, const std::string& name);

// The custom game for the mod `name` on the stock game `stock_id`: among
// `customs`, those whose ID starts with stock_id's first four characters
// (and isn't the stock game), the one whose title has the name. Empty when
// none matches or several do.
std::string mod_cover_id(const std::vector<CustomGame>& customs, const std::string& stock_id,
                         const std::string& name);

}  // namespace riftwii
