// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "riftwii/emulatorfile.hpp"

namespace riftwii {

// An emulator described by a WiiFlow plugin file (sd:/wiiflow/plugins/*.ini),
// read so that emulators set up for WiiFlow Lite appear on Home with nothing
// more to write. The file's own format (docs/EMULATORS.md, "WiiFlow plugin
// files") is turned into RiftWii's EmulatorFile; its DOL is found by the
// caller, which can see the drives.
struct WiiflowPlugin {
    EmulatorFile file;                        // dol left empty
    std::vector<std::string> dol_candidates;  // WiiFlow's search order
};

enum class WiiflowPluginResult { Emulator, NotAnEmulator, Refused };

// plugins_dir is the folder the file was found in (sd:/wiiflow/plugins);
// loader is the DOL an emulator may be told to return to ({loader}).
// NotAnEmulator: one of WiiFlow's own lists (Wii, GameCube, channels,
// homebrew), its music player or ScummVM, which RiftWii shows its own way.
WiiflowPluginResult parse_wiiflow_plugin(const std::string& text, const std::string& plugins_dir,
                                         const std::string& loader, WiiflowPlugin& out, std::string& why);

}  // namespace riftwii
