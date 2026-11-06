// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The Saves choice "Emulated NAND": the folder for `game_id`. Settings'
// "Emulated NAND folder" names it (sd:/nands/01 or usb:/nands/01), or
// leaves it to the other loader's settings on the SD card (then the USB
// drive), read with riftwii/sharednand.hpp, with its default folder when
// there are none. False with the reason when the folder cannot be used
// (not a plain folder of the SD card or the USB drive): the launch stops
// then, rather than keeping the save somewhere else.
namespace riftwii::wii {

// What the settings say for one game, checked.
struct SharedNandLookup {
    std::string configured;  // the path as written (or the default)
    std::string from;        // which file it came from, for the log
    std::string folder;      // the folder to use, when usable
    std::string error;       // why it cannot be used, otherwise
    bool usable = false;
};

// Reads the settings without logging (the game page shows the result).
SharedNandLookup look_up_shared_nand(const std::string& game_id);

// The emulated NAND folder the other loader keeps its WiiWare and Virtual
// Console channels in (GXGlobal.cfg's NandEmuChanPath, else sd:/nands/01),
// checked as a save folder is.
SharedNandLookup look_up_channel_nand();

// For a launch: the same, with a log line either way.
bool find_shared_nand(const std::string& game_id, std::string& folder, std::string& error);

}  // namespace riftwii::wii
