// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "usbcatalog.hpp"

// Saves on the SD card for a game started on d2x, kept by d2x's own NAND
// emulation as USB Loader GX keeps them: d2x serves the game's NAND paths
// (partial emulation: /title, /tmp, /ticket) from a folder of the card,
// inside IOS, through its own SD driver. RiftWii's runtime then leaves the
// save alone. A Wii U's SD controller failed RiftWii's own raw card
// commands at one step of City Folk's save (a read right after a write,
// every time); d2x's path is the one GX players use every day.
//
// RiftWii's save folders (sd:/riftwii/saves/<ID>/fresh or /clone) hold
// that emulated NAND: the save is in <folder>/title/<type>/<id>/data, for
// d2x and for the runtime (disc games) alike. Before 2610-190 the files
// sat in the folder itself; they are moved into data the first time.
namespace riftwii::wii {

// "<dir>/title/00010000/52555545/data" for title 00010000-52555545.
std::string own_save_data_dir(const std::string& dir, std::uint64_t title_id);

// Moves save files left in `dir` itself (before 2610-190) into
// `data_dir`, creating it when there are any. The clone marker stays.
bool move_flat_saves(const std::string& dir, const std::string& data_dir, std::string& error);

// Before the cIOS reload, under the menu's IOS: the emulated NAND's
// folders, the game's title.tmd (from the image), the old flat files moved
// in and, for `clone` and a save folder that had no save yet, the Wii's
// own save copied in once. `game.title_id` must be known.
bool prepare_d2x_saves(const ImageGame& game, const std::string& dir, bool clone, std::string& error);

// After the reload into d2x and before the game's partition is opened
// (d2x refuses it once a title runs): the card is unmounted (libfat
// flushes), d2x mounts it and serves the NAND from `dir`, the card is
// mounted again (libfat reads it afresh), the log around that.
bool enable_d2x_saves(const std::string& dir, std::string& error);

}  // namespace riftwii::wii
