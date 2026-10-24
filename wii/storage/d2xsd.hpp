// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include <ogc/disc_io.h>

// d2x's SD device, /dev/sdio/sdhc (d2x cIOS sdhc-module). It is used only
// when a game image on the SD card is booted: d2x reads the game through
// this device. The card must then have exactly one driver: this device
// sets the card up once and selects it around each transfer, and another
// driver on /dev/sdio/slot0 (libogc's, or the runtime's raw one) resets and
// selects it on its own, after which d2x read the wrong sectors ("disc
// header has an invalid game id" on hardware). So from the reload into the
// cIOS on, the loader's libfat, its raw reads and the resident runtime all
// go through this device too, on one handle that stays open for the whole
// launch: closing a handle shuts the device's card state down under d2x as
// well. d2x's NAND emulation (d2xnandemu.hpp) never switches a launch onto
// this device: while the emulation has the card, RiftWii stays off it.
namespace riftwii::wii {

// Opens the device (once per IOS) and initialises the card through it.
bool d2x_sd_open(std::string& error);
// Before an IOS reload, while the old IOS still runs: closes the handle
// and goes back to libogc's driver, so the next d2x_sd_open really opens
// and starts the new IOS's device. reload_ios calls it; libfat must not
// be mounted on the device by then.
void d2x_sd_forget();
// Why the last d2x_sd_open failed ("" after one that worked).
const std::string& d2x_sd_last_error();
// The shared handle, or -1 before d2x_sd_open.
std::int32_t d2x_sd_fd();
// Sector transfers through the shared handle; any buffer alignment.
bool d2x_sd_read(std::uint32_t sector, std::uint32_t count, void* buffer);
bool d2x_sd_write(std::uint32_t sector, std::uint32_t count, const void* buffer);

// Which SD driver the loader uses from now on: d2x's while an SD image
// game is being launched, else libogc's __io_wiisd. Chosen after the
// reload into the game's cIOS; d2x_sd_forget turns it off again before
// the next reload.
void use_d2x_sd(bool on);
bool using_d2x_sd();
// Each call goes through one lock, so threads never use the card at once.
// While d2x's FAT module holds the card (below), an interface that refuses
// every start, read and write instead.
const DISC_INTERFACE* sd_interface();

// d2x's FAT module has the card for its NAND emulation (d2xnandemu.hpp),
// with its own file system cache: a second FAT writer (libfat, the
// runtime, a game's own SD driver passed the card) could allocate the same
// clusters as the save d2x writes, and a second driver could reset the
// card under it. While held, sd_interface, d2x_sd_read/write and
// sdio::open_card refuse. d2x_sd_forget ends it: an IOS reload ends the
// module and its emulation with it.
void hold_sd_for_d2x_fat(bool held);
bool sd_held_by_d2x_fat();

}  // namespace riftwii::wii
