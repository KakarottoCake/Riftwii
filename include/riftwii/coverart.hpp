// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Box art for the menu, from GameTDB (https://www.gametdb.com): the
// front cover, shrunk once when it is downloaded into a ready GX texture
// on the SD card (wii/covers.cpp), so drawing a page of covers is a few
// small reads and no image decoding.
namespace riftwii {

// The stored size: half of GameTDB's 160x224 covers.
constexpr int kCoverWidth = 80;
constexpr int kCoverHeight = 112;
// "RWC1", the width and the height (big-endian 16-bit), then the pixels:
// GX RGB5A3 in 4x4 tiles, the corners rounded off with transparency.
constexpr std::size_t kCoverHeaderSize = 8;
constexpr std::size_t kCoverPixelBytes = static_cast<std::size_t>(kCoverWidth) * kCoverHeight * 2;
constexpr std::size_t kCoverFileSize = kCoverHeaderSize + kCoverPixelBytes;

// GameTDB's cover regions to try for a game, best first: its own region
// (for European games, the menu language's when GameTDB has one), then
// EN, US and JA.
std::vector<std::string> cover_regions(const std::string& game_id, const std::string& menu_language);
std::string cover_url(const std::string& region, const std::string& game_id);

// An RGBA image (rows, 4 bytes a pixel) resized to dw x dh, each target
// pixel the average of the source area it covers.
std::vector<std::uint8_t> scale_rgba(const std::uint8_t* src, int sw, int sh, int dw, int dh);

// The stored cover for an RGBA image of any size; empty when the size is
// unusable.
std::vector<std::uint8_t> make_cover_file(const std::uint8_t* rgba, int w, int h);
// True when `header` (kCoverHeaderSize bytes) starts a stored cover.
bool cover_header_valid(const std::uint8_t* header);

}  // namespace riftwii
