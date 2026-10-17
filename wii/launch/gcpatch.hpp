// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "gclaunch.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {

// Patch-only GameCube mods: VCDIFF (xdelta) patches in sd:/riftwii/patches.
// A patch is listed as the game it makes when one of the GameCube images
// found is its original (its first window's checksum says so) and that
// game is not there already. Starting it prepares the image once, into
// sd:/games/<ID>/game.iso, from then on an ordinary game.
constexpr const char* kGcPatchDir = "sd:/riftwii/patches";

// Adds an entry (gc_patch_source set) for each patch whose original is
// among `games` and whose result is not.
void ScanGcPatches(std::vector<ImageGame>& games);

// Room the SD card needs to prepare `image` (patch in path, original in
// patch_source): the original's size and some to spare. False when the
// original cannot be opened; `have` stays 0 when the card cannot say.
bool GcPatchSpace(const GcImage& image, std::uint64_t& need, std::uint64_t& have);

// Called as the patch is applied, `done` of `total` bytes of the patch
// read; false stops the preparation (error "stopped", nothing kept).
using GcPrepareProgress = std::function<bool(std::uint64_t done, std::uint64_t total)>;

// Makes the patched image `image` (patch in path, original in
// patch_source) as sd:/games/<ID>/game.iso and points `image` at it.
// With `progress` the caller shows the progress (the menu's box); without
// it the launch screen's bar does.
bool PrepareGcPatch(GcImage& image, std::string& error, const GcPrepareProgress& progress = nullptr);

}  // namespace riftwii::wii
