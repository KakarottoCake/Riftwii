// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

// The USB drive's side of an emulated NAND kept there (#487), implemented
// in usbcatalog.cpp beside the drive's mounts; its own header so d2x's
// NAND emulation (d2xnandemu.cpp) and its tests need nothing else of the
// catalog.
namespace riftwii::wii {

// An emulated NAND folder on the USB drive (#487, "usb:/nands/01" or
// "usb2:/nands/01"): which partition d2x's FAT module mounts for it
// (riftwii/imagevolume.hpp's d2x_usb_partition). FAT32 only.
bool usb_d2x_nand_partition(const std::string& usb_folder, std::int32_t& index, std::string& why);
// The folder's FAT32 partition mounted for writing as "rwnand:" and the
// folder's path there, for the save copied in before a launch. libfat's
// own usb: is unmounted meanwhile, so two caches never write one volume.
// Undone by unmount_usb_nand_folder, which mounts usb: again.
bool mount_usb_nand_folder(const std::string& usb_folder, std::string& writable, std::string& why);
void unmount_usb_nand_folder();
// Before d2x's FAT module mounts the drive: libfat's usb: writes what it
// holds and is unmounted, so only d2x writes to the drive while the game
// runs. True when it was mounted. usb_libfat_back_from_d2x mounts it again
// after d2x let go (a launch that did not go on).
bool usb_libfat_hand_to_d2x();
void usb_libfat_back_from_d2x();

}  // namespace riftwii::wii
