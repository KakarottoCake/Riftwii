// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "rtgcad.h"

// The GameCube controller adapter for Wii U in the menu: the same driver
// the game gets (runtime/rtgcad.c), run here through libogc's
// asynchronous IPC. Its controllers work the menu like the Wii's own
// GameCube ports (UpdatePads), and Settings has a page showing what the
// adapter reports. Every step lands in session.log.
//
// On IOS 58, /dev/usb/hid is v5 and libogc's USB (the menu's USB drive)
// has it open: a second opener is refused there (a Wii U's IOS 58
// answered -4), so the adapter is then reached through libogc's own
// handle (USB_OpenDevice and its interrupt messages). On a d2x cIOS
// (v4) the driver opens /dev/usb/hid itself, as before.
namespace riftwii::wii {

struct GcAdapterView {
    bool open = false;          // the driver runs
    bool through_ogc = false;   // on libogc's USB handle (v5)
    unsigned version = 0;       // 4 or 5
    unsigned link = GCAD_LINK_NONE;
    unsigned reports = 0;
    unsigned listed = 0;        // devices in the last list
    int last_error = 0;
    bool present[GCAD_PORTS] = {};
    gcad_pad pads[GCAD_PORTS] = {};
};

// Starts the driver (true when it already runs); false with `why` when
// this IOS has no USB HID.
bool GcAdapterStart(std::string& why);
bool GcAdapterRunning();
// Drives it (call it every frame or so) and reads what it knows. Only
// ever from one thread at a time (the GUI thread, or with it halted).
void GcAdapterPoll(GcAdapterView& out);
// Cancels what is in flight, gives the device back and closes.
void GcAdapterStop();

// The menu's controllers (UpdatePads, every frame): starts the adapter
// once it is plugged in (unless the setting is Off), and gives its ports.
void GcAdapterMenuPads(GcAdapterView& out);
// The menu's first drive scan is over: the adapter may start. Until then
// it waits, so its USB traffic never meets the scan's first card reads
// (a card read failed as the adapter started).
void GcAdapterMenuAllowStart();
// The menu is over (a launch, or leaving): stops it for good.
void GcAdapterMenuEnd();

}  // namespace riftwii::wii
