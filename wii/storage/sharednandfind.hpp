// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The Saves choice "SD, emulated NAND": the folder for `game_id`, read
// from the other loader's settings on the SD card (then the USB drive)
// with riftwii/sharednand.hpp. Its default folder when there are no
// settings. False with the reason when the folder cannot be used (on the
// USB drive, or a path that is not a plain folder of the SD card): the
// launch stops then, rather than keeping the save somewhere else.
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

// For a launch: the same, with a log line either way.
bool find_shared_nand(const std::string& game_id, std::string& folder, std::string& error);

}  // namespace riftwii::wii
