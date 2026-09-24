// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// Starting RiftWii again without the Homebrew Channel: after a launch
// that failed, and after a crash (wii/crash.cpp).
//
// crt0 calls __CheckARGV before it clears .bss; Makefile.wii wraps it, so
// every start first copies the image's writable data (.ctors to .sdata,
// about 20 KB) to a snapshot at the bottom of the MEM2 limit
// (memlimits.hpp). A restart shuts libogc down, puts that copy back over
// the data and jumps to crt0's entry: the program then starts exactly as
// the Homebrew Channel started it, argv included. A small handoff record
// beside the snapshot tells the new start why it happened; it then
// reloads IOS (dropping every handle the old run left open) and shows
// the reason on Home.
namespace riftwii::wii {

enum class RestartKind : unsigned { None = 0, LaunchFailed = 1, Crashed = 2 };

// What the previous run left, read once at startup (and cleared).
struct RestartNote {
    RestartKind kind = RestartKind::None;
    std::string message;
    unsigned crash_restarts = 0;  // crash restarts in a row, this one included
};
RestartNote TakeRestartNote();
const RestartNote& CurrentRestartNote();

// Whether the snapshot is in place (a restart is possible).
bool CanRestart();

// Restarts RiftWii; returns only if it cannot (no snapshot). `unmount`
// is false after a crash, when the card's lock may be held.
bool WarmRestart(RestartKind kind, const std::string& message, bool unmount = true);

}  // namespace riftwii::wii
