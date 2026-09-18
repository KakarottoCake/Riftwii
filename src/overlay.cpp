// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/overlay.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace riftwii {

ReadOverlay::ReadOverlay(const ByteSource& original, std::uint64_t virtual_size)
    : original_(original), size_(virtual_size) {}

bool ReadOverlay::append(OverlayExtent extent, std::string& error) {
    if (extents_.size() >= 4096) {
        error = "Maximum extent count reached";
        return false;
    }
    if (extent.source == nullptr) {
        error = "Null extent source";
        return false;
    }
    if (extent.length == 0) {
        error = "Empty extent rejected";
        return false;
    }
    if (extent.destination > size_ || extent.length > size_ - extent.destination) {
        error = "Extent destination exceeds virtual size";
        return false;
    }
    std::uint64_t source_size = extent.source->size();
    if (extent.source_offset > source_size || extent.length > source_size - extent.source_offset) {
        error = "Extent source range exceeds source size";
        return false;
    }
    try {
        extents_.push_back(extent);
    } catch (...) {
        error = "Allocation failure";
        return false;
    }
    return true;
}

std::uint64_t ReadOverlay::size() const {
    return size_;
}

bool ReadOverlay::read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const {
    if (offset > size_) {
        return false;
    }
    if (static_cast<std::uint64_t>(length) > size_ - offset) {
        return false;
    }
    if (length == 0) {
        return true;
    }
    if (destination == nullptr) {
        return false;
    }
    constexpr std::size_t kMaxReadSize = 16 * 1024 * 1024;
    if (length > kMaxReadSize) {
        return false;
    }

    std::vector<std::uint8_t> scratch;
    try {
        scratch.resize(length);
    } catch (...) {
        return false;
    }

    std::vector<std::uint64_t> boundaries;
    try {
        boundaries.push_back(offset);
        boundaries.push_back(offset + static_cast<std::uint64_t>(length));
        std::uint64_t orig_size = original_.size();
        if (orig_size > offset && orig_size < offset + static_cast<std::uint64_t>(length)) {
            boundaries.push_back(orig_size);
        }
        for (const auto& ext : extents_) {
            std::uint64_t ext_start = ext.destination;
            std::uint64_t ext_end = ext.destination + ext.length;
            if (ext_start > offset && ext_start < offset + static_cast<std::uint64_t>(length)) {
                boundaries.push_back(ext_start);
            }
            if (ext_end > offset && ext_end < offset + static_cast<std::uint64_t>(length)) {
                boundaries.push_back(ext_end);
            }
        }
        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    } catch (...) {
        return false;
    }

    for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) {
        std::uint64_t seg_start = boundaries[i];
        std::uint64_t seg_end = boundaries[i + 1];
        std::size_t seg_len = static_cast<std::size_t>(seg_end - seg_start);
        std::size_t seg_scratch_offset = static_cast<std::size_t>(seg_start - offset);

        const OverlayExtent* winner = nullptr;
        for (std::size_t k = extents_.size(); k > 0; --k) {
            const auto& ext = extents_[k - 1];
            std::uint64_t ext_start = ext.destination;
            std::uint64_t ext_end = ext.destination + ext.length;
            if (ext_start <= seg_start && ext_end >= seg_end) {
                winner = &ext;
                break;
            }
        }

        if (winner != nullptr) {
            std::uint64_t src_off = winner->source_offset + (seg_start - winner->destination);
            if (!winner->source->read(src_off, scratch.data() + seg_scratch_offset, seg_len)) {
                return false;
            }
        } else {
            std::uint64_t orig_size = original_.size();
            if (seg_start < orig_size) {
                if (!original_.read(seg_start, scratch.data() + seg_scratch_offset, seg_len)) {
                    return false;
                }
            } else {
                std::memset(scratch.data() + seg_scratch_offset, 0, seg_len);
            }
        }
    }

    std::memcpy(destination, scratch.data(), length);
    return true;
}

}
