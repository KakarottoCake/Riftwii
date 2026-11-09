// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>

#include "riftwii/ctgplaunch.hpp"

// The native CTGP-R launch (docs/CTGPR.md): RiftWii loads Mario Kart Wii
// itself and places CTGP-R's code in it, in place of the CTGP-R Channel.
// Offline only: online play, LAN play and ghost signing stay off.
namespace riftwii::wii {

// Made from the player's own copy of CTGP-R, never part of RiftWii
// (docs/CTGPR.md, "Files the native launch needs"):
//   efa.key     the package disk's AES key
//   rows.bin    the table of game addresses CTGP-R hooks
//   tables.bin  the channel's constants (riftwii/ctgptables.hpp)
inline constexpr const char* kCtgprKeyPath = "sd:/riftwii/ctgpr/efa.key";
inline constexpr const char* kCtgprRowsPath = "sd:/riftwii/ctgpr/rows.bin";
inline constexpr const char* kCtgprTablesPath = "sd:/riftwii/ctgpr/tables.bin";
// CTGP-R's own files, as its installer puts them on the card.
inline constexpr const char* kCtgprBlobPath = "sd:/ctgpr/blob.bin";
inline constexpr const char* kCtgprJellyPath = "sd:/ctgpr/jelly.bin";
inline constexpr const char* kCtgprConfigPath = "sd:/ctgpr/config.ini";

// Whether the player has set up the native launch: any of the three
// RiftWii files above is on the card. Then the CTGP-R pack starts natively,
// and a file that is missing is named in the launch's error instead of
// falling back to the CTGP-R Channel (which does not start from RiftWii).
bool ctgpr_native_wanted();

// Builds the plan for the game `game_id` (its 4th character names the
// region). The card must be mounted. False, with `error` in plain words,
// when a file is missing or wrong, or the CTGP-R version is not the tested
// one; nothing has been changed then.
bool prepare_ctgpr_launch(const std::string& game_id, std::shared_ptr<const CtgpLaunchPlan>& out,
                          std::string& error);

}  // namespace riftwii::wii
