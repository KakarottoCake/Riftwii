// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include <ogc/disc_io.h>

// d2x's SD device, /dev/sdio/sdhc (d2x cIOS sdhc-module). When a game image
// on the SD card is booted, d2x reads it through this device. The card must
// then have exactly one driver: this device sets the card up once and
// selects it around each transfer, and another driver on /dev/sdio/slot0
// (libogc's, or the runtime's raw one) resets and selects it on its own,
// after which d2x read the wrong sectors ("disc header has an invalid game
// id" on hardware). So from the reload into the cIOS on, the loader's
// libfat, its raw reads and the resident runtime all go through this
// device too, on one handle that stays open for the whole launch: closing
// a handle shuts the device's card state down under d2x as well.
namespace riftwii::wii {

// Opens the device (once) and initialises the card through it.
bool d2x_sd_open(std::string& error);
// The shared handle, or -1 before d2x_sd_open.
std::int32_t d2x_sd_fd();
// Sector transfers through the shared handle; any buffer alignment.
bool d2x_sd_read(std::uint32_t sector, std::uint32_t count, void* buffer);
bool d2x_sd_write(std::uint32_t sector, std::uint32_t count, const void* buffer);

// Which SD driver the loader uses from now on: d2x's while an SD image
// game is being launched, else libogc's __io_wiisd.
void use_d2x_sd(bool on);
bool using_d2x_sd();
const DISC_INTERFACE* sd_interface();

}  // namespace riftwii::wii
