// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "packedblobs.hpp"

#include "riftwii/checkedalloc.hpp"
#include "zstd.h"

namespace riftwii::wii {

std::uint32_t PackedBlobs::listed_size(const std::uint8_t* packed, int index) {
    const std::uint8_t* p = packed + 4 * index;
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

bool PackedBlobs::unpack(const std::uint8_t* packed, std::size_t packed_size, int count) {
    const std::size_t table_bytes = 4 * static_cast<std::size_t>(count);
    if (count <= 0 || count > kMaxBlobs || packed_size < table_bytes) return false;
    std::size_t total = 0;
    for (int i = 0; i < count; ++i) {
        const std::uint32_t size = listed_size(packed, i);
        // Each blob starts word-aligned, so its header can be read in place.
        if (size % 4 != 0 || size > (1u << 20)) return false;
        offsets_[i] = static_cast<std::uint32_t>(total);
        sizes_[i] = size;
        total += size;
    }
    const std::uint8_t* frame = packed + table_bytes;
    const std::size_t frame_size = packed_size - table_bytes;
    if (ZSTD_getFrameContentSize(frame, frame_size) != total || !try_resize(bytes_, total)) return false;
    const std::size_t got = ZSTD_decompress(bytes_.data(), bytes_.size(), frame, frame_size);
    return !ZSTD_isError(got) && got == total;
}

}  // namespace riftwii::wii
