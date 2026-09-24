// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <set>
#include <string>

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
    std::string borders = "keep";         // keep, remove
    std::string video_mode = "game";      // a VideoMode name
    std::string game_language = "console";  // riftwii/gamelang.hpp names
    std::string game_cios = "auto";       // auto (d2x in 249-251), 248 ... 252
    std::string wfc_server = "off";       // online play: riftwii/wfcpatch.hpp names
    std::string wfc_domain;               // the "custom" server's domain
    std::string home_tiles = "covers";    // Home's tiles: covers or names
    bool online = true;                   // download game names and cheats when the Wii is online
    std::string gc_adapter = "off";       // GameCube controller adapter for Wii U: off, on (demo: Dolphin tests)
    std::set<std::string> favorites;      // game IDs, written "favorites = ID,ID"
    std::map<std::string, std::string> other;

    void parse(const std::string& text);
    std::string serialize() const;
};

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
