// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/stashimage.hpp"

namespace riftwii {
namespace {

constexpr char kMagic[8] = {'R', 'W', 'S', 'T', 'A', 'S', 'H', '1'};

std::uint32_t Read32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

// A NUL-padded field of `width` bytes equal to `text`. Plain loops: crt0
// runs this before the C library's state is set up.
bool FieldEquals(const std::uint8_t* field, std::size_t width, const char* text) {
    std::size_t i = 0;
    for (; i < width && text[i] != '\0'; ++i) {
        if (field[i] != static_cast<std::uint8_t>(text[i])) return false;
    }
    if (text[i] != '\0') return false;  // longer than the field
    for (; i < width; ++i) {
        if (field[i] != 0) return false;
    }
    return true;
}

}  // namespace

std::uint32_t stash_checksum(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

std::uint32_t stash_check_shape(const std::uint8_t* image, std::size_t available, const char* version) {
    if (image == nullptr || available < kStashHeaderBytes) return 0;
    for (std::size_t i = 0; i < sizeof(kMagic); ++i) {
        if (image[i] != static_cast<std::uint8_t>(kMagic[i])) return 0;
    }
    const std::uint32_t total = Read32(image + 8);
    const std::uint32_t count = Read32(image + 16);
    if (total < kStashHeaderBytes || total > kStashMaxBytes || total > available) return 0;
    if (count > kStashMaxEntries) return 0;
    const std::uint32_t directory_end = kStashHeaderBytes + count * kStashEntryBytes;
    if (directory_end > total) return 0;
    if (!FieldEquals(image + 20, kStashVersionBytes, version)) return 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* entry = image + kStashHeaderBytes + i * kStashEntryBytes;
        if (entry[0] == 0 || entry[kStashNameBytes - 1] != 0) return 0;  // empty or unterminated name
        const std::uint32_t offset = Read32(entry + kStashNameBytes);
        const std::uint32_t size = Read32(entry + kStashNameBytes + 4);
        if (offset < directory_end || offset > total || size > total - offset) return 0;
    }
    return total;
}

std::uint32_t stash_check(const std::uint8_t* image, std::size_t available, const char* version) {
    const std::uint32_t total = stash_check_shape(image, available, version);
    if (total == 0) return 0;
    if (stash_checksum(image + 16, total - 16) != Read32(image + 12)) return 0;
    return total;
}

bool stash_find(const std::uint8_t* image, const char* name, const std::uint8_t*& data, std::uint32_t& size) {
    const std::uint32_t count = Read32(image + 16);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* entry = image + kStashHeaderBytes + i * kStashEntryBytes;
        if (!FieldEquals(entry, kStashNameBytes, name)) continue;
        data = image + Read32(entry + kStashNameBytes);
        size = Read32(entry + kStashNameBytes + 4);
        return true;
    }
    return false;
}

}  // namespace riftwii
