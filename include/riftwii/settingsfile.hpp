// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "riftwii/launch.hpp"
#include "riftwii/videopatch.hpp"
#include "riftwii/wfcpatch.hpp"

namespace riftwii {

// RiftWii's own settings (sd:/riftwii/settings.txt): "key = value" lines,
// "#" comments. Unknown keys are kept, so an older RiftWii does not drop
// what a newer one wrote.
struct LoaderSettings {
    std::string language = "auto";        // auto (the Wii's), en, es, ja, pt, it, ko
    std::string video_width = "game";     // riftwii/videopatch.hpp names
    std::string deflicker = "game";
    std::string borders = "keep";         // keep, remove (the sides), remove_all (experimental)
    std::string video_mode = "game";      // a VideoMode name
    std::string aspect = "game";          // game, 4:3, 16:9: every game's, unless its own page says otherwise
    std::string game_language = "console";  // riftwii/gamelang.hpp names
    std::string game_cios = "auto";       // auto (d2x in 249-251), 248 ... 252
    std::string wfc_server = "off";       // online play: riftwii/wfcpatch.hpp names
    std::string wfc_domain;               // the "custom" server's domain
    std::string home_tiles = "covers";    // Home's tiles: covers, names, shelf or channels
    std::string channel_corners = "theme";  // theme, square, slight, round, rounder: the Channels view's tiles
    std::string channel_background = "none";
    std::string grid_spacing = "normal";  // normal, roomy: Home's grid smaller, with wider gaps at its sides
    std::string page_peek = "on";         // on, off: Channels shows the next and previous pages' nearest columns at its sides
    std::string menu_widescreen = "off";  // auto (the Wii's own 16:9 setting), on, off: the menu kept in shape on a 16:9 TV
    int screen_size = 100;                // the menu's size on screen, 80-100 (%), inside what a TV's overscan crops
    std::string menu_sounds = "quiet";    // normal, quiet (a soft hover tick), off
    std::string menu_music = "on";        // on, off: music.ogg while the menu is open
    std::string launch_music = "on";      // on, off: the music plays on under the launch screen, or fades out as the menu closes
    std::string play_history = "on";      // on, off: count the games played from RiftWii (history.txt)
    std::string home_source = "all";      // all, sd, usb: which drive's games Home lists; gamecube: the GameCube games only; wii: its WiiWare and Virtual Console games
    std::string home_sort = "az";         // az, recent (last played first), most (most played first), custom (sd:/riftwii/order.txt)
    std::string home_start = "last";      // last (opens on the last game played), first (always on the first page)
    std::string home_disc = "on";         // on, off: the Disc drive's tile on Home (asked on a new card)
    bool nand_non_games = false;
    std::string theme = "default";        // default, or a folder in sd:/riftwii/themes (docs/THEMES.md)
    std::string clock = "auto";           // auto (as the menu's language writes it), 12, 24: Home's clock
    std::string menu_font = "riftwii";    // riftwii, wii: the Wii Menu's own font, read from the NAND (riftwii/sysfont.hpp)
    std::string return_to = "riftwii";    // riftwii, menu: where a game's "Wii Menu" goes
    std::string screenshots = "off";      // in-game screenshots: on, off (demo: Dolphin tests)
    bool online = true;                   // download game names and cheats when the Wii is online
    std::string riitag_key;               // RiiTag (riftwii/riitag.hpp): the player's key, empty for none
    std::string gc_rumble = "on";         // on, off: GameCube controllers' rumble in games (PADControlMotor)
    std::string wiimote_rumble = "on";    // on, off: Wii Remotes' rumble in every game (WPADControlMotor; a game's own Rumble too)
    std::string gc_adapter = "auto";      // GameCube controller adapter for Wii U: auto (when plugged in at launch), on, off (demo: Dolphin tests)
    std::string sd_games = "on";          // on, off: look for game images on the SD card (sd:/wbfs, sd:/games); off for setups that keep games only on USB
    std::string gc_backups = "on";        // on, off: copy a GameCube game's memory card to sd:/riftwii/gc/backups before it starts
    std::string nintendont_saves = "ask"; // ask, asked: whether Home has asked about bringing Nintendont's saves (any answer, Not now too, makes it "asked")
    std::set<std::string> favorites;      // game IDs and profiles' tile keys, written "favorites = ID,ID@slug"
    // More folders to look for games in, besides wbfs and games:
    // "game_folders = /Wii Games; usb:/iso". A plain "/path" is looked
    // for on both drives, "sd:/path" or "usb:/path" on that one.
    // A folder holding ';' cannot be named: saving leaves it out.
    std::vector<std::string> game_folders;
    std::map<std::string, std::string> other;

    void parse(const std::string& text);
    std::string serialize() const;
};

// What "Reset all settings" leaves: every setting back to how RiftWii
// comes, except what the question promises to keep and what finding the
// games depends on. Favorites are the player's own picks, not a setting;
// game_folders say where the games are, and without them Home would
// empty. Unknown keys (`other`) are reset with the rest. Games' own pages,
// play history, order, profiles and mods live in files of their own that
// a reset never touches.
LoaderSettings settings_after_reset(const LoaderSettings& before);

// Where Home puts the focus while the drives are still being read at
// start-up. Home first shows what is there and the SD card's and the USB
// drive's games join in as each is read, so a late decision would move the
// page under the player.
//   KeepTile   leave the focus on the tile the player chose, wherever the
//              games that join in put it
//   KeepPage   stay on the page (and place on it) the player is looking
//              at, rather than follow a tile they never chose
//   FirstTile  the first tile, so the first page
//   LastGame   the tile of the game played last
// `start_on` is the "home_start" setting; `player_moved` is true once the
// player has navigated at all (see FocusWatch); `grid_seen` is true once the
// grid has been on screen long enough to be looked at; `last_game_listed`
// is true when the last game played has a tile.
enum class HomeStartAction { KeepTile, KeepPage, FirstTile, LastGame };
HomeStartAction home_start_action(const std::string& start_on, bool player_moved, bool grid_seen,
                                  bool last_game_listed);

// Whether the player has moved the focus: the menu says where it put the
// focus itself (expect), and any other focus or page seen afterwards (observe)
// is the player's. Once moved, always moved: away and back is moved.
class FocusWatch {
public:
    void expect(int index, int page) { index_ = index; page_ = page; }
    void observe(int index, int page) {
        if (index != index_ || page != page_) moved_ = true;
    }
    bool moved() const { return moved_; }

private:
    int index_ = 0;
    int page_ = 0;
    bool moved_ = false;
};

// The extra game folders for one drive ("sd" or "usb"), as "/path":
// slashes turned forward, no trailing slash, wbfs and games left out
// (they are always looked in).
std::vector<std::string> game_folders_on(const LoaderSettings& settings, const std::string& device);

// The video settings a launch uses: the game's own choices, where it has
// them, over the global defaults. The mode is left for the Wii to turn
// into a target (it depends on the console's settings).
VideoSettings effective_video(const GameSettings& game, const LoaderSettings& global);
// The game language code (-1: the console's) and the cIOS slot (0: auto).
int effective_game_language(const GameSettings& game, const LoaderSettings& global);
int effective_game_cios(const GameSettings& game, const LoaderSettings& global);
// "auto" or a slot the loader offers: 248 ... 252.
bool parse_cios_choice(const std::string& s, int& slot);
// The online server a launch uses; Custom without a valid domain is Off.
WfcServer effective_wfc_server(const GameSettings& game, const LoaderSettings& global);
constexpr int kFirstGameCios = 248, kLastGameCios = 252;
// How round the Channels view's tiles are, as a multiple of their corner
// radius (riftwii/skinpaint.hpp): the theme's own [shape] corners for
// "theme" (or a value this RiftWii does not know), else the player's choice.
float channel_corner_scale(const std::string& choice, float theme_corners);

}  // namespace riftwii
