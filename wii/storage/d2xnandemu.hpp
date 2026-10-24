// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2011 Dimok
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// d2x's NAND emulation from a folder of the SD card (d2xsave.hpp has the
// rest of the save handling). d2x reads the card through its FAT module,
// which runs its own SD driver inside IOS, so RiftWii lets go of the card
// completely while the module mounts it: libfat, boot.log and the SD
// driver under libfat.
namespace riftwii::wii {

// After the reload into d2x and before the game's partition is opened
// (d2x refuses it once a title runs). In order: d2x's FAT module is
// opened (an IOS without one is left alone); the card's layout goes to
// boot.log; boot.log is closed, libfat unmounts the card (and flushes) and
// libogc's SD driver is shut down; the module mounts the card (once more
// after a failure) and d2x serves the NAND from `dir`; when the emulation
// does not come on, the module is told to let go of the card again, for
// RiftWii's own save code; libfat mounts the card again (through d2x's own
// SD device once the emulation is on, so d2x is the card's only driver)
// and boot.log reopens with a line for each step. With d2x's own SD device
// already in use (a game on the card) that device stays open, and the
// module is asked once and never told to let go: either would stop the
// card under the device.
bool enable_d2x_saves(const std::string& dir, std::string& error);

}  // namespace riftwii::wii
