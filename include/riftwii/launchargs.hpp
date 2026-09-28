// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/launch.hpp"

// Another loader (USB Loader GX) can start a game through RiftWii without
// its menu: it starts RiftWii's boot.dol with "--launch" and key=value
// arguments (docs/HEADLESS.md). Every key but game is optional; a missing
// one leaves RiftWii's own choice for the game.
//
//   --launch game=RMCE01 from=usb xml=sd:/riivolution/ctgp.xml
//            video_mode=pal60 language=en cios=249 server=wiimmfi
//            gct=sd:/codes/RMCE01.gct return_to=0001000147584c44
namespace riftwii {

struct HeadlessLaunch {
    std::string game;                // the six-character game ID
    std::string from;                // "usb", "sd", "disc"; empty: the first that has it
    std::vector<std::string> xmls;   // packs to turn on (the others off)
    bool packs_given = false;        // an xml= was passed (xml=none: every pack off)
    GameSettings settings;           // "global" where not given: RiftWii's own
    std::string wfc_domain;          // for server=custom
    std::string gct;                 // cheat codes file; "none": no cheats; empty: RiftWii's own
    std::uint64_t return_to = 0;     // the title the Wii Menu button starts; 0: RiftWii's choice
    bool return_to_menu = false;     // return_to=menu: the Wii Menu, as the game has it
};

// True when `args` (argv without the program's path) ask for a headless
// launch, that is, start with "--launch".
bool is_headless_launch(const std::vector<std::string>& args);

// Reads the arguments after "--launch". False with `error` naming the
// argument that is missing, unknown or has a value RiftWii does not take.
bool parse_headless_launch(const std::vector<std::string>& args, HeadlessLaunch& out, std::string& error);

}  // namespace riftwii
