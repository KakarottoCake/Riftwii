// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/gameconfig.hpp"
#include "riftwii/launch.hpp"

// Code builds: mods made of Gecko codes that load their own files from the
// SD card while the game runs, as their authors ship them for USB loaders
// (docs/GUIDE.md, "Code builds"). RiftWii finds them by their code file,
// named after the game, in a folder at the top of the card or in its
// codes folder: sd:/rex_/RSBE01.GCT, sd:/Project+/codes/RSBE01.gct,
// sd:/codes/RSBE01.gct. Any other code file can be picked on the game's
// Mods page (sd:/riftwii/codebuilds.txt keeps the picks). Each shows on
// the Mods page as a switch. Where the code list goes and what runs it
// come from gameconfig.txt (riftwii/gameconfig.hpp): the code file's
// folder first, then the folders above it up to the top of the card.
//
// Code builds inside the virtual SD card's image (sd.raw, wii/vsdimage.hpp)
// are listed too, keyed "sd.raw/Project+/RSBE01.GCT" with their paths on
// the image's own card ("vsd:/Project+/RSBE01.GCT"); launching one serves
// the image to the game as its SD card.
namespace riftwii::wii {

struct CodeBuildFile {
    std::string key;      // how the choices file names it: "rex_/RSBE01.GCT"
    std::string name;     // shown on the Mods page: "rex_"
    std::string folder;   // "sd:/rex_"
    std::string gct;      // "sd:/rex_/RSBE01.GCT"
    std::string game_id;  // "RSBE01"
    bool picked = false;  // from the Mods page, not found by its name
};

std::vector<CodeBuildFile> ListCodeBuilds();

// The picks: `gct` (a full sd:/ path) for `game_id`, and the key it shows
// under; forgetting takes a picked build's key.
bool PickCodeBuild(const std::string& game_id, const std::string& gct, std::string& key, std::string& error);
bool ForgetCodeBuild(const std::string& key, std::string& error);
bool IsPickedCodeBuild(const std::string& key);

// A folder's subfolders, then its .gct files, for picking one.
struct CodeBrowseEntry {
    std::string name;
    bool folder = false;
};
std::vector<CodeBrowseEntry> BrowseForCodes(const std::string& folder);

// What a launch runs for the code builds turned on in `model`, joined
// with the cheats' list `cheats` (may be empty). False, with `error` for
// the player, when a build's codes cannot be run as they are.
struct CodeBuildLaunch {
    std::vector<std::uint8_t> gct;     // the builds' codes, then the cheats
    std::uint32_t list_start = 0;      // 0: the handler's own room
    std::uint32_t list_end = 0;
    int hooktype = 0;                  // gameconfig's; 0 unset
    std::vector<GamePoke> pokes;
    std::string names;                 // for the log
    bool in_image = false;             // from the virtual SD card's image (all of them, then)
};
bool PrepareCodeBuilds(const LaunchModel& model, const std::string& game_id, const std::vector<std::uint8_t>& cheats,
                       CodeBuildLaunch& out, std::string& error);

}  // namespace riftwii::wii
