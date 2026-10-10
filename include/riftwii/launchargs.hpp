// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
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
//   --launch game=RSBE01 code_build=sd:/projectplus
namespace riftwii {

struct HeadlessLaunch {
    std::string game;                // the game ID: six characters, or the first four
    std::string from;                // "usb", "sd", "disc"; empty: the first that has it
    std::string path;                // the image itself (usb:/ or sd:/); empty: found by ID
    std::vector<std::string> xmls;   // packs to turn on (the others off): paths, or file names
    bool packs_given = false;        // an xml= was passed (xml=none: every pack off)
    bool all_packs = false;          // xml=all: every pack for the game on
    // Code builds to turn on (Project+ and the like, SD card only): the
    // build's folder ("sd:/projectplus" or "projectplus"), its code file, or
    // its path without the drive ("pm.raw/Project+/RSBE01.gct" in an SD image).
    // Like xml=, any given turns every pack not named off.
    std::vector<std::string> code_builds;
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

// Friivolution's launch argument (FRIIV_CFG, docs/HEADLESS.md): a loader
// that starts Friivolution this way can start RiftWii the same way. The
// 384 bytes of argv[1] become the matching "--launch" arguments: the
// image's path, or the disc; the game ID's first four characters when
// given; one pack by file name, every pack for the game, or none. False
// when `data` is not such an argument asking to boot (then RiftWii opens
// its menu, as Friivolution does).
bool friiv_launch_args(const std::uint8_t* data, std::size_t size, std::vector<std::string>& args);

}  // namespace riftwii
