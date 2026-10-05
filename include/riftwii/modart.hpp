// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A mod's picture on the Mods page: a PNG the player puts next to the
// mod, named after it, so there is nothing to look up or register:
//
//   sd:/riivolution/Newer.xml       sd:/riivolution/Newer.png
//   sd:/riftwii/pm.raw (or .001...) sd:/riftwii/pm.png
//   sd:/Project+/RSBE01.gct         sd:/Project+/cover.png
//   sd:/Brawl Minus/codes/RSBE01.gct  sd:/Brawl Minus/codes/cover.png,
//                                   else sd:/Brawl Minus/cover.png
namespace riftwii {

// Where a mod's picture may be, best first. `pack_path` is an XML pack's
// file ("sd:/riivolution/Newer.xml"); `gct_path` a code build's code file
// on the SD card ("sd:/Project+/RSBE01.gct"); `image_location` the
// virtual SD card a build is inside ("sd:/riftwii/pm.raw"), which wins.
std::vector<std::string> mod_art_paths(const std::string& pack_path, const std::string& gct_path,
                                       const std::string& image_location);

// An RGBA picture (rows, 4 bytes a pixel) fitted whole into a `bw` x `bh`
// box, as large as it goes, centred, the rest transparent.
std::vector<std::uint8_t> fit_art(const std::uint8_t* rgba, int w, int h, int bw, int bh);

}  // namespace riftwii
