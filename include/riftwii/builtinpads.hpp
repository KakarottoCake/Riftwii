// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace riftwii {

// A USB controller layout RiftWii knows without a file on the SD card:
// one of Nintendont's controller.ini files (vendor-nintendont-controllers),
// read with parse_hid_config like a file the player put there.
struct BuiltinPad {
    std::uint32_t vid_pid;  // VID << 16 | PID
    const char* name;       // the file's [section] name, for the log
    const char* file;       // the file it came from
    const char* text;       // the file, as Nintendont has it
};

// Made by tools/make_builtin_pads.py (src/data/builtinpads_data.cpp),
// sorted by vid_pid.
extern const BuiltinPad kBuiltinPads[];
extern const std::size_t kBuiltinPadCount;

// The layout for a controller's VID and PID, or nullptr.
const BuiltinPad* find_builtin_pad(std::uint32_t vid_pid);

}  // namespace riftwii
