// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/casecolour.hpp"

#include <cctype>
#include <cstring>

namespace riftwii {
namespace {

// Twelve characters per game, sorted: the ID, then rrggbb.
const char kColours[] =
#include "casecolours.inc"
    ;
constexpr std::size_t kCount = (sizeof(kColours) - 1) / 12;

int Hex(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }

}  // namespace

bool game_case_colour(const std::string& game_id, std::uint8_t (&rgb)[3]) {
    if (game_id.size() != 6) return false;
    char id[6];
    for (int i = 0; i < 6; ++i) id[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(game_id[i])));
    std::size_t lo = 0, hi = kCount;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        const char* entry = kColours + mid * 12;
        const int by = std::memcmp(entry, id, 6);
        if (by == 0) {
            for (int i = 0; i < 3; ++i) rgb[i] = static_cast<std::uint8_t>(Hex(entry[6 + i * 2]) * 16 + Hex(entry[7 + i * 2]));
            return true;
        }
        if (by < 0) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

}  // namespace riftwii
