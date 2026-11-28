// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/playtime.hpp"

namespace riftwii {
namespace {

std::uint64_t be64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

}  // namespace

bool parse_play_log(const std::uint8_t* data, std::size_t size, PlayLogSession& out) {
    if (data == nullptr || size < 0x80) return false;
    std::string id;
    for (int i = 0; i < 6; ++i) {
        const char c = static_cast<char>(data[0x68 + i]);
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return false;
        id += c;
    }
    out.game_id = id;
    out.start = be64(data + 0x58);
    out.last = be64(data + 0x60);
    return true;
}

SessionVerdict judge_play_session(const PlayLogSession& session, const std::string& tile_key,
                                  std::uint64_t counted_start, std::uint64_t& seconds) {
    seconds = 0;
    if (session.game_id.empty() || session.game_id != tile_game_id(tile_key)) return SessionVerdict::other_game;
    if (session.last < session.start) return SessionVerdict::backwards;
    if (counted_start != 0 && session.start == counted_start) return SessionVerdict::counted_already;
    const std::uint64_t secs = (session.last - session.start) / kWiiTicksPerSecond;
    if (secs > kMaxSessionSeconds) return SessionVerdict::too_long;
    seconds = secs;
    return SessionVerdict::counted;
}

}  // namespace riftwii
