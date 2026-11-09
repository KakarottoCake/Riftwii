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

// The launch needs only CTGP-R's own files, as its installer puts them on the
// card (docs/CTGPR.md). RiftWii carries CTGP-R's non-secret constants, row
// table, course table and cup layouts in its own build, so the player needs
// no RiftWii files for it.
inline constexpr const char* kCtgprBlobPath = "sd:/ctgpr/blob.bin";
inline constexpr const char* kCtgprJellyPath = "sd:/ctgpr/jelly.bin";
inline constexpr const char* kCtgprConfigPath = "sd:/ctgpr/config.ini";

// Whether CTGP-R is installed on the card (blob.bin and jelly.bin are there).
// Then the CTGP-R pack starts natively, and a problem is named in the launch's
// error instead of falling back to the CTGP-R Channel (which does not start
// from RiftWii).
bool ctgpr_native_wanted();

// What the card's CTGP-R is to the native launch. Worked out once (it opens
// blob.bin and reads its version list) and kept, so the menu can ask on every
// redraw; ctgpr_native_status_forget makes the next ask read the card again
// (after the packs are scanned).
enum class CtgprNativeStatus {
    NotInstalled,  // blob.bin or jelly.bin is missing: the pack goes the channel-app way
    Supported,     // its core and track package are the versions the launch is made for
    Unsupported,   // another version, or files that do not open: the launch refuses it
};
CtgprNativeStatus ctgpr_native_status();
void ctgpr_native_status_forget();

// Builds the plan for the game `game_id` (its 4th character names the
// region). The card must be mounted. False, with `error` in plain words
// (translated; the technical detail goes to the log), when a file is missing
// or wrong, or the CTGP-R version is not the one the launch is made for;
// nothing has been changed then.
bool prepare_ctgpr_launch(const std::string& game_id, std::shared_ptr<const CtgpLaunchPlan>& out,
                          std::string& error);

}  // namespace riftwii::wii
