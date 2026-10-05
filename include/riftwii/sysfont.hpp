// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The Wii Menu's own font, for Settings > Menu font. It is a shared
// content in the NAND: /shared1/content.map lists each one as an 8-letter
// name ("0000000f") and the SHA-1 of its data, and the font's content is a
// U8 archive holding one TrueType collection (WiiNTLG-Regular.ttc, or the
// Korean Wii's own). Nothing of it ships with RiftWii: it is read from the
// player's Wii.
namespace riftwii {

// SHA-1 of the font content on every Wii but a Korean one, and on a Korean Wii.
extern const std::uint8_t kWiiFontHash[20];
extern const std::uint8_t kWiiKoreanFontHash[20];

// The name in content.map's text (28-byte entries: 8 letters, then the
// SHA-1) of the content with this hash, or "" when none has it.
std::string shared_content_name(const std::uint8_t* map, std::size_t size, const std::uint8_t hash[20]);

// Where the first .ttf or .ttc file of a U8 archive is, as an offset in the
// archive and a size. False when the archive is not U8, has no font, or a
// node points past its end.
bool u8_find_font(const std::uint8_t* data, std::size_t size, std::size_t& offset, std::size_t& length);

}  // namespace riftwii
