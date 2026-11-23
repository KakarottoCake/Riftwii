// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The games Home found the last time it read the drives, kept on the card
// (sd:/riftwii/homelist.txt) so the next start can show them at once.
//
// Reading the SD card and a USB drive takes seconds at every start, and
// Home stayed empty until each drive was done. The drives are still read
// at every start, exactly as before: this list only fills Home meanwhile.
// When a drive's part of the scan comes in it replaces the remembered
// games of that drive, so a game deleted since goes and a new one joins.
// Nothing is started from a remembered game: a tile picked while the
// drives are read opens once they are, as on a rescan.
//
// One game a line, its fields separated by tabs:
//
//     homelist 1
//     usb <tab> RMCE01 <tab> 0 <tab> m <tab> Mario Kart Wii <tab> usb:/wbfs/Mario Kart Wii [RMCE01]/RMCE01.wbfs <tab>
//     sd  <tab> GALE01 <tab> 0 <tab> g <tab> Super Smash Bros. Melee <tab> sd:/games/Melee/game.iso <tab>
//     end 2
//
// The drive, the game's ID, its disc number, its marks ("g" a GameCube
// game, "m" a game with mods, "-" neither), the name its tile shows, its
// path, and for a patch-only GameCube mod the original it is made from.
// The last line counts the games: a file cut short by a full card or a
// power cut has no such line and is not used.
namespace riftwii {

struct RememberedGame {
    bool usb = false;               // on a USB drive, else on the SD card
    std::string id;
    std::uint8_t disc_number = 0;
    bool gamecube = false;
    bool has_mods = false;          // a pack or code build was found for it
    std::string name;               // as its tile shows it
    std::string path;               // sd:/..., usb:/... or usb<N>:/...
    std::string patch_source;       // a patch-only GameCube mod's original, else empty
};

// As many games as a scan lists at most (4000 a drive), and room for
// their lines: a larger file is not RiftWii's and is not read.
constexpr std::size_t kHomeListMaxGames = 8000;
constexpr std::size_t kHomeListMaxBytes = 1u << 20;
constexpr std::size_t kHomeListMaxName = 256;
constexpr std::size_t kHomeListMaxPath = 1024;

// Whether a game can be written as a line and read back the same: an ID
// of one to six plain characters, a name and a path of sensible length
// with no tab, line break or other control character, and a path on the
// drive the game says it is on.
bool remembered_game_valid(const RememberedGame& game);

// The file. Games that are not valid (above) are left out; at most
// kHomeListMaxGames are written.
std::string serialize_home_list(const std::vector<RememberedGame>& games);

// The games of a file serialize_home_list wrote. Strict, as the file is
// RiftWii's own: a line that is not a game's, a missing or wrong last
// line, or a file past the limits above gives no games at all (false, and
// `out` empty), so Home starts empty as it did before.
bool parse_home_list(const std::string& text, std::vector<RememberedGame>& out);

}  // namespace riftwii
