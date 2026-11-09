// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "riftwii/ctgpefa.hpp"

// The CTGP-R channel's own constants and tables that the native launch needs.
// They are CTGP-R's, not RiftWii's work, but they are not secrets either: the
// AES key and the chunk IV are four PPC instruction words from CTGP's own code,
// and the row table and course tables are plain CTGP data the developers agreed
// RiftWii may carry. So the launch no longer reads any of them from a file on
// the card (there is no efa.key, rows.bin or tables.bin): the keys and the row
// table are built in here, and the course and cup tables are read at run time
// from the player's own copy of CTGP-R (its blob.bin), the one input the launch
// still needs (docs/CTGPR.md).
namespace riftwii {

constexpr std::size_t kCtgpCourseWords = 32;
constexpr std::size_t kCtgpLayoutBytes = 216;
constexpr std::uint32_t kCtgpLayoutMaxGroups = kCtgpLayoutBytes / 4;

struct CtgpLayout {
    std::uint32_t groups = 0;  // groups of four tracks: 4 * groups bytes of `tracks` are used
    std::array<std::uint8_t, kCtgpLayoutBytes> tracks{};
};

struct CtgpTables {
    CtgpChunkIv efa_iv;
    CtgpEfbKeyWords efb_key;
    std::array<std::uint32_t, kCtgpCourseWords> course{};
    // cup_course_layout_using 0 and 1; layout 2 is always empty.
    std::array<CtgpLayout, 2> layouts;
};

// blob.bin's AES-128 key: four CTGP-R instruction words (not a secret).
std::array<std::uint8_t, 16> ctgp_blob_key();
// blob.bin's and jelly.bin's chunk IV: words 0, 1 and 3 (word 2 is the chunk's
// first sector), the same CTGP-R instruction words.
CtgpChunkIv ctgp_blob_chunk_iv();
// jelly.bin's per-file AES key words 1 and 3 (word 0 is the file's last-write
// time, word 2 its creation time): CTGP-R's own instruction words again.
CtgpEfbKeyWords ctgp_jelly_key_words();

// CTGP-R's row table, baked into the build (src/ctgpr/ctgprowdata.cpp, made by
// tools/ctgpr/gen_rowdata.py): "CRT0", the row count, the rows and 54 region
// words, big endian, as ctgp_parse_row_table reads them (ctgpimage.hpp).
const std::uint8_t* ctgp_embedded_row_table(std::size_t& size);

// The course table (32 words at 0x816ffaf8): the internal course IDs of Mario
// Kart Wii's 32 base tracks, read from the blob's /PACKAGES/CTGPR/CTGP.CRS. The
// file is "CRS", three header words, then entries of an index byte, a course-ID
// byte and a NUL-terminated name; the first 32 entries are the base tracks in
// grid order. False, with `error` in plain words, when the file is not one.
bool ctgp_course_from_crs(const std::uint8_t* data, std::size_t size,
                          std::array<std::uint32_t, kCtgpCourseWords>& out, std::string& error);

// One cup layout, read from a blob .cup file (/PACKAGES/CTGPR/alphabeticEN.cup
// or alphabeticJP.cup): "CUP2", a group count (at most 54), then count * 4
// course-ID bytes. False, with `error` in plain words, when it is not one.
bool ctgp_layout_from_cup(const std::uint8_t* data, std::size_t size, CtgpLayout& out, std::string& error);

}  // namespace riftwii
