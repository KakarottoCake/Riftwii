// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2011 Dimok
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "rtable.h"

// d2x's NAND emulation from a folder of the SD card (d2xsave.hpp has the
// rest of the save handling). d2x reads and writes the card through its
// FAT module, which runs its own SD driver and its own file system cache
// inside IOS. So RiftWii lets go of the card completely while the module
// mounts it (libfat, boot.log and the SD driver under libfat), and once
// the emulation is on RiftWii stays off the card until the next IOS
// reload: two FAT writers on one card could hand out the same clusters,
// and a second driver could reset the card under the module.
namespace riftwii::wii {

// What d2x emulates. A game's saves: its own paths only (/title, /tmp,
// /ticket; d2x's "partial" mode). A channel started from an emulated NAND:
// the whole NAND, as that NAND's own system would see it (d2x's full flag,
// 0x100), the mode the channel loader of #220 set for WiiWare and
// Virtual Console titles on an emulated NAND.
enum class D2xNandScope { GamePaths, WholeNand };

// Kept by one channel launch, never across launches or card changes. A
// failed mount is not repeated on the same IOS, even after a reload.
struct D2xNandMountFailures {
    bool ios[256] = {};
};

// After the reload into d2x and before the game's partition is opened
// (d2x refuses it once a title runs). In order: the folder's path is
// checked (d2x takes at most 255 characters); d2x's FAT module is opened
// (an IOS without one is left alone); the card's layout goes to boot.log;
// boot.log is closed, libfat unmounts the card (and flushes) and libogc's
// SD driver is shut down; the module mounts the card (once more after a
// failure) and d2x serves the NAND from `dir`.
//
// True only with the emulation on. Then RiftWii stays off the card
// (hold_sd_for_d2x_fat): libfat is not mounted again, boot.log stays
// closed (its lines wait in memory and on screen), and every way RiftWii
// has to the card refuses until the next IOS reload or
// release_d2x_saves.
//
// False with the emulation off: the module was told to let go of the
// card again, for RiftWii's own save code, and libfat has the card back
// with boot.log. With d2x's own SD device already in use (a game on the
// card) that device stays open, and the module is asked once and never
// told to let go: either would stop the card under the device.
//
// `card_back`, when given, says whether libfat and boot.log have the card
// after the call; a refusal before the card was touched (the folder, no
// FAT module) leaves it as the caller set it.
// `mount_failures`, when given by a channel launch, records an IOS whose
// module could not mount the card (after its retry), so that launch does
// not reload it again. A remembered failure returns before touching the
// card; cleanup after a new failure still runs.
bool enable_d2x_saves(const std::string& dir, std::string& error, D2xNandScope scope = D2xNandScope::GamePaths,
                      bool* card_back = nullptr, D2xNandMountFailures* mount_failures = nullptr);

// A successful enable still in effect on this IOS, rather than a FAT
// module holding the card after a failure. An IOS reload clears the hold.
bool d2x_nand_emulation_active();

// When the emulation came on but the launch does not go on with it (ES
// refused the title on this IOS, the launch failed): d2x's emulation is
// turned off (mode 0, while no title runs) and its FAT module unmounts the
// card (0xF1: it writes what it holds and stops its driver); d2x's own SD
// device, if a game on the card used it, is let go too, and RiftWii may
// use the card again. False when the emulation could not be turned off
// or the module would not let go: RiftWii
// then stays off the card until an IOS reload (its restart does one).
// Logs what it did and the hand-off's summary line again, for a failure's
// boot.log. True and nothing done when the emulation was not on.
bool release_d2x_saves();

// Whether a launch needs the SD card in the game, so d2x's NAND emulation
// must stay off: a pack read from anywhere but the USB drive (the card's
// own, a network pack's files cached on it), or a compiled table entry on
// the card.
bool packs_need_sd(const std::vector<std::string>& xml_paths);
bool entries_need_sd(const std::vector<rt_entry>& entries);

}  // namespace riftwii::wii
