// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "riftwii/fat32.hpp"

// d2x's USB mass-storage device, /dev/usb2 (its "UMS" ioctls: init,
// capacity, read sectors), for packs and RVZ games on the USB drive: the
// loader reads their XMLs, headers and sectors through it, and hands the
// same fd to the resident runtime, which reads those sectors while the
// game runs (d2x refuses to open the device once a title runs, so it is
// opened once and never closed). Only under a d2x cIOS (the game's cIOS for an SD or USB
// game, the menu's for a disc); libogc's USB driver is shut down first so
// the drive has one driver. FAT32 with 512-byte sectors, as Riivolution.
namespace riftwii::wii::ums {

// Opens and starts the device once; false with `error` when this IOS has
// no d2x USB device or the drive is unusable.
bool Open(std::string& error);
// Before an IOS reload: the fd and a failed open belong to the IOS that is
// going away (a disc's packs failing under IOS 58 must be tried again
// once Settings has loaded the d2x slot).
void Forget();
// The open fd, or -1.
int Fd();
// Raw sectors into any buffer (through a MEM2 bounce buffer: d2x's USB
// driver reads into MEM2 only).
bool Read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out);
// The drive's FAT32 volume, read through Read (Open first).
bool Volume(const Fat32Volume*& out, std::string& error);
// A whole file of the volume, by "usb:/..." path.
bool ReadText(const std::string& usb_path, std::string& out, std::string& error);

}  // namespace riftwii::wii::ums
