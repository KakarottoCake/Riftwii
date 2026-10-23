// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "riftwii/zipread.hpp"

// zlib's raw inflate for the zip reader (riftwii/zipread.hpp) with zlib's
// memory taken from blocks the caller lends, in MEM2, instead of the MEM1
// heap. Used by the theme browser and the GameHacking.org cheats.
namespace riftwii::wii {

// zlib's state and window need about 40 KB; the chunk is what each inflate
// hands to the sink at a time.
constexpr std::size_t kZipZlibBytes = 64u << 10;
constexpr std::size_t kZipChunkBytes = 16u << 10;

// The blocks ZipInflate works in (kZipZlibBytes and kZipChunkBytes). They
// must stay valid while it is used; one inflate at a time.
void ZipInflateUse(std::uint8_t* zlib_block, std::uint8_t* chunk_block);

// A riftwii::ZipInflater: raw deflate data to `sink`, exactly `expected` bytes.
bool ZipInflate(const std::uint8_t* in, std::size_t in_size, std::uint64_t expected, const ZipSink& sink);

}  // namespace riftwii::wii
