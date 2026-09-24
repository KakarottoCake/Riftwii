// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

// d2x's USB storage device, /dev/usb2 (d2x-cios ehci-module; usb-module
// registers the same name and commands). An RVZ game on the USB drive is
// read through it: by the loader after the reload into the cIOS (libogc's
// own USB driver is shut down by then) and, in game, by the resident
// runtime on the same handle. d2x refuses to open the device once a title
// runs, so the loader opens it once and never closes it.
namespace riftwii::wii {

// Opens the device (once), starts mass storage on it and checks the
// drive has 512-byte sectors.
bool d2x_usb_open(std::string& error);
// The shared handle, or -1 before d2x_usb_open.
std::int32_t d2x_usb_fd();
// Sector reads through the shared handle; any buffer alignment.
bool d2x_usb_read(std::uint32_t sector, std::uint32_t count, void* buffer);

}  // namespace riftwii::wii
