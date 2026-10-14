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

// Forgets which mods had no picture (leaving the Mods page: one may have
// been added since). The pictures read stay.
void ForgetModPictures();

}  // namespace riftwii::wii
