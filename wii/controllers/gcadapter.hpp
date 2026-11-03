// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2017 Alex Chadwick (wup-028-bslug) <https://github.com/Chadderz121/wup-028-bslug>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <cstdint>

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
    unsigned hid_pads = 0;      // configurable pads currently reporting
    bool adapter_present = false;
    std::uint32_t hid_ids[GCAD_HID_PADS] = {};
    bool hid_input[GCAD_HID_PADS] = {};
    int last_error = 0;
    bool present[GCAD_PORTS] = {};
    bool adapter_ports[GCAD_PORTS] = {};
    gcad_pad pads[GCAD_PORTS] = {};
};

// Starts the driver (true when it already runs); false with `why` when
// this IOS has no USB HID. `own_handle`: on RiftWii's own /dev/usb/hid,
// never libogc's, as a game gets it (Check each cIOS rehearses a game).
bool GcAdapterStart(std::string& why, bool own_handle = false);
// Everything the driver counted, on one line (for a problem report).
std::string GcAdapterDiag();
bool GcAdapterRunning();
// Drives it (call it every frame or so) and reads what it knows. Only
// ever from one thread at a time (the GUI thread, or with it halted).
void GcAdapterPoll(GcAdapterView& out);
// Cancels what is in flight, gives the device back and closes.
void GcAdapterStop();

// The menu's controllers (UpdatePads, every frame): starts the adapter
// once it is plugged in (unless the setting is Off), and gives its ports.
void GcAdapterMenuPads(GcAdapterView& out);
// What GcAdapterMenuPads gave last (false before it ever ran the
// driver), for a problem report or the launch's log line.
bool GcAdapterMenuLastView(GcAdapterView& out);
// The menu's first drive scan is over: the adapter may start. Until then
// it waits, so its USB traffic never meets the scan's first card reads
// (a card read failed as the adapter started).
void GcAdapterMenuAllowStart();
// True when the menu was looking for the adapter up to its end and the
// USB list never had one: a launch on Automatic need not look again.
bool GcAdapterMenuSawNone();
// The menu is over (a launch, or leaving): stops it for good.
void GcAdapterMenuEnd();
// A screen after the menu (a failed launch's, an exit's) that waits for a
// press: starts the driver for it when the menu's rule lets this IOS
// reach the adapter and USB lists one (or a mapped USB pad), looked for
// once, so its ports and mapped USB pads can answer (a Wii U
// has no GameCube ports). True when it was started here; the caller
// stops it (GcAdapterStop) on the way out. Never during a launch: there,
// /dev/usb/hid can break the disc's reads. `say_why`: log why it was not
// started (the screen's later tries, every 2 s, say nothing). Once the
// driver itself would not start (USB listed one), it isn't tried again,
// as the menu does: that failure is logged once.
bool GcAdapterScreenStart(bool say_why = true);

}  // namespace riftwii::wii
