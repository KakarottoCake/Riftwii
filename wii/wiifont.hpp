// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2012-2025 Dimok
// SPDX-FileCopyrightText: 2012-2025 giantpune
// SPDX-FileCopyrightText: 2012-2025 blackb0x
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <cstddef>
#include <string>

namespace riftwii::wii {

// The Wii Menu's font, read from the NAND's shared contents
// (riftwii/sysfont.hpp) into MEM2 for the whole menu phase: Settings >
// Menu font. A pointer to the TrueType collection and its size, or nullptr
// with why it could not be read, for the log.
u8* LoadWiiMenuFont(std::size_t& size, std::string& why);

// Which face of the collection the menu draws with: the proportional one
// (Wii NTLG PGothic), as the Wii Menu does.
constexpr long kWiiMenuFontFace = 1;

}  // namespace riftwii::wii
