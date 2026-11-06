// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

// The Saves choice "Emulated NAND" ("emunand"): the game's saves are kept
// in the emulated NAND folder other loaders already keep on the SD card or
// the USB drive, so a game started from RiftWii and from that loader finds
// the same save (#487: one drive shared by two Wiis). The folder is read from that loader's own settings, as it writes
// them: a global "NandEmuPath = <path>" line in GXGlobal.cfg (default
// sd:/nands/01/) and a game's own "NandEmuPath:<path>;" in its
// "game:<ID6> = ..." line of GXGameSettings.cfg, which wins. The files
// are the player's, so a value is checked before it becomes a folder.
namespace riftwii {

// Where the other loader keeps its emulated NAND when nothing says otherwise.
constexpr const char* kDefaultSharedNand = "sd:/nands/01";

// Longest folder accepted, after its drive's "sd:" or "usb1:". d2x takes
// that part in a 32-byte buffer in the other loader (and its own NAND
// paths are appended to it), so a longer one is not a folder that loader
// could have used.
constexpr std::size_t kSharedNandMaxPath = 31;

// The emulated NAND folder Settings names on each drive (#487).
constexpr const char* kSdSharedNand = "sd:/nands/01";
constexpr const char* kUsbSharedNand = "usb:/nands/01";

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
// card ("sd:/"), on the USB drive's partition RiftWii uses ("usb:/") or on
// a numbered partition ("usb1:/", as the other loader writes it, which
// RiftWii names the same way), no "." or ".." parts, no backslashes, short
// enough, trailing slashes removed ("sd:/nands/01/" is "sd:/nands/01").
// False with the reason otherwise.
bool shared_nand_folder(const std::string& configured, std::string& folder, std::string& why);

// Whether a folder shared_nand_folder accepted is on the USB drive.
bool shared_nand_on_usb(const std::string& folder);

}  // namespace riftwii
