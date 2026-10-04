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
    std::string language = "auto";        // auto (the Wii's), en, es, ja, pt, it
    std::string video_width = "game";     // riftwii/videopatch.hpp names
    std::string deflicker = "game";
    std::string borders = "keep";         // keep, remove (the sides), remove_all (experimental)
    std::string video_mode = "game";      // a VideoMode name
    std::string game_language = "console";  // riftwii/gamelang.hpp names
    std::string game_cios = "auto";       // auto (d2x in 249-251), 248 ... 252
    std::string wfc_server = "off";       // online play: riftwii/wfcpatch.hpp names
    std::string wfc_domain;               // the "custom" server's domain
    std::string home_tiles = "covers";    // Home's tiles: covers or names
    std::string menu_sounds = "quiet";    // normal, quiet (a soft hover tick), off
    std::string menu_music = "on";        // on, off: music.ogg while the menu is open
    std::string theme = "default";        // default, or a folder in sd:/riftwii/themes (docs/THEMES.md)
    std::string return_to = "riftwii";    // riftwii, menu: where a game's "Wii Menu" goes
    std::string screenshots = "off";      // in-game screenshots: on, off (demo: Dolphin tests)
    bool online = true;                   // download game names and cheats when the Wii is online
    std::string riitag_key;               // RiiTag (riftwii/riitag.hpp): the player's key, empty for none
    std::string update_channel = "auto";  // stable, beta, or auto (the build's own: beta for a -suffix version)
    std::string gc_adapter = "auto";      // GameCube controller adapter for Wii U: auto (when plugged in at launch), on, off (demo: Dolphin tests)
    std::set<std::string> favorites;      // game IDs, written "favorites = ID,ID"
    // More folders to look for games in, besides wbfs and games:
    // "game_folders = /Wii Games; usb:/iso". A plain "/path" is looked
    // for on both drives, "sd:/path" or "usb:/path" on that one.
    std::vector<std::string> game_folders;
    std::map<std::string, std::string> other;

    void parse(const std::string& text);
    std::string serialize() const;
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

}  // namespace riftwii
