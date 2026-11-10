// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/ctgpfields.hpp"

// /PACKAGES.BIN on CTGP-R's package disk ("efa:"): a 16-byte header ("PAK0",
// total size, 0, record count) and 16-byte records (8-byte name, version,
// flags). The channel reads it at start-up (FUN_8087d768) and keeps the
// version of each package; the in-game code reads four of them from the
// loader block as the package ids.
namespace riftwii {

// The installed set has only a handful of packages. Bound metadata before
// growing its strings and vector, even for a structurally valid disk file.
constexpr std::uint32_t kCtgpMaxPackages = 64;
constexpr std::size_t kCtgpPackagesMaxBytes = (kCtgpMaxPackages + 1) * 16;

struct CtgpPackage {
    std::string name;            // up to 8 characters
    std::uint32_t version = 0;
    std::uint32_t flags = 0;
};

// Same checks as the channel: magic, size field == (count + 1) * 16, third
// header word 0, and the file holds the records it claims.
bool ctgp_parse_packages(const std::uint8_t* data, std::size_t size,
                         std::vector<CtgpPackage>& out, std::string& error);

// The version of the named package, 0 when it is missing (like the channel).
std::uint32_t ctgp_package_version(const std::vector<CtgpPackage>& packages, const std::string& name);

// 0x816ff9d8 CORE, 0x816ff9dc CTGPR, 0x816ff9e0 CHAN, 0x816ff9e4 MYSTUFF: each
// the package's version. 0x816ff9e8 (a USB HID device is open) is always 0
// here: the native launcher opens none.
std::vector<CtgpWordWrite> ctgp_package_id_writes(const std::vector<CtgpPackage>& packages);

}  // namespace riftwii
