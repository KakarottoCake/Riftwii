// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

// CTGP-R's save on the SD card (docs/CTGPR.md). CTGP-R's own code in the game
// sends every access to /title/00010004/524d43xx/data/ to
// sd:/riivolution/save/RMC<R>/ (R is the game ID's 4th character: E, J, K or
// P), and shows "Continue without saving" when it finds no save there. The
// pack's <savegame external clone="true"/> asks the loader to fill that
// folder from the NAND once; the native launch does not run the pack's XML, so
// the loader does that step itself (wii/storage/d2xsave.hpp, SeedCtgprSdSave).
// This file is the platform-free part: names and the decision which files
// to copy.
namespace riftwii {

// The region letter of an "RMC?" game ID (E, J, K or P), or 0 for any other ID.
char ctgpr_save_region(const std::string& game_id);

// "sd:/riivolution/save/RMC<R>" for a region letter from ctgpr_save_region.
std::string ctgpr_sd_save_folder(char region);

// "/title/00010004/524d43<hex of R>/data": Mario Kart Wii's data folder on the NAND.
std::string ctgpr_nand_data_dir(char region);

// The folder's name for the player ("RMCP"), as the Saves row says it.
std::string ctgpr_save_folder_label(const std::string& game_id);

// The files of the NAND folder to copy into the card's folder: only names that
// are plain file names, none that the card already has (compared without
// regard to case, as FAT does), and rksys.dat last, so that a folder holding
// rksys.dat is a folder whose copy ran to its end. `on_card` lists what the
// folder already holds. Nothing in the result overwrites anything.
std::vector<std::string> ctgpr_seed_plan(const std::vector<std::string>& nand_names,
                                         const std::vector<std::string>& on_card);

// Whether the seeding is skipped altogether: the folder already holds rksys.dat.
bool ctgpr_seed_skipped(const std::vector<std::string>& on_card);

// The temporary name a file is written under before it is renamed.
std::string ctgpr_seed_temp_name(const std::string& name);

}  // namespace riftwii
