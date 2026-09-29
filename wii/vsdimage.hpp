// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

// The virtual SD card's image (wii/vsdhook.hpp) as the menu sees it: a
// file named sd.raw in the riftwii folder of the SD card or, when the SD
// card has none, of the USB drive. While code builds are listed or their
// codes read, the image's own card is a drive of the menu's, "vsd:"
// (read only), so the code builds inside it show and run like the ones
// on the SD card: their keys start with "sd.raw/", their paths with
// "vsd:/", and launching one serves the image to the game as its SD card.
namespace riftwii::wii {

constexpr const char* kVsdKeyPrefix = "sd.raw/";
constexpr const char* kVsdDrive = "vsd:/";

// "sd:/riftwii/sd.raw" or "usb:/riftwii/sd.raw" (only when the menu's USB
// view is up); "" when there is none.
std::string VsdImageLocation();

// Mounts "vsd:" for its lifetime (nested ones share the mount). ok() is
// false when there is no image or its card cannot be read.
class VsdMount {
public:
    VsdMount();
    ~VsdMount();
    VsdMount(const VsdMount&) = delete;
    VsdMount& operator=(const VsdMount&) = delete;
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

}  // namespace riftwii::wii
