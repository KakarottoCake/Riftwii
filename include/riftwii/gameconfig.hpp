// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// gameconfig.txt: the per-game code handler settings Gecko OS introduced
// and the USB loaders read (USB Loader GX, WiiFlow, Configurable USB
// Loader). Mods built from Gecko codes (Project+ builds, for instance)
// ship one beside their codes:
//
//   RSBE01:                        a game ID (or its first letters), then ':'
//   codeliststart = 80566528       where the code list goes (hex)
//   codelistend = 80597800         and where its room ends
//   hooktype = 7                   what runs the code handler (below)
//   poke(800042B8, 60000000)       a word written before the game starts
//   pokeifequal(803E9930, 4BFECA1D, 803E9930, 60000000)
//                                  written only where the first word holds
//                                  the second (so one file fits every revision)
//
// Lines until the next ID belong to that ID; '#' starts a comment. Hook
// types: 1 the video retrace (RiftWii's own choice), 7 the audio frame
// (AXNextFrame); Gecko OS's others (2-6) are not supported.
namespace riftwii {

struct GamePoke {
    std::uint32_t address = 0;
    std::uint32_t value = 0;
    bool conditional = false;  // pokeifequal
    std::uint32_t check_address = 0;
    std::uint32_t check_value = 0;
};

struct GameConfig {
    bool found = false;               // a section for the game was there
    std::uint32_t codelist_start = 0; // 0: not set
    std::uint32_t codelist_end = 0;
    int hooktype = 0;                 // 0: not set
    std::vector<GamePoke> pokes;
    std::vector<std::string> ignored; // lines of the game's sections not understood
};

// The settings for `game_id` in `text`. Several sections may match (by
// prefix): their lines add up in file order, later values winning.
GameConfig parse_gameconfig(const std::string& text, const std::string& game_id);

}  // namespace riftwii
