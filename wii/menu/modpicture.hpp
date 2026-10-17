// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <string>

#include "riftwii/launch.hpp"

// The mods' pictures for the Mods page (riftwii/modart.hpp): the PNG the
// player put next to a mod, fitted into the popup's box.
namespace riftwii::wii {

constexpr int kModPictureW = 120;
constexpr int kModPictureH = 168;

// The mod's picture as an RGBA8 texture of kModPictureW x kModPictureH,
// or nullptr when it has none or it can't be read. Read and decoded on the
// first ask and kept (the last few, in MEM2 taken once); a mod without
// one is remembered until ForgetModPictures. GUI thread, with the GUI halted.
const u8* ModPicture(const LaunchPackage& p);

// What the player names the picture, for the popup's hint when there is
// none: "Newer.png", "pm.png", "Project+/cover.png".
std::string ModPictureName(const LaunchPackage& p);

// The mod's picture file on the card, the first of its places that has
// one; empty when none does.
std::string ModPicturePath(const LaunchPackage& p);

// A profile's picture for its Home tile (riftwii/profiles.hpp): the PNG the
// player put at sd:/riftwii/profiles/<key>.png, else the one it was made
// with (its pack's). An RGBA8 texture of kProfilePicture square, the
// picture fitted whole in it; nullptr when there is none or it can't be
// read. Read once and kept (the last page's worth); GUI thread, halted.
constexpr int kProfilePicture = 64;
constexpr const char* kProfilePictureDir = "sd:/riftwii/profiles";
const u8* ProfilePicture(const std::string& key, const std::string& art);
// Reads the picture ahead of drawing (Home's loop, a page at a time): true
// when it had to read the card this time, false when it was ready or there
// is none.
bool ProfilePictureLoad(const std::string& key, const std::string& art);

// Forgets which mods had no picture (leaving the Mods page: one may have
// been added since). The pictures read stay.
void ForgetModPictures();
// The same for the profiles' pictures (Home looks again: one may have
// been added, or a profile renamed or removed).
void ForgetProfilePictures();

}  // namespace riftwii::wii
