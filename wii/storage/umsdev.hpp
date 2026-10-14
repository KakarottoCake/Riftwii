// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "riftwii/fat32.hpp"
#include "riftwii/imagevolume.hpp"

// d2x's USB mass-storage device, /dev/usb2 (its "UMS" ioctls: init,
// capacity, read sectors), for packs and RVZ games on the USB drive: the
// loader reads their XMLs, headers and sectors through it, and hands the
// same fd to the resident runtime, which reads those sectors while the
// game runs (d2x refuses to open the device once a title runs, which for
// d2x starts when the game partition is opened: so it is opened before
// that, once, and closed only for an IOS reload). Only under a d2x cIOS (the game's cIOS for an SD or USB
// game, the menu's for a disc); libogc's USB driver is shut down first so
// the drive has one driver. FAT32 or NTFS, with 512-byte sectors.
namespace riftwii::wii::ums {

// Opens and starts the device once; false with `error` when this IOS has
// no d2x USB device or the drive is unusable.
bool Open(std::string& error);
// Open, asking again once a second for up to `seconds` while the device is
// there but the drive has not started: right after the reload into the
// cIOS some drives take a while (a tester's NTFS drive answered -100 at
// once; the WBFS path already waited, in usbcatalog.cpp).
bool OpenWaiting(std::string& error, int seconds);
// Before an IOS reload: the fd and a failed open belong to the IOS that is
// going away (a disc's packs failing under IOS 58 must be tried again
// once Settings has loaded the d2x slot).
void Forget();
// The open fd, or -1.
int Fd();
// The drive's sector size once open: 512 for a hard drive, 2048 for a
// DVD in a USB DVD drive. Packs and RVZ games need 512 (Volume says so).
std::uint32_t SectorBytes();
// Raw sectors of SectorBytes() each into any buffer (through a MEM2
// bounce buffer: d2x's USB driver reads into MEM2 only).
bool Read(std::uint64_t sector, std::uint32_t count, std::uint8_t* out);
// The drive's FAT32 or NTFS volume, read through Read (Open first).
bool Volume(const ImageVolume*& out, std::string& error);
// A whole file of the volume, by "usb:/..." path.
bool ReadText(const std::string& usb_path, std::string& out, std::string& error);

}  // namespace riftwii::wii::ums
