// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/vsdbuild.hpp"

// Making a virtual SD card's image (wii/vsdimage.hpp) out of a code build
// on the SD card, on the Wii itself: the build's folder, the other top
// folders its codes load files from, the card's gameconfig.txt or gc.txt
// and the game's folder in private/wii/app go into riftwii/<name>.raw
// (in parts of 4000 MiB past 4 GiB, as riftwii/vsdparts.hpp reads them),
// laid out as they are on the card. The game then gets that image as its
// SD card, so it may be on the SD card itself.
namespace riftwii::wii {

struct VsdMakePlan {
    std::string key;                 // the build's: "Project+/codes/RSBE01.gct"
    std::string image;               // "Project+.raw"
    std::vector<std::string> tops;   // what goes in, as shown: "Project+", "gc.txt"
    riftwii::VsdPlan plan;
    std::uint64_t free_bytes = 0;    // on the SD card, counting an image replaced
    bool replaces = false;           // an image of that name is there already
    unsigned parts = 0;              // 0: one file
    bool beside = false;             // room to write it beside the image it replaces
    // The key the build will have in the image: "Project+.raw/Project+/codes/RSBE01.gct".
    std::string image_key() const { return image + "/" + key; }
};

// Lists what goes into an image of the build `key` (a code build on the
// SD card) for `game_id`, and plans the image. False, with `error` for
// the player, when it cannot be made.
bool PlanVsdMake(const std::string& key, const std::string& game_id, VsdMakePlan& out, std::string& error);

// Writes the image planned. `progress` (bytes written, the file being
// copied) is called every few megabytes; false from it stops. A stopped
// or failed image is deleted. An image it replaces is kept until the new
// one is whole when the card has room for both, else deleted first.
bool MakeVsdImage(const VsdMakePlan& plan,
                  const std::function<bool(std::uint64_t written, const std::string& path)>& progress,
                  std::string& error);

}  // namespace riftwii::wii
