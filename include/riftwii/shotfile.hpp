// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "riftwii/pngencode.hpp"

// Screenshots. The Wii's video interface shows an external framebuffer
// (XFB) of YUYV pixels: two pixels share four bytes, Y0 U Y1 V, with
// BT.601 studio-range values. The menu saves its own XFB as a PNG; a game's
// is copied by the in-game screenshot blob (runtime/shot) into a raw file
// on the NAND, which the menu turns into a PNG at its next start.
namespace riftwii {

// The raw file: a 64-byte big-endian header, then `lines` rows of
// `width` * 2 bytes.
constexpr std::uint32_t kShotMagic = 0x52575348u;  // "RWSH"
constexpr std::uint32_t kShotVersion = 1;
constexpr std::size_t kShotHeaderBytes = 64;
constexpr std::uint32_t kShotFlagDoubleLines = 1;  // a field-rendered picture: each line was shown twice
constexpr std::uint32_t kShotMaxWidth = 720;
constexpr std::uint32_t kShotMaxLines = 576;

struct ShotInfo {
    std::uint32_t width = 0;
    std::uint32_t lines = 0;
    std::uint32_t flags = 0;
    std::string game_id;      // up to 6 characters, printable
    std::uint32_t index = 0;  // the capture's number in its game session
};

// Checks the header of a raw file of `file_size` bytes.
bool parse_shot_header(const std::uint8_t* header, std::size_t file_size, ShotInfo& out, std::string& error);

// One row of `width` pixels (even) from YUYV to RGB.
void yuyv_row_to_rgb(const std::uint8_t* yuyv, std::uint32_t width, std::uint8_t* rgb);

// A YUYV picture (`lines` rows, `stride` bytes apart) as a PNG through
// `sink`, each row twice when `double_lines`.
bool write_yuyv_png(const std::uint8_t* yuyv, std::uint32_t width, std::uint32_t lines, std::size_t stride,
                    bool double_lines, PngWriter::Sink sink, void* user, void* work = nullptr);

// "RMCE01-0007.png" for prefix "RMCE01" (a game ID, or "riftwii" for the
// menu) and number 7. Finding a free number is the caller's.
std::string shot_file_name(const std::string& prefix, unsigned number);

}  // namespace riftwii
