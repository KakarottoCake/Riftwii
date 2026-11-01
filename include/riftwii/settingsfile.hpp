// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "riftwii/launch.hpp"
#include "riftwii/parental.hpp"
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
    bool wii_menu_sounds = false;       // System Menu effects, read from NAND into memory only
    bool wii_menu_music = false;        // System Menu background music; a theme keeps priority
    std::string menu_sounds = "quiet";    // normal, quiet (a soft hover tick), off
    std::string menu_music = "on";        // on, off: music.ogg while the menu is open
    std::string launch_music = "on";      // on, off: the music plays on under the launch screen, or fades out as the menu closes
    std::string page_banner_sound = "off";  // on, off: a game's page plays its banner's sound when opened outside the Channels view
    std::string disc_art = "off";         // on, off: GameTDB's picture of the game's disc under the box on its page
    // The player's own menu music, picked in Settings or written here:
    // "" for RiftWii's (a theme's, the Wii Menu's or music.ogg), else an
    // .ogg or .mp3 song, an .m3u playlist, or a folder ending in '/' whose
    // songs play shuffled ("usb:/Music/"); riftwii/musiclist.hpp.
    std::string music_file;
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
    bool emulator_covers = true;
    bool steamgrid_covers = false;        // requires the player's own key on the SD card
    std::string riitag_key;               // RiiTag (riftwii/riitag.hpp): the player's key, empty for none
    std::string riitag_id;                // RiiTag: the player's Discord ID, for Settings > Online > My RiiTag; empty for none
    std::string gc_rumble = "on";         // on, off: GameCube controllers' rumble in games (PADControlMotor)
    std::string wiimote_rumble = "on";    // on, off: Wii Remotes' rumble in every game (WPADControlMotor; a game's own Rumble too)
    std::string usb_input = "on";         // on, off: configured USB pads (menu and games), keyboards, mice, pen tablets and uDraw
    std::string gc_adapter = "auto";      // GameCube controller adapter for Wii U: auto (when plugged in at launch), on, off (demo: Dolphin tests)
    std::string sd_games = "on";          // on, off: look for game images on the SD card (sd:/wbfs, sd:/games); off for setups that keep games only on USB
    std::string gc_backups = "on";        // on, off: copy a GameCube game's memory card to sd:/riftwii/gc/backups before it starts
    std::string nintendont_saves = "ask"; // ask, asked: whether Home has asked about bringing Nintendont's saves (any answer, Not now too, makes it "asked")
    // For developers with a USB Gecko (wii/core/gecko.hpp):
    std::string usb_gecko_log = "off";    // on, off: RiftWii's log also goes to a USB Gecko in slot B, with what libraries print
    std::string wiird_debugger = "off";   // off, on, pause: games start with the WiiRD debugger's code handler (pause: held at the first frame for the PC)
    // Parental controls (riftwii/parental.hpp). The PIN's salted hash, ""
    // for none: without it the other three do nothing, so deleting the
    // parental_pin line takes every lock away.
    std::string parental_pin;
    std::string parental_mode = "lock";   // lock (shown; the PIN starts them), hide (left off Home)
    int parental_age = 12;                // the highest age rating allowed without the PIN
    std::string parental_unrated = "allow";  // allow, restrict: a game whose rating RiftWii does not know
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
// empty. Parental controls stay too: a reset from the menu must not be a
// way round them. Unknown keys (`other`) are reset with the rest. Games' own pages,
// play history, order, profiles and mods live in files of their own that
// a reset never touches.
LoaderSettings settings_after_reset(const LoaderSettings& before);

// The parental controls the settings ask for (inactive without a PIN).
ParentalRules parental_rules(const LoaderSettings& settings);

// Where Home puts the focus while the drives are still being read at
// start-up. Home first shows what is there and the SD card's and the USB
// drive's games join in as each is read, however long that takes.
//   KeepTile   the player has moved: leave the focus on the tile they chose,
//              wherever the games that join in put it
//   FirstTile  the first tile, so the first page
//   LastGame   the tile of the game played last
// `start_on` is the "home_start" setting; `player_moved` is true once the
// player has pressed a button or pushed a stick (see PlayerActed); `last_game_listed` is true
// when the last game played has a tile among the games read so far.
enum class HomeStartAction { KeepTile, FirstTile, LastGame };
HomeStartAction home_start_action(const std::string& start_on, bool player_moved, bool last_game_listed);

// One part of the scan joining in, as the decision sees it. The tiles are
// the ones Home lists now, in order: `identities` names each tile as Home
// tells tiles apart across a refresh (the Disc drive's tile has its own),
// `keys` are the play history's game keys ("" for a tile that is no game).
struct HomeStartPart {
    std::string start_on;
    bool player_moved = false;     // see PlayerActed
    bool already_decided = false;  // an earlier part, or an action of the player's, ended the start-up focus
    std::string last_game_key;     // the game played last; "" if none
    std::string focused_identity;  // the tile in focus before this part joined; "" if none
    int index_before = 0;
    bool last_part = false;
    std::vector<std::string> identities;
    std::vector<std::string> keys;
};
// The tile to focus after the part. `decided` is set once no later part may
// change the focus (the last part came, or the player has moved).
int home_start_focus(const HomeStartPart& part, bool& decided);

// Whether input is the player acting on Home: any button newly pressed, or a
// stick pushed past its dead zone, on any controller. The Wii Remote pointer
// moving is not.
inline bool counts_as_player_action(std::uint32_t buttons_pressed, bool stick_pushed) {
    return buttons_pressed != 0 || stick_pushed;
}

// A stick counts when it crosses the dead zone, not while it sits past it: a
// worn pad or an idle GamePad that rests outside from the start is not the
// player. The first sample only sets the starting side.
class StickEdge {
public:
    bool crossed(bool outside_now) {
        const bool crossing = seen_ && outside_now && !outside_;
        outside_ = outside_now;
        seen_ = true;
        return crossing;
    }

private:
    bool outside_ = false;
    bool seen_ = false;
};

// Whether the player has acted since Home started, so that the games joining
// in while the drives are read never move the focus from under them. It is
// told only about input (see), never about the focus: the menu moves that
// itself and the pointer's hover moves it too. Once set it stays set.
// mark() is for the player's actions that are not read as input here.
// Written by the GUI thread, read by the menu's (atomic: no data race).
class PlayerActed {
public:
    void see(bool input_was_action) {
        if (input_was_action) acted_ = true;
    }
    void mark() { acted_ = true; }
    bool acted() const { return acted_; }

private:
    std::atomic<bool> acted_{false};
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
