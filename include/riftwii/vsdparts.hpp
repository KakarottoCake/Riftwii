// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/redirect.hpp"

// A virtual SD card's image split in parts, the way 7-Zip's "Split to
// volumes" writes a file: rex.raw.001, rex.raw.002, ... for rex.raw, so a
// card image bigger than FAT32's 4 GB fits on a FAT32 drive. RiftWii
// reads the parts one after another as one image.
namespace riftwii {

constexpr unsigned kMaxVsdParts = 64;

// "rex.raw.001" for part 1 of "rex.raw".
std::string vsd_part_name(const std::string& image, unsigned part);

// "rex.raw" for a first part's name "rex.raw.001" (any case); "" for any
// other name.
std::string vsd_split_image(const std::string& file);

// One part: its size in bytes and where its bytes lie on the drive (the
// 512-byte sectors a file system's fragment list gives, which may run
// past the size to the end of its last cluster).
struct VsdPart {
    std::uint64_t size = 0;
    std::vector<Fragment> fragments;
};

// The parts' sectors as one image, in order, each part's cut to its size
// and touching pieces joined; `bytes` the image's size. False with `why`
// when a part's size is not whole 512-byte sectors (the parts would not
// line up) or its fragments are shorter than it.
bool join_vsd_parts(const std::vector<VsdPart>& parts, std::vector<Fragment>& out, std::uint64_t& bytes,
                    std::string& why);

}  // namespace riftwii
