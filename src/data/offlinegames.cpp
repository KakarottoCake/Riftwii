// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/offlinegames.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace riftwii {
namespace {

// Four characters per game, sorted.
const char kCodes[] =
#include "offlinegames.inc"
    ;
constexpr std::size_t kCount = (sizeof(kCodes) - 1) / 4;

}  // namespace

bool game_has_no_online(const std::string& game_id) {
    if (game_id.size() < 4) return false;
    char code[4];
    for (int i = 0; i < 4; ++i) code[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(game_id[i])));
    std::size_t lo = 0, hi = kCount;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        const int by = std::memcmp(kCodes + mid * 4, code, 4);
        if (by == 0) return true;
        if (by < 0) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

}  // namespace riftwii
