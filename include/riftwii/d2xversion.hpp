// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace riftwii {

// The information block d2x puts at the start of a cIOS slot's first
// content (wiidev/d2x-cios, data/ciosmaps.xml): the magic 0x1ee7c105,
// version 1, the major version ("v11"), the base IOS in the fourth word,
// "d2x" at 0x10 and the release ("beta3") at 0x20, 16 bytes each.
// A cIOS that is not d2x (Waninkoko's, Hermes') has no such block.
struct D2xInfo {
    bool d2x = false;    // the block is there and names d2x
    int major = 0;       // 11 for d2x-v11-beta3
    int base = 0;        // the base IOS: 56 for a slot 249 made from IOS56
    std::string release; // "beta3"
};

// The block from the content's first bytes (at least 0x30 of them).
D2xInfo parse_d2x_info(const std::uint8_t* data, std::size_t size);

// The oldest d2x RiftWii runs SD and USB games on: d2x-v11-beta3
// (2025-03-22, the latest at https://github.com/wiidev/d2x-cios/releases).
// Older ones are not supported, nor are cIOSes that are not d2x.
constexpr int kD2xMajorWanted = 11;
constexpr int kD2xBetaWanted = 3;
constexpr const char* kD2xWantedName = "d2x v11 beta3";

// Whether `info` is d2x v11 beta3 or newer: a later major version, or v11
// with beta 3 or later (a release that is not a beta or an alpha, such as
// a final one, counts as newer than every beta).
bool d2x_current(const D2xInfo& info);

// "d2x v10 beta52", "d2x v11" (no release given).
std::string d2x_name(const D2xInfo& info);

}  // namespace riftwii
