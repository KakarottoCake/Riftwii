// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "nandchannels.hpp"
#include "sharednandfind.hpp"

namespace riftwii::wii {

// The emulated NAND WADs go into, for the wiiware folders and Settings >
// Install WAD files alike: USB Loader GX's channel folder when it is on the
// SD card, else sd:/nands/01. RiftWii prepares and plays channels only from
// the SD card; a tester's channel folder on the USB drive (usb1:/nands/01)
// stopped every WAD. `nand` is look_up_channel_nand()'s answer.
std::string wad_nand_folder(const SharedNandLookup& nand);

// Runs on Home's drive worker, after the corresponding volumes are mounted.
// USB includes every readable partition. One level of subfolders is read.
// Returns the unreadable WAD count for Home's notice; reasons are logged.
unsigned list_wad_channels(bool usb, std::vector<NandChannel>& out);
void merge_wad_channels(std::vector<NandChannel>& out, std::vector<NandChannel>& found);

// Rechecks the WAD and installed files before any IOS reload. Preparation
// preserves the title's data folder, including when a WAD is replaced.
bool prepare_wad_channel(NandChannel& channel, const std::function<void(double)>& progress,
                         std::string& error);

// Auto-prepared titles are supplied by the WAD scan alone: removing a WAD
// removes its tile, while its content and save remain on the SD card.
bool wad_managed_title(const std::string& folder, std::uint64_t title_id);

}  // namespace riftwii::wii
