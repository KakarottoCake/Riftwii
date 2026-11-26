// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace riftwii::wii {

// Runtime blobs that ship as builds of one program (the Wii Remote
// logger's three, the WiiRD handlers' two) embedded together, as tools/pack_blobs.py
// writes them: each blob's size as a big-endian word, then one zstd frame
// of the blobs in that order. They share most of their code, so the frame
// keeps a fraction of the MEM1 they took apart. Unpacked only while a
// launch reads them, into a buffer that goes with its owner.
class PackedBlobs {
public:
    static constexpr int kMaxBlobs = 8;

    // False when the data is damaged or there is no memory to unpack it.
    bool unpack(const std::uint8_t* packed, std::size_t packed_size, int count);
    const std::uint8_t* blob(int index) const { return bytes_.data() + offsets_[index]; }
    std::uint32_t size(int index) const { return sizes_[index]; }

    // A blob's size as listed in front of the frame, without unpacking.
    static std::uint32_t listed_size(const std::uint8_t* packed, int index);

private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t offsets_[kMaxBlobs] = {};
    std::uint32_t sizes_[kMaxBlobs] = {};
};

}  // namespace riftwii::wii
