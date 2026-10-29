// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "riftwii/contentbin.hpp"
#include "riftwii/wadfile.hpp"

// The console's common key, which decrypts games' title keys
// (riftwii/wiicrypt.hpp), read from this Wii's own OTP memory
// (https://wiibrew.org/wiki/Hardware/OTP: words 5 to 8). RiftWii carries
// no key: it is read here when a banner is wanted, kept in memory only,
// and never logged or written anywhere. Needs AHBPROT off, as the
// Homebrew Channel starts apps.
namespace riftwii::wii {

// False without hardware access or when OTP reads back empty. A nonzero
// result still needs content-hash verification (notably in emulators).
bool ConsoleCommonKey(std::uint8_t out[16]);

// One candidate title key. IOS decrypts the ticket's encrypted title key
// through ES without exporting the common key or installing a ticket.
// The caller must check the banner hash and clear the result after use.
bool ConsoleWadTitleKey(WadKeySource source, const std::vector<std::uint8_t>& ticket,
                        std::uint8_t out[16]);

// This Wii's console ID (OTP word 9) and PRNG key (words 26 to 29), which
// decrypt the channels its Wii Menu moved to the SD card
// (riftwii/contentbin.hpp). The same rules as the common key: read when
// wanted, never kept, logged or written. False without hardware access or
// when the console ID reads back 0; an all-zero PRNG key with a console ID
// is real (Dolphin's default console) and is returned.
bool ConsoleBackupKey(BackupKey& out);

// The 16-byte key this Wii's IOS59 encrypts a WFS drive with (Dragon
// Quest X's; riftwii/wfs.hpp), for reading what is installed on it. The
// same rules as the keys above, read from the dump left on the card
// (wfskey.cpp). False when there is none.
bool ConsoleWfsKey(std::uint8_t out[16]);

}  // namespace riftwii::wii
