// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcdiscs.hpp"

#include <algorithm>
#include <cstring>

namespace riftwii {

namespace {

constexpr std::uint32_t kGcMagic = 0xC2339F3Du;  // a GameCube disc header's word at 0x1C
constexpr std::size_t kDiscNumber = 6;
constexpr std::size_t kFstOffset = 0x424;
constexpr std::size_t kFstSize = 0x428;
constexpr std::size_t kFstMax = 0x42C;

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::string id_of(const std::uint8_t* header) { return std::string(reinterpret_cast<const char*>(header), 6); }

}  // namespace

bool gc_second_disc_matches(const std::uint8_t* first, const std::uint8_t* second, std::uint64_t second_bytes,
                            std::string& why) {
    if (be32(first + 0x1C) != kGcMagic || be32(second + 0x1C) != kGcMagic) {
        why = "it is not a GameCube disc";
        return false;
    }
    if (std::memcmp(first, second, 6) != 0) {
        why = "its game ID " + id_of(second) + " is not disc 1's " + id_of(first);
        return false;
    }
    if (first[kDiscNumber] != 0) {
        why = "the game was started from its disc " + std::to_string(first[kDiscNumber] + 1) + ", not disc 1";
        return false;
    }
    if (second[kDiscNumber] != 1) {
        why = "its header says it is disc " + std::to_string(second[kDiscNumber] + 1) + ", not disc 2";
        return false;
    }
    const std::uint32_t fst_offset = be32(second + kFstOffset);
    const std::uint32_t fst_size = be32(second + kFstSize);
    if (fst_size == 0 || fst_size > kGcSecondDiscFstMax) {
        why = "its FST's size (" + std::to_string(fst_size) + " bytes) is not one a disc has";
        return false;
    }
    if (std::uint64_t(fst_offset) + fst_size > second_bytes) {
        why = "its FST lies past the image's end (cut short?)";
        return false;
    }
    return true;
}

bool gc_image_name_is_rvz(const std::string& path) {
    static const char kSuffix[] = ".rvz";
    const std::size_t length = sizeof(kSuffix) - 1;
    if (path.size() < length) return false;
    for (std::size_t i = 0; i < length; ++i) {
        char c = path[path.size() - length + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != kSuffix[i]) return false;
    }
    return true;
}

std::uint32_t gc_two_disc_fst_room(std::uint32_t first_room, const std::uint8_t* second) {
    // The header's "FST max" is only a hint (a disc may leave it 0 or
    // anything); its own FST's size is what the game reads. Each bounded
    // by kGcSecondDiscFstMax (gc_second_disc_matches checked the size).
    const std::uint32_t fst_size = std::min(be32(second + kFstSize), kGcSecondDiscFstMax);
    const std::uint32_t fst_max = std::min(be32(second + kFstMax), kGcSecondDiscFstMax);
    return std::max({first_room, fst_size, fst_max});
}

}  // namespace riftwii
