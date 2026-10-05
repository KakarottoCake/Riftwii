// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

// The virtual SD cards' images (wii/vsdhook.hpp) as the menu sees them:
// every .raw file in the riftwii folder of the SD card and of the USB
// drive (sd.raw, pm.raw, rex.raw...). While code builds are listed or
// their codes read, one image's own card is a drive of the menu's,
// "vsd:" (read only), so the code builds inside it show and run like the
// ones on the SD card: their keys start with the image's name
// ("pm.raw/codes/RSBE01.gct"), their paths with "vsd:/", and launching
// one serves that image to the game as its SD card.
namespace riftwii::wii {

constexpr const char* kVsdDrive = "vsd:/";

struct VsdImageFile {
    std::string name;      // "pm.raw"
    std::string location;  // "sd:/riftwii/pm.raw" or "usb:/riftwii/pm.raw"
};

// The SD card's images, then the USB drive's (only while the menu's USB
// view is up) but for a name the SD card already has, sorted by name.
std::vector<VsdImageFile> ListVsdImages();

// Where the image `name` is, the SD card's first; "" when it is nowhere.
std::string VsdImageLocation(const std::string& name);

// The image a code build's key is inside: "pm.raw" for
// "pm.raw/codes/RSBE01.gct", "" for a build on the SD card itself.
std::string VsdImageOfKey(const std::string& key);

// Mounts the image `name` as "vsd:" for its lifetime (nested ones of the
// same image share the mount; another image's cannot mount meanwhile).
// ok() is false when there is no such image or its card cannot be read.
class VsdMount {
public:
    explicit VsdMount(const std::string& name);
    ~VsdMount();
    VsdMount(const VsdMount&) = delete;
    VsdMount& operator=(const VsdMount&) = delete;
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

}  // namespace riftwii::wii
