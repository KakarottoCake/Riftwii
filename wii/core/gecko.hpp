// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-FileCopyrightText: WiiFlow Lite contributors <https://github.com/Fledge68/WiiFlow_Lite>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

// Debug output over a USB Gecko in memory card slot B (Dolphin emulates
// one on TCP port 55020: tools/dolphin/gecko_log.py), for developers.
// Settings > USB Gecko log ("usb_gecko_log = on") starts it: logf's lines
// then reach the PC as they are logged, and so does whatever libogc and
// the libraries print to stdout or stderr while the menu owns the screen.
// Follows USB Loader GX's source/gecko.c (gprintf, hexdump,
// USBGeckoOutput) and WiiFlow Lite's source/gecko/gecko.cpp (the send
// with interrupts off, so a line is never split by another thread's).
namespace riftwii::wii {

// Looks for the USB Gecko and, when it answers, turns the output on and
// points stdout and stderr at it. False, with nothing changed, when no
// USB Gecko is in slot B. Calling it again only looks again.
bool GeckoStart();
// Whether GeckoStart found one.
bool GeckoActive();
// Sends `text` as it is (no line ending added); nothing while inactive.
void GeckoWrite(const char* text, std::size_t length);
void GeckoPrintf(const char* format, ...) __attribute__((format(printf, 1, 2)));
// `length` bytes from `data` as rows of 16, offset, hex and ASCII, as USB
// Loader GX's hexdump prints them, for a buffer worth seeing whole.
void GeckoHexDump(const void* data, std::size_t length);

// Whether a USB Gecko answers in slot (EXI channel) 0 (A) or 1 (B). Probes
// the slot; for the WiiRD debugger's choice of handler at a launch.
bool GeckoInSlot(int slot);

}  // namespace riftwii::wii
