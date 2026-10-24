// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "otpkey.hpp"

#include <cstdio>
#include <cstring>

#include "riftwii/wfstitle.hpp"

namespace riftwii::wii {

namespace {

// Where a dump of this Wii's WFS key is left on the card: the 16 bytes,
// or 32 hex digits (parse_wfs_key).
constexpr const char* kWfsKeyFile = "sd:/riftwii/wfskey.bin";
// A hex dump with spaces and line ends fits well inside this.
constexpr std::size_t kMaxKeyFileBytes = 128;

}  // namespace

// IOS59 keeps the key; RiftWii does not work it out itself, it takes the
// one a key dump left on the card. Without it a WFS drive reads as an
// encrypted drive RiftWii leaves alone, and Dragon Quest X still starts
// (its own boot program finds the drive); only the check that its
// install is on the drive is skipped. Read when wanted, never kept,
// logged or written, as the other keys.
bool ConsoleWfsKey(std::uint8_t out[16]) {
    FILE* file = std::fopen(kWfsKeyFile, "rb");
    if (file == nullptr) return false;
    std::uint8_t bytes[kMaxKeyFileBytes + 1];
    const std::size_t got = std::fread(bytes, 1, sizeof bytes, file);
    std::fclose(file);
    const bool ok = got <= kMaxKeyFileBytes && parse_wfs_key(bytes, got, out);
    std::memset(bytes, 0, sizeof bytes);
    return ok;
}

}  // namespace riftwii::wii
