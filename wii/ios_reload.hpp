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
    Terminal,        // IOS did not restart after IPC was disabled; do no recovery I/O
};

// Reloads IOS the way libogc's IOS_ReloadIOS does, except that a missing
// ticket view is tolerated under Dolphin, whose HLE boots any IOS it
// knows without needing one installed in the NAND. Every IOS file
// descriptor (DI, SD, ...) is invalid afterwards.
ReloadResult reload_ios(int version, std::string& error);

// True only after reload_ios() returned Terminal. Callers must return to a
// non-IOS-dependent top level without mounting media, reopening logs, or
// initializing input.
bool reload_terminal_failure();

// Never returns and never calls IOS/libogc cleanup. Use only after
// reload_terminal_failure() is true; physical POWER remains available.
[[noreturn]] void halt_after_terminal_reload();

}  // namespace riftwii::wii
