// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "rtgcad.h"

// The menu's check of the GameCube controller adapter for Wii U: the same
// driver the game gets (runtime/rtgcad.c), run here through libogc's
// asynchronous IPC so Settings can show what the adapter reports before
// a game is started. Every step lands in session.log.
namespace riftwii::wii {

struct GcAdapterView {
    bool open = false;          // /dev/usb/hid is open
    unsigned version = 0;       // 4 or 5
    unsigned link = GCAD_LINK_NONE;
    unsigned reports = 0;
    unsigned listed = 0;        // devices in the last list
    int last_error = 0;
    bool present[GCAD_PORTS] = {};
    gcad_pad pads[GCAD_PORTS] = {};
};

// Opens /dev/usb/hid and starts the driver; false with `why` when this
// IOS has no USB HID.
bool GcAdapterStart(std::string& why);
// Drives it (call it every frame or so) and reads what it knows.
void GcAdapterPoll(GcAdapterView& out);
// Cancels what is in flight, gives the device back and closes.
void GcAdapterStop();

}  // namespace riftwii::wii
