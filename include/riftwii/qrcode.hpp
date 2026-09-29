// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A QR code for a short text (a problem report's link), so a phone can
// read it off the TV: byte mode, the lowest error correction (L), versions
// 1 to 4 (up to 78 bytes), each a single Reed-Solomon block. Written from
// the QR specification (ISO/IEC 18004).
namespace riftwii {

struct QrCode {
    int size = 0;                  // modules per side (21, 25, 29 or 33)
    std::vector<std::uint8_t> dark;  // size * size, row by row: 1 is dark
    int version = 0;
    int mask = 0;
    bool at(int x, int y) const { return dark[static_cast<std::size_t>(y) * size + x] != 0; }
};

// The smallest version that holds `text`, with the mask the specification's
// penalty rules pick; `force_mask` 0 to 7 picks that one instead (tests).
// False when the text is longer than 78 bytes.
bool make_qr(const std::string& text, QrCode& out, int force_mask = -1);

}  // namespace riftwii
