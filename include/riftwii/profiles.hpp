// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

// A game's extra Home tiles, each with its own mods: "Mario Kart Wii",
// "Retro Rewind" and "MKW Deluxe" as three tiles of one game. RiftWii calls
// one a profile. It is a name and a copy of the game's choices file of its
// own, so starting its tile starts the game with those mods, and changing
// them on its page leaves the game's own choices alone.
//
// Where a profile lives, all named by its key, "<game ID>@<slug>":
//   sd:/riftwii/profiles.txt                 the list (ProfileList)
//   sd:/riftwii/choices/RMCE01@retro-rewind.txt   its choices (LaunchModel::save)
//   sd:/riftwii/profiles/RMCE01@retro-rewind.png  its picture, when the player adds one
// The slug is made from the name once, of plain letters, digits and '-',
// so a key is always a safe file name; renaming keeps it.
namespace riftwii {

constexpr std::size_t kMaxProfiles = 48;         // on the whole list
constexpr std::size_t kMaxProfilesPerGame = 8;   // more than a page of one game's tiles is noise
constexpr std::size_t kMaxProfileName = 32;      // bytes of UTF-8, as many as the keyboard takes
constexpr std::size_t kMaxProfileSlug = 24;
constexpr std::size_t kMaxProfileArtPath = 200;
constexpr std::size_t kMaxProfilesFile = 32 * 1024;  // 48 lines of at most ~270 bytes, with room

// The choices file a problem report shows for the last launch: the tile
// launched (sd:/riftwii/last_tile.txt, its key on the first line) when it
// is a profile of `launched_id`, the game boot.log names; else the game's
// own. A key that is not a profile of that game (a stale file, another
// game started since) is never used.
constexpr const char* kLastTilePath = "sd:/riftwii/last_tile.txt";
std::string launched_choices_key(const std::string& launched_id, const std::string& last_tile_text);

struct Profile {
    std::string game_id;
    std::string slug;
    std::string name;  // what its tile says
    // The picture its tile shows when the player put none in
    // sd:/riftwii/profiles: the picture of the pack it was made with
    // (riftwii/modart.hpp), or empty.
    std::string art;
    std::string key() const { return game_id + "@" + slug; }
};

// A game ID as the Wii writes them: 4 to 6 capital letters and digits.
bool plausible_game_id(const std::string& id);
// A slug profile_slug could have made.
bool valid_profile_slug(const std::string& slug);
// What Home keys a tile on (play history, favourites): a game ID, or a
// profile's "<game ID>@<slug>".
bool valid_tile_key(const std::string& key);
// The game a tile key starts ("RMCE01" for "RMCE01@retro-rewind" and for "RMCE01").
std::string tile_game_id(const std::string& key);

// A typed name made fit to keep: control characters (tabs and line
// breaks among them) dropped, spaces trimmed and runs of them made one,
// cut to kMaxProfileName bytes on a whole UTF-8 character.
std::string clean_profile_name(const std::string& typed);
// A name's slug: lower-case ASCII letters and digits, anything else one
// '-', none at the ends, at most kMaxProfileSlug; "mods" when nothing is left.
std::string profile_slug(const std::string& name);
// A picture path the list may keep: on "sd:/" or "usb:/", a ".png", with no
// "..", control characters or tabs, at most kMaxProfileArtPath long.
bool valid_profile_art(const std::string& path);

class ProfileList {
public:
    enum class AddResult { Added, NoName, BadGame, GameFull, ListFull };

    // Lenient, as the other files on the card: a line it can't read, a
    // second line for the same key and lines past the limits are dropped.
    void parse(const std::string& text);
    std::string serialize() const;

    const std::vector<Profile>& all() const { return profiles_; }
    std::vector<const Profile*> for_game(const std::string& game_id) const;
    // The tiles Home shows for a game it lists: its profiles when the game
    // is on a drive (USB or SD), none for the disc drive's tile or one of
    // the Wii's own channels, which never read a choices file.
    std::vector<const Profile*> tiles_for(const std::string& game_id, bool on_drive) const;
    const Profile* find(const std::string& key) const;
    // A new profile for `game_id` named `name` (cleaned), its slug made
    // unique among the game's ("retro-rewind-2"); `key` is set to its key.
    AddResult add(const std::string& game_id, const std::string& name, const std::string& art, std::string& key);
    // The key and its files stay: only the name changes. False when there
    // is no such profile or the name cleans to nothing.
    bool rename(const std::string& key, const std::string& name);
    bool remove(const std::string& key);

private:
    std::vector<Profile> profiles_;
};

}  // namespace riftwii
