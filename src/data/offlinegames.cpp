// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/offlinegames.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace riftwii {
namespace {

#include "offlinegames.inc"

// The regions with a bit in kPrefixes, in tools/make_offline_games.py's order.
constexpr char kRegions[] = "EPJXKDFY";

// The entry of `width` bytes in `table` (sorted, `count` entries) whose
// first `key_bytes` are `key`; null when none is.
const char* find(const char* table, std::size_t count, std::size_t width, const char* key, std::size_t key_bytes) {
    std::size_t lo = 0, hi = count;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        const int by = std::memcmp(table + mid * width, key, key_bytes);
        if (by == 0) return table + mid * width;
        if (by < 0) lo = mid + 1;
        else hi = mid;
    }
    return nullptr;
}

}  // namespace

bool game_has_no_online(const std::string& game_id) {
    if (game_id.size() < 4) return false;
    char code[4];
    for (int i = 0; i < 4; ++i) code[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(game_id[i])));
    if (const char* region = std::strchr(kRegions, code[3]); region != nullptr && code[3] != '\0') {
        const char* entry = find(kPrefixes, (sizeof(kPrefixes) - 1) / 4, 4, code, 3);
        return entry != nullptr && (static_cast<unsigned char>(entry[3]) >> (region - kRegions) & 1) != 0;
    }
    return find(kOtherCodes, (sizeof(kOtherCodes) - 1) / 4, 4, code, 4) != nullptr;
}

}  // namespace riftwii
