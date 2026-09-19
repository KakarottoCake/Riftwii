// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace riftwii::wii {

// True when the loader runs under Dolphin (its IOS exposes /dev/dolphin).
bool running_in_dolphin();

enum class ReloadResult {
    Ok,              // now running the requested IOS
    AlreadyRunning,  // nothing to do
    NotInstalled,    // the console has no ticket for that IOS
    Failed,          // ES/IPC error, details in `error`
};

// Reloads IOS the way libogc's IOS_ReloadIOS does, except that a missing
// ticket view is tolerated under Dolphin, whose HLE boots any IOS it
// knows without needing one installed in the NAND. Every IOS file
// descriptor (DI, SD, ...) is invalid afterwards.
ReloadResult reload_ios(int version, std::string& error);

}  // namespace riftwii::wii
