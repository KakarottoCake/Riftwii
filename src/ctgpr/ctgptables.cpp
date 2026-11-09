// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgptables.hpp"

#include <cstring>

namespace riftwii {
namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

constexpr std::size_t kEfaIv = 0x004;
constexpr std::size_t kEfbKey = 0x010;
constexpr std::size_t kListKey = 0x018;
constexpr std::size_t kCourse = 0x0EC;
constexpr std::size_t kLayouts = 0x16C;
constexpr std::size_t kLayoutStride = 4 + kCtgpLayoutBytes;

}  // namespace

bool ctgp_parse_tables(const std::uint8_t* data, std::size_t size, CtgpTables& out, std::string& error) {
    if (data == nullptr || size != kCtgpTablesSize || std::memcmp(data, "CTB0", 4) != 0) {
        error = "tables.bin is not a CTGP-R tables file (it should start with CTB0 and be 804 bytes)";
        return false;
    }
    CtgpTables t;
    t.efa_iv.w0 = be32(data + kEfaIv);
    t.efa_iv.w1 = be32(data + kEfaIv + 4);
    t.efa_iv.w3 = be32(data + kEfaIv + 8);
    t.efb_key.w1 = be32(data + kEfbKey);
    t.efb_key.w3 = be32(data + kEfbKey + 4);
    const std::uint8_t* key = data + kListKey;
    std::memcpy(t.list_key.p, key, sizeof t.list_key.p);
    key += sizeof t.list_key.p;
    std::memcpy(t.list_key.q, key, sizeof t.list_key.q);
    key += sizeof t.list_key.q;
    std::memcpy(t.list_key.g, key, sizeof t.list_key.g);
    key += sizeof t.list_key.g;
    std::memcpy(t.list_key.y, key, sizeof t.list_key.y);
    for (std::size_t i = 0; i < kCtgpCourseWords; ++i) t.course[i] = be32(data + kCourse + 4 * i);
    for (std::size_t l = 0; l < t.layouts.size(); ++l) {
        const std::uint8_t* at = data + kLayouts + l * kLayoutStride;
        t.layouts[l].groups = be32(at);
        if (t.layouts[l].groups > kCtgpLayoutMaxGroups) {
            error = "tables.bin names more course groups than a layout holds";
            return false;
        }
        std::memcpy(t.layouts[l].tracks.data(), at + 4, kCtgpLayoutBytes);
    }
    out = t;
    return true;
}

}  // namespace riftwii
