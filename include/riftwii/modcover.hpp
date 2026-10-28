// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
