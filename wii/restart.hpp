// SPDX-FileCopyrightText: 2026 RiftWii contributors
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

// BurnedDisc: the menu runs under a d2x cIOS for the new session, which
// reads burned discs (wii/menuios.hpp, BurnedDiscSlot).
// SdRetry: "Try again" on the screen that says no SD card could be read;
// the fresh IOS of a restart lets go of a card the last run left held.
enum class RestartKind : unsigned { None = 0, LaunchFailed = 1, Crashed = 2, ChannelDone = 3, BurnedDisc = 4, Theme = 5, MenuFont = 6, UsbCheck = 7, SdRetry = 8 };

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
