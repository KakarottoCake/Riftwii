// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "riftwii/ctgpdsa.hpp"
#include "riftwii/ctgpefa.hpp"

// The CTGP-R channel's own constants that the native launch needs: how its
// package disks are encrypted, the public key its signed lists are checked
// with, and the course tables its settings step copies into the game. They
// come from CTGP-R itself, so they are never part of RiftWii's source: the
// launch reads them at run time from sd:/riftwii/ctgpr/tables.bin, next to
// efa.key and rows.bin (docs/CTGPR.md says which files a player needs).
//
// The file, big endian, 804 bytes:
//   0x000  "CTB0"
//   0x004  blob.bin's chunk IV: words 0, 1 and 3 (word 2 is the chunk's
//          first sector)
//   0x010  jelly.bin's key: words 1 and 3 (word 0 is the file's last-write
//          time, word 2 its creation time)
//   0x018  the list-signing public key: p (64 bytes), q (20), g (64), y (64)
//   0x0EC  the course table: 32 words
//   0x16C  course layout 0: its group count (at most 54), then 216 bytes
//   0x248  course layout 1: the same
//   0x324  end
namespace riftwii {

constexpr std::size_t kCtgpTablesSize = 0x324;
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
    CtgpDsaKey list_key{};
    std::array<std::uint32_t, kCtgpCourseWords> course{};
    // cup_course_layout_using 0 and 1; layout 2 is always empty.
    std::array<CtgpLayout, 2> layouts;
};

// Reads the file. False, with `error` in plain words, when it is not one.
bool ctgp_parse_tables(const std::uint8_t* data, std::size_t size, CtgpTables& out, std::string& error);

}  // namespace riftwii
