// SPDX-FileCopyrightText: 2026 RiftWii contributors
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

// The box for the Home shelf, from GameTDB's full cover (back, spine and
// front in one picture, e.g. 1024x680): the spine and the front, side by
// side in one stored texture, so a game on the shelf is one read.
constexpr int kBoxSpineWidth = 16;
constexpr int kBoxFrontWidth = 128;
constexpr int kBoxBackWidth = 128;
constexpr int kBoxWidth = kBoxSpineWidth + kBoxFrontWidth + kBoxBackWidth;
constexpr int kBoxHeight = 176;
// "RWB2", the width and the height (big-endian 16-bit), then the pixels:
// GX RGB5A3 in 4x4 tiles: the spine in the first kBoxSpineWidth columns,
// then the front, then the back.
constexpr std::size_t kBoxHeaderSize = 8;
constexpr std::size_t kBoxPixelBytes = static_cast<std::size_t>(kBoxWidth) * kBoxHeight * 2;
constexpr std::size_t kBoxFileSize = kBoxHeaderSize + kBoxPixelBytes;

std::string coverfull_url(const std::string& region, const std::string& game_id);

// Where the spine is across a full cover `w` x `h`: [x0, x1). A Wii case's
// front and back are 135 mm wide and 190 mm tall, the spine between them
// about 14 mm; a picture whose middle strip is far off that gets a 14 mm
// strip in its centre. False when the picture can't be a full cover.
bool spine_strip(int w, int h, int& x0, int& x1);

// The stored box for a full cover; empty when it can't be one.
std::vector<std::uint8_t> make_box_file(const std::uint8_t* rgba, int w, int h);
bool box_header_valid(const std::uint8_t* header);

}  // namespace riftwii
