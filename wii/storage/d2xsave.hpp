// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "d2xnandemu.hpp"
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
// Nothing for a folder outside sd:/riftwii/saves (the shared emulated NAND).
bool move_flat_saves(const std::string& dir, const std::string& data_dir, std::string& error);

// Whether a game's data folder holds no save: missing, or nothing in it
// but RiftWii's hidden clone marker.
bool save_folder_empty(const std::string& data_dir);

// Before the cIOS reload, under the menu's IOS: the emulated NAND's
// folders, the game's title.tmd (from the image), the old flat files moved
// in and, for `clone` and a save folder that had no save yet, the Wii's
// own save copied in once. With `clone_into_empty`, a data folder that
// exists but is empty counts as no save yet (the shared emulated NAND).
// `game.title_id` must be known. A folder on the USB drive (#487) is
// written through its own partition's mount (mount_usb_nand_folder).
// `whole_nand`, when given and true, asks whether the folder holds a
// whole NAND (the Wii's settings file): set false when it does not.
bool prepare_d2x_saves(const ImageGame& game, const std::string& dir, bool clone, bool clone_into_empty,
                       std::string& error, bool* whole_nand = nullptr);

// Before a native CTGP-R launch, under the menu's IOS (docs/CTGPR.md): CTGP-R
// keeps Mario Kart Wii's save in sd:/riivolution/save/RMC<R>/, and without
// that folder the game shows its "Continue without saving" screen. Does what
// <savegame clone> does: when the folder holds no rksys.dat, makes it and
// copies the NAND data folder's files in (each under a temporary name, then
// renamed; rksys.dat last). Nothing on the card is overwritten or deleted but
// this run's own partial files, and a folder with rksys.dat is left alone. A
// Wii with no save gets the empty folder. False, with `why`, when it could not
// be done (the launch goes on; the folder is then left as it was).
bool SeedCtgprSdSave(const std::string& game_id, std::string& why);

}  // namespace riftwii::wii
