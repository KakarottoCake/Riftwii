// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/sysfont.hpp"

#include <cstring>

namespace riftwii {

const std::uint8_t kWiiFontHash[20] = {0x32, 0xb3, 0x39, 0xcb, 0xbb, 0x50, 0x7d, 0x50, 0x27, 0x79,
                                       0x25, 0x9a, 0x78, 0x66, 0x99, 0x5d, 0x03, 0x0b, 0x1d, 0x88};
const std::uint8_t kWiiKoreanFontHash[20] = {0xb7, 0x15, 0x6d, 0xf0, 0xf4, 0xae, 0x07, 0x8f, 0xd1, 0x53,
                                             0x58, 0x3e, 0x93, 0x6e, 0x07, 0xc0, 0x98, 0x77, 0x49, 0x0e};

namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

bool ends_with(const std::string& s, const char* tail) {
    const std::size_t n = std::strlen(tail);
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        char c = s[s.size() - n + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != tail[i]) return false;
    }
    return true;
}

}  // namespace

std::string shared_content_name(const std::uint8_t* map, std::size_t size, const std::uint8_t hash[20]) {
    for (std::size_t at = 0; at + 28 <= size; at += 28)
        if (std::memcmp(map + at + 8, hash, 20) == 0) {
            std::string name(reinterpret_cast<const char*>(map + at), 8);
            for (char c : name)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return "";
            return name;
        }
    return "";
}

bool u8_find_font(const std::uint8_t* data, std::size_t size, std::size_t& offset, std::size_t& length) {
    if (size < 32 || be32(data) != 0x55AA382D) return false;
    const std::size_t root = be32(data + 4);
    if (root > size || size - root < 12) return false;
    // The root node's size is how many nodes there are, itself counted;
    // the names follow the last node.
    const std::size_t count = be32(data + root + 8);
    if (count == 0 || count > (size - root) / 12) return false;
    const std::size_t names = root + count * 12;
    for (std::size_t i = 1; i < count; ++i) {
        const std::uint8_t* node = data + root + i * 12;
        if (node[0] != 0) continue;  // a folder
        const std::size_t name_at = names + (be32(node) & 0xFFFFFF);
        if (name_at >= size) return false;
        const char* name = reinterpret_cast<const char*>(data + name_at);
        const void* nul = std::memchr(name, 0, size - name_at);
        const std::string file(name, nul ? static_cast<const char*>(nul) - name : size - name_at);
        if (!ends_with(file, ".ttf") && !ends_with(file, ".ttc")) continue;
        const std::size_t at = be32(node + 4), n = be32(node + 8);
        if (at > size || n > size - at || n < 4) return false;
        offset = at;
        length = n;
        return true;
    }
    return false;
}

}  // namespace riftwii
