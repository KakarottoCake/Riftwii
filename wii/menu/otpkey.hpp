// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

// The console's common key, which decrypts games' title keys
// (riftwii/wiicrypt.hpp), read from this Wii's own OTP memory
// (https://wiibrew.org/wiki/Hardware/OTP: words 5 to 8). RiftWii carries
// no key: it is read here when a banner is wanted, kept in memory only,
// and never logged or written anywhere. Needs AHBPROT off, as the
// Homebrew Channel starts apps.
namespace riftwii::wii {

// False without hardware access, or when the OTP reads back empty (an
// emulator that leaves it out).
bool ConsoleCommonKey(std::uint8_t out[16]);

}  // namespace riftwii::wii
