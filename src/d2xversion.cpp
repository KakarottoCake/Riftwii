// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/d2xversion.hpp"

#include <cctype>
#include <cstring>

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

// A 16-byte field up to its first zero, printable characters only.
std::string field(const std::uint8_t* p) {
    std::string out;
    for (int i = 0; i < 16 && p[i] != 0; ++i)
        if (p[i] >= 0x20 && p[i] < 0x7F) out += static_cast<char>(p[i]);
    return out;
}

bool starts_with(const std::string& text, const char* prefix) {
    const std::size_t n = std::strlen(prefix);
    if (text.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(text[i])) != prefix[i]) return false;
    return true;
}

// The number after a prefix ("beta3" -> 3), -1 when there is none.
int number_after(const std::string& text, std::size_t from) {
    int value = -1;
    for (std::size_t i = from; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); ++i) {
        value = (value < 0 ? 0 : value) * 10 + (text[i] - '0');
        if (value > 100000) break;
    }
    return value;
}

}  // namespace

D2xInfo parse_d2x_info(const std::uint8_t* data, std::size_t size) {
    D2xInfo info;
    if (data == nullptr || size < 0x30) return info;
    if (be32(data) != 0x1ee7c105u || be32(data + 4) != 1) return info;
    if (!starts_with(field(data + 0x10), "d2x")) return info;
    info.d2x = true;
    const std::uint32_t major = be32(data + 8);
    info.major = major > 999 ? 999 : static_cast<int>(major);
    info.base = data[0x0F];
    info.release = field(data + 0x20);
    return info;
}

bool d2x_current(const D2xInfo& info) {
    if (!info.d2x) return false;
    if (info.major != kD2xMajorWanted) return info.major > kD2xMajorWanted;
    if (starts_with(info.release, "beta")) return number_after(info.release, 4) >= kD2xBetaWanted;
    if (starts_with(info.release, "alpha")) return false;
    return true;
}

std::string d2x_name(const D2xInfo& info) {
    std::string name = "d2x v" + std::to_string(info.major);
    if (!info.release.empty()) name += " " + info.release;
    return name;
}

}  // namespace riftwii
