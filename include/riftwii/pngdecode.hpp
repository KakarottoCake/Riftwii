// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A PNG reader for the menu's cover art, in place of libpng (whose
// simplified reader it matches for 8-bit output): every colour type and
// bit depth, Adam7 interlacing, tRNS transparency, and gAMA/sRGB gamma
// (applied the way libpng does only when it differs from sRGB's by more
// than 5%, and for 16-bit files, which libpng takes as linear unless a
// gAMA says otherwise). iCCP and cHRM are ignored, as libpng's simplified
// reader does. Critical chunks must have a good CRC; an ancillary one with
// a bad CRC is skipped.
namespace riftwii {

// Inflates the zlib stream `data` (the IDAT chunks, joined) into exactly
// `out_size` bytes at `out`; false when the stream is broken or short.
// Bytes past `out_size` are ignored, as libpng does.
using ZlibInflate = bool (*)(const std::uint8_t* data, std::size_t size, std::uint8_t* out, std::size_t out_size);

// Decodes `png` into 8-bit RGBA rows (not premultiplied). Images wider or
// taller than `max_side` are refused.
bool decode_png(const std::uint8_t* png, std::size_t size, ZlibInflate inflate, std::uint32_t max_side,
                std::vector<std::uint8_t>& rgba, std::uint32_t& width, std::uint32_t& height, std::string& error);

// The PNG CRC-32 (ISO 3309) of `size` bytes.
std::uint32_t png_crc32(const std::uint8_t* data, std::size_t size);

}  // namespace riftwii
