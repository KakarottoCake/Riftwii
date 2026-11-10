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
// card (docs/CTGPR.md), and RiftWii's ctgpr plugin, which ships with RiftWii
// (docs/PLUGINS.md, "The ctgpr plugin"): it carries CTGP-R's non-secret
// constants, row table and course table, reads the cup layouts from the
// player's install and builds the plan. This file is the DOL's side: the
// checks that need no plugin, the MEM2 block the plan's buffers live in, the
// plugin's launch, and the words the player sees.
inline constexpr const char* kCtgprBlobPath = "sd:/ctgpr/blob.bin";
inline constexpr const char* kCtgprJellyPath = "sd:/ctgpr/jelly.bin";

// Whether CTGP-R is installed on the card (blob.bin and jelly.bin are there).
// Then the CTGP-R pack starts natively, and a problem is named in the launch's
// error instead of falling back to the CTGP-R Channel (which does not start
// from RiftWii).
bool ctgpr_native_wanted();

// What the card's CTGP-R is to the native launch, for the menu's notes. Only
// whether its files are there: which version they are is in its encrypted
// package disk, which only the ctgpr plugin reads, and the menu does not load
// the plugin for a note. A version the launch cannot start is refused, in
// plain words, when it is started. Worked out once and kept, so the menu can
// ask on every redraw; ctgpr_native_status_forget makes the next ask look at
// the card again (after the packs are scanned).
enum class CtgprNativeStatus {
    NotInstalled,  // blob.bin or jelly.bin is missing: the pack goes the channel-app way
    Installed,     // CTGP-R's files are there: the pack starts natively
};
CtgprNativeStatus ctgpr_native_status();
void ctgpr_native_status_forget();

// Builds the plan for the game `game_id` (its 4th character names the
// region): loads the ctgpr plugin into the plugin arena in MEM2, has it build
// the plan, and shuts it down again. The card must be mounted. False, with
// `error` in plain words (translated; the technical detail goes to the log),
// when the plugin is missing, from another RiftWii or damaged, a file is
// missing or wrong, or the CTGP-R version is not one the launch is made for;
// nothing has been changed then.
bool prepare_ctgpr_launch(const std::string& game_id, std::shared_ptr<const CtgpLaunchPlan>& out,
                          std::string& error);

}  // namespace riftwii::wii
