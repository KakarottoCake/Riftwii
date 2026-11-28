// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/playtime.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>

#include "riftwii/textlines.hpp"

namespace riftwii {
namespace {

std::uint64_t be64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

bool number(const std::string& text, unsigned long long& out) {
    if (text.empty() || text[0] < '0' || text[0] > '9') return false;
    char* end = nullptr;
    errno = 0;
    out = std::strtoull(text.c_str(), &end, 10);
    return end != text.c_str() && *end == '\0' && errno != ERANGE;  // too big a number is no number
}

}  // namespace

std::string serialize_play_marker(const PlayMarker& m) {
    return m.key + "\n" + std::to_string(m.launch) + "\n" + m.emu_nand + "\n";
}

bool parse_play_marker(const std::string& text, PlayMarker& out) {
    LineReader in(text);
    std::string key, launch, nand;
    if (!in.next(key) || !in.next(launch)) return false;
    in.next(nand);
    for (std::string* s : {&key, &launch, &nand})
        if (!s->empty() && s->back() == '\r') s->pop_back();
    unsigned long long t = 0;
    if (!valid_tile_key(key) || !number(launch, t) || t == 0 || t > 0x7FFFFFFFFFFFFFFFull) return false;
    out.key = key;
    out.launch = static_cast<std::int64_t>(t);
    out.emu_nand = nand;
    return true;
}

void PlayTotals::parse(const std::string& text) {
    totals_.clear();
    LineReader in(text);
    std::string line;
    while (in.next(line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        LineReader fields(line);
        std::string key, seconds, start;
        if (!fields.next(key, '\t') || !fields.next(seconds, '\t') || !fields.next(start)) continue;
        unsigned long long s = 0, t = 0;
        if (!valid_tile_key(key) || !number(seconds, s) || !number(start, t)) continue;
        totals_[key] = PlayTotal{s, t};
    }
}

std::string PlayTotals::serialize() const {
    std::string out = "# RiftWii play time: tile key, seconds played, start of the session added last (Wii ticks)\n";
    for (const auto& t : totals_)
        out += t.first + "\t" + std::to_string(t.second.seconds) + "\t" + std::to_string(t.second.session_start) + "\n";
    return out;
}

const PlayTotal* PlayTotals::find(const std::string& key) const {
    const auto it = totals_.find(key);
    return it == totals_.end() ? nullptr : &it->second;
}

void PlayTotals::add(const std::string& key, std::uint64_t seconds, std::uint64_t session_start) {
    PlayTotal& t = totals_[key];
    t.seconds += std::min<std::uint64_t>(seconds, UINT64_MAX - t.seconds);
    t.session_start = session_start;
}

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
                                  std::uint64_t counted_start, std::int64_t launch_unix, std::uint64_t& seconds) {
    seconds = 0;
    if (session.game_id.empty() || session.game_id != tile_game_id(tile_key)) return SessionVerdict::other_game;
    // A start of 0 is no start (and 0 means "none counted" in the totals): a record
    // never written by a game's SDK, or one cleared.
    if (session.start == 0) return SessionVerdict::no_start;
    if (session.last < session.start) return SessionVerdict::backwards;
    if (launch_unix > 0) {
        const std::uint64_t start_unix = session.start / kWiiTicksPerSecond + kWiiEpochUnix;
        const std::int64_t need = launch_unix - kLaunchToleranceSeconds;
        if (need > 0 && start_unix < static_cast<std::uint64_t>(need)) return SessionVerdict::before_launch;
        if (start_unix > static_cast<std::uint64_t>(launch_unix + kLaunchWindowSeconds)) return SessionVerdict::after_launch;
    }
    if (counted_start != 0 && session.start == counted_start) return SessionVerdict::counted_already;
    const std::uint64_t secs = (session.last - session.start) / kWiiTicksPerSecond;
    if (secs > kMaxSessionSeconds) return SessionVerdict::too_long;
    seconds = secs;
    return SessionVerdict::counted;
}

}  // namespace riftwii
