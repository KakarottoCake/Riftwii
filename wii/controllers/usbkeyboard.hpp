// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2026 quatric (libwiiinput) <https://github.com/quatric/libwiiinput>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

// A USB keyboard (libogc's libwiikeyboard), read on a thread of its own:
// held and newly pressed keys by USB HID usage code, and the characters
// typed, for the menu (usbinput.cpp). From libwiiinput's keyboard driver.
namespace riftwii::wii {

// USB HID usage codes the menu uses.
namespace key {
constexpr u8 kEnter = 0x28, kEscape = 0x29, kBackspace = 0x2A, kTab = 0x2B, kSpace = 0x2C;
constexpr u8 kOne = 0x1E, kTwo = 0x1F;
constexpr u8 kHome = 0x4A, kPageUp = 0x4B, kDelete = 0x4C, kEnd = 0x4D, kPageDown = 0x4E;
constexpr u8 kRight = 0x4F, kLeft = 0x50, kDown = 0x51, kUp = 0x52;
constexpr u8 kPadEnter = 0x58;
constexpr u8 kF1 = 0x3A;
}  // namespace key

constexpr u8 kModShift = 1 << 0, kModCtrl = 1 << 1, kModAlt = 1 << 2, kModGui = 1 << 3;

struct KeyPress {
    u8 code = 0;  // USB HID usage code
    char ch = 0;  // printable ASCII for the press (Shift and Caps Lock aware), else 0
    u8 mods = 0;  // kMod* held at the time
};

// Starts the thread that finds and reads the keyboard (now or whenever one
// is plugged in). False when libwiikeyboard would not start, or when an
// earlier thread is still stuck (UsbKeyboardStop).
bool UsbKeyboardStart();
// Stops the thread and closes the keyboard. libwiikeyboard's scan waits in
// IOS for the keyboard's next report, which an idle keyboard may not send
// for a while. After half a second, false, and:
// - with an IOS reload next (a launch, the exit), the thread is suspended
//   where it waits and the keyboard left to the reload; it is not started
//   again until then (UsbKeyboardIosReloaded);
// - otherwise (USB input turned off in Settings) the thread ends when the
//   read comes back, and UsbKeyboardStart refuses until it has.
bool UsbKeyboardStop(bool ios_reload_next);
// Stopped without an IOS reload, with the last read still waiting.
bool UsbKeyboardDraining();
bool UsbKeyboardRunning();
bool UsbKeyboardConnected();
// Once per frame, on the thread that reads the keys below: what was
// pressed and released since the last call.
void UsbKeyboardPoll();
bool UsbKeyHeld(u8 code);
bool UsbKeyPressed(u8 code);
// The next key pressed (modifier keys alone are not), oldest first.
bool UsbKeyboardPop(KeyPress& out);
// Drops the presses not yet popped.
void UsbKeyboardFlush();
// Forgets a stuck thread once the IOS that held its read is gone.
void UsbKeyboardIosReloaded();

}  // namespace riftwii::wii
