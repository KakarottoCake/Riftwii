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
    // A GameCube game switching to another program on its disc (the SDK's
    // OSExec), which RiftWii's blob writes as a launch file
    // (sd:/riftwii/gc/exec.txt, runtime/gc's gc_on_boot_dol): the
    // program's offset on the disc (gc_dol=, 0: the disc's own), the
    // restart code the game passed (gc_restart=) and its arguments
    // (gc_arg=, one each, in order).
    bool gc_exec = false;
    std::uint32_t gc_dol = 0;
    std::uint32_t gc_restart = 0;
    std::vector<std::string> gc_args;
    // A GameCube bonus disc's menu starting a game packed in a TGC image
    // on its disc (runtime/gc's gc_on_tgc_read, the same exec.txt): the
    // TGC file's offset on the disc (gc_tgc=, in decimal; 0: none). The
    // loader starts the TGC's own program, with its files where the TGC
    // has them.
    std::uint32_t gc_tgc = 0;
    // The pack choices file to read instead of the game's own (a file name
    // in sd:/riftwii/choices, "SMNP01.txt" or a profile's): the choices the
    // menu launched the game with, for a launch RiftWii makes for itself.
    std::string choices;
    // A Wii game's own Reset (HOME Menu > Reset): the runtime leaves this
    // launch as a request (sd:/riftwii/restart.txt) and goes to the RiftWii
    // channel, and RiftWii starts the game again from it. reset=1 marks it:
    // the game was already playing, so it is not counted as a new play.
    bool reset = false;
};

// The longest value, in bytes, any key takes, and the most packs and code
// builds one launch names: a request comes from a file, an argument or the
// game itself, and none of them gets to make RiftWii read without a bound.
constexpr std::size_t kMaxLaunchValueBytes = 255;
constexpr std::size_t kMaxLaunchPacks = 32;
constexpr std::size_t kMaxLaunchCodeBuilds = 16;
// A line of a launch file, key and value: longer is not a launch.
constexpr std::size_t kMaxLaunchLineBytes = kMaxLaunchValueBytes + 16;
// How much of a launch file is read, and how many lines of it count.
constexpr std::size_t kMaxLaunchFileBytes = 16 * 1024;
constexpr std::size_t kMaxLaunchLines = 64;

// True when `args` (argv without the program's path) ask for a headless
// launch, that is, start with "--launch".
bool is_headless_launch(const std::vector<std::string>& args);

// Reads the arguments after "--launch". False with `error` naming the
// argument that is missing, unknown or has a value RiftWii does not take.
bool parse_headless_launch(const std::vector<std::string>& args, HeadlessLaunch& out, std::string& error);

// The arguments that start `h` again, "--launch" first and one key=value
// each, only what `h` sets: parse_headless_launch reads them back to the
// same launch. A Wii game's own Reset makes its request from these.
std::vector<std::string> headless_launch_arguments(const HeadlessLaunch& h);

// A launch file's arguments (docs/HEADLESS.md), one per line, "--launch"
// first: the text of the file, or of a sector written a whole sector at a
// time (NULs after the text). Carriage returns and trailing spaces are
// dropped, empty lines skipped. False, with `args` empty, when the text is
// not a launch: it does not start with "--launch" (an "--idle--" sector is
// not one), it has more than kMaxLaunchLines lines, or a line longer than
// kMaxLaunchLineBytes.
bool launch_lines_from_text(const std::string& text, std::vector<std::string>& args);

// The sector a Wii game's own Reset leaves on the card for its next start:
// `args` (from headless_launch_arguments) one per line, NUL-filled to
// `sector_bytes`. `armed`: as the Reset leaves it; otherwise as the loader
// makes the file, "--idle--" for the first line, which the next start skips.
// False, with `why`, when the text does not leave room for a final NUL.
bool launch_request_sector(const std::vector<std::string>& args, bool armed, std::size_t sector_bytes,
                           std::vector<std::uint8_t>& sector, std::string& why);

// A Wii game's own Reset starts it again headless, without the menu, so a
// cIOS that Automatic chose from what the menu saw (on a Wii U, the d2x on
// base 58 when the menu had the GameCube adapter) comes out differently,
// and the game comes back on a cIOS where the adapter cannot work. When
// `args` (from headless_launch_arguments) leave the cIOS to RiftWii (no
// cios key, or "cios=auto"), they name `running_ios` instead, the cIOS the
// launch runs on, if it is one a launch can name (248 ... 252). True when
// they were changed.
bool pin_launch_cios(std::vector<std::string>& args, int running_ios);

// What restart_request_sectors did with the cIOS.
enum class RestartCios {
    Unchanged,  // the request already names one, or running_ios is no slot to name
    Pinned,     // the request names running_ios
    DidNotFit,  // naming it would not fit the sector: left to the restart, as before
};

// A Wii game's own Reset request as its two sectors (launch_request_sector,
// armed and idle), with the cIOS pinned (pin_launch_cios) when the pinned
// request still fits. A request that fits only without the extra line keeps
// its packs and leaves the cIOS to the restart (`cios` DidNotFit), rather
// than losing the Reset hook. False, with `why`, only when `args` themselves
// do not fit.
bool restart_request_sectors(const std::vector<std::string>& args, int running_ios, std::size_t sector_bytes,
                             std::vector<std::uint8_t>& armed, std::vector<std::uint8_t>& idle, RestartCios& cios,
                             std::string& why);

// Whether `value` is a path (or a name) that stays where it is put: no
// ".." in it, between slashes or backslashes, at either end or alone.
bool launch_path_stays_put(const std::string& value);

// Friivolution's launch argument (FRIIV_CFG, docs/HEADLESS.md): a loader
// that starts Friivolution this way can start RiftWii the same way. The
// 384 bytes of argv[1] become the matching "--launch" arguments: the
// image's path, or the disc; the game ID's first four characters when
// given; one pack by file name, every pack for the game, or none. False
// when `data` is not such an argument asking to boot (then RiftWii opens
// its menu, as Friivolution does).
bool friiv_launch_args(const std::uint8_t* data, std::size_t size, std::vector<std::string>& args);

}  // namespace riftwii
