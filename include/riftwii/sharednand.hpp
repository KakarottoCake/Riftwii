// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

// The Saves choice "SD, emulated NAND" ("emunand"): the game's saves are
// kept in the emulated NAND folder other loaders already keep on the SD
// card, so a game started from RiftWii and from that loader finds the same
// save. The folder is read from that loader's own settings, as it writes
// them: a global "NandEmuPath = <path>" line in GXGlobal.cfg (default
// sd:/nands/01/) and a game's own "NandEmuPath:<path>;" in its
// "game:<ID6> = ..." line of GXGameSettings.cfg, which wins. The files
// are the player's, so a value is checked before it becomes a folder.
namespace riftwii {

// Where the other loader keeps its emulated NAND when nothing says otherwise.
constexpr const char* kDefaultSharedNand = "sd:/nands/01";

// Longest folder accepted. d2x takes the path after "sd:" in a 32-byte
// buffer in the other loader (and its own NAND paths are appended to it),
// so a longer one is not a folder that loader could have used.
constexpr std::size_t kSharedNandMaxPath = 31 + 3;

// The value of a GXGlobal.cfg line "NandEmuPath = <path>" (spaces and tabs
// around the name and the value trimmed, and a UTF-8 byte order mark at
// the line's start skipped), or false for any other line.
bool shared_nand_global_line(const std::string& line, std::string& path);

// The value of a GXGlobal.cfg line "NandEmuChanPath = <path>": the
// emulated NAND the other loader keeps its WiiWare and Virtual Console
// channels in (sd:/nands/01 too when nothing says otherwise), read the
// same way; false for any other line.
bool shared_nand_channels_line(const std::string& line, std::string& path);

// The NandEmuPath of `game_id`'s line in GXGameSettings.cfg
// ("game:RMCE01 = name:...; NandEmuPath:sd:/nands/02/; ..."), or false for
// another game's line, a line without one, or an empty one (the game then
// uses the global folder).
bool shared_nand_game_line(const std::string& line, const std::string& game_id, std::string& path);

// The configured path as a folder RiftWii can serve saves from: on the SD
// card, no "." or ".." parts, no backslashes, short enough, trailing
// slashes removed ("sd:/nands/01/" is "sd:/nands/01"). False with the
// reason otherwise (a folder on the USB drive among them: RiftWii keeps
// saves only on the SD card).
bool shared_nand_folder(const std::string& configured, std::string& folder, std::string& why);

}  // namespace riftwii
