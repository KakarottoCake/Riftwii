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
// `force` reloads even when `version` is already running (a fresh IOS for
// a game when the menu runs under the same cIOS).
ReloadResult reload_ios(int version, std::string& error, bool force = false);

// True only after reload_ios() returned Terminal. Callers must return to a
// non-IOS-dependent top level without mounting media, reopening logs, or
// initializing input.
bool reload_terminal_failure();

// How the last successful reload_ios() went (how long the new IOS took to
// announce itself and to open IPC), for the boot log, which is closed while
// the reload runs.
const std::string& last_reload_detail();

// Shuts the Wii Remote stack down once. It keeps Bluetooth IPC in flight
// and saves pairings to NAND on shutdown, so it must stop while the
// running IOS is still alive: before any IOS reload and before handoff.
void release_wii_remotes();

// Never returns and never calls IOS/libogc cleanup. Use only after
// reload_terminal_failure() is true; physical POWER remains available.
[[noreturn]] void halt_after_terminal_reload();

}  // namespace riftwii::wii
