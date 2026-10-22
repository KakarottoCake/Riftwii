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

// Where a `w` x `h` picture goes to fit whole inside the rectangle
// (`area_x`, `area_y`, `area_w`, `area_h`), keeping its shape and centred.
// A tile's picture is fitted to the tile's inside, never to a box that may be
// wider than the tile (a channel's 128 x 96 icon box is, and a wide 128 x 48
// app icon fitted to it spilled out of the tile).
struct FitRect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float scale = 0.0f;
};
inline FitRect fit_inside(float w, float h, float area_x, float area_y, float area_w, float area_h) {
    FitRect r;
    if (w <= 0.0f || h <= 0.0f || area_w <= 0.0f || area_h <= 0.0f) return r;
    const float by_width = area_w / w;
    const float by_height = area_h / h;
    r.scale = by_width < by_height ? by_width : by_height;
    r.w = w * r.scale;
    r.h = h * r.scale;
    r.x = area_x + (area_w - r.w) / 2.0f;
    r.y = area_y + (area_h - r.h) / 2.0f;
    return r;
}

}  // namespace riftwii
