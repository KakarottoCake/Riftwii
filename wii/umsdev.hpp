// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "riftwii/fat32.hpp"

// d2x's USB mass-storage device, /dev/usb2 (its "UMS" ioctls: init,
// capacity, read sectors), for packs on the USB drive: the loader reads
// their XMLs and finds their files' sectors through it, and hands the
// same fd to the resident runtime, which reads those sectors while the
// game runs. Only under a d2x cIOS (the game's cIOS for an SD or USB
// game, the menu's for a disc); libogc's USB driver is shut down first so
// the drive has one driver. FAT32 with 512-byte sectors, as Riivolution.
namespace riftwii::wii::ums {

// Opens and starts the device once; false with `error` when this IOS has
// no d2x USB device or the drive is unusable.
bool Open(std::string& error);
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
