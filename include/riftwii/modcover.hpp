// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
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

// A mod tile's own 3D cover and full box (the 3D covers view, the shelf),
// kept as its flat cover is: downloaded from the mod's GameTDB entry and
// stored under the tile's key, "<game ID>@<slug>", beside a ".mod" file
// that names the entry they came from (a tile keeps its key when renamed).

// A name the 3D cover and box stores take: a game's six characters, a
// homebrew app's four (GameTDB has pictures for some), or a mod tile's key.
bool valid_art_name(const std::string& name);
// Whether `name` is a mod tile's key rather than a game's ID.
bool is_tile_art_name(const std::string& name);
// The GameTDB entry whose picture a download for `name` asks for: a
// game's own ID, or for a mod tile the entry it matches now
// (`tile_match`, from mod_cover_id). Empty: nothing to ask for (a tile
// GameTDB doesn't list, or a name the stores don't take).
std::string art_download_id(const std::string& name, const std::string& tile_match);
// Whether a mod tile's stored picture, or its "GameTDB has none" note,
// is for the entry it matches now: `stored_from` is what its .mod file
// says. A tile that matches nothing any more has nothing current.
bool tile_art_current(const std::string& stored_from, const std::string& tile_match);

// What a picture store has for a name, for whether to download it.
struct StoredArt {
    bool picture = false;      // the picture is on the card
    bool recent_miss = false;  // GameTDB had none, less than a week ago
    std::string from;          // a tile's: the entry they came from ("" if none)
};
// Whether `name`'s picture is to be downloaded: a game's when it has
// neither a picture nor a recent miss; a mod tile's only when it matches
// an entry, and then also when what is stored is another entry's.
bool art_wanted(const std::string& name, const std::string& tile_match, const StoredArt& stored);

// Which picture a Home tile shows where a 3D cover or a full box can stand
// (the 3D covers ring, the shelf, the close-up, the game page), in order:
// the player's own PNG for a mod tile, the mod's own 3D cover or box, the
// tile's flat cover when it comes first (the player's own PNG for the game,
// a chosen second set, the mod's own flat cover), then the game's 3D cover
// or box.
struct TileArtOrder {
    bool tile_art_first = false;        // try the mod tile's own 3D cover or box
    bool flat_before_game_art = false;  // without it: the flat cover, not the game's
};
// `is_tile`: a mod tile's key; `own_png_for_tile`: the flat cover in memory
// is the player's own PNG for that tile; `flat_first`: the flat cover in
// memory comes before the game's 3D cover or box (CoverBeforeBox).
TileArtOrder tile_art_order(bool is_tile, bool own_png_for_tile, bool flat_first);

// The boxes a game page downloads for its tile, each once a visit: by name
// and, for a mod tile, the GameTDB entry it matches, so a box still wanted
// after its answer (no answer, a renamed tile's old entry's) is never asked
// for again, and a tile renamed to another entry gets that entry's box.
class PageBoxAsks {
public:
    // A box's key: the game's ID for the game's box, else the mod tile's
    // key with the entry ("RSBE01@project-plus|RSBEPL").
    static std::string key(const std::string& name, const std::string& game_id, const std::string& entry);
    bool asked(const std::string& key) const { return asked_.count(key) != 0; }
    // Whether the box `key`, wanted now, counts as started (then asked):
    // with a download of its name already running (`running_key`, its key
    // when it started; "" when none runs), only when that one is for the
    // same entry, so a renamed tile's new box waits for the old one's to end;
    // with none running, when `started_new` (a download was just started).
    bool start(const std::string& key, const std::string& running_key, bool started_new);
    // The mod tile `name` matches the entry in `key` now (after a rename):
    // when that is another entry than last time, what was asked for the
    // tile is forgotten, so going back to an earlier entry tries its box
    // again; an unchanged match keeps its one try.
    void tile_matched(const std::string& name, const std::string& key);

private:
    std::set<std::string> asked_;
    std::map<std::string, std::string> tile_keys_;  // each tile's key when last told
};

}  // namespace riftwii
