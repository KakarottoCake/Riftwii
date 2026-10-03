// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/playhistory.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace riftwii {
namespace {

bool plausible_id(const std::string& id) {
    if (id.size() < 4 || id.size() > 6) return false;
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

}  // namespace

void PlayHistory::parse(const std::string& text) {
    games_.clear();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        std::string id, count, last;
        if (!std::getline(fields, id, '\t') || !std::getline(fields, count, '\t') || !std::getline(fields, last)) continue;
        if (!plausible_id(id)) continue;
        char* end = nullptr;
        const unsigned long c = std::strtoul(count.c_str(), &end, 10);
        if (end == count.c_str() || *end) continue;
        const long long t = std::strtoll(last.c_str(), &end, 10);
        if (end == last.c_str() || *end || t < 0) continue;
        PlayRecord& r = games_[id];
        r.count = static_cast<std::uint32_t>(std::min<unsigned long>(c, 0xFFFFFFFFul));
        r.last = t;
    }
}

std::string PlayHistory::serialize() const {
    std::string out = "# RiftWii play history: game ID, times started, last start (Unix time)\n";
    for (const auto& g : games_) {
        out += g.first + "\t" + std::to_string(g.second.count) + "\t" + std::to_string(g.second.last) + "\n";
    }
    return out;
}

void PlayHistory::record(const std::string& game_id, std::int64_t now) {
    if (!plausible_id(game_id)) return;
    PlayRecord& r = games_[game_id];
    if (r.count != 0xFFFFFFFFu) ++r.count;
    r.last = std::max<std::int64_t>(now, 0);
}

const PlayRecord* PlayHistory::find(const std::string& game_id) const {
    const auto it = games_.find(game_id);
    return it == games_.end() ? nullptr : &it->second;
}

std::vector<std::string> PlayHistory::recent(std::size_t limit) const {
    std::vector<std::pair<std::int64_t, std::string>> order;
    for (const auto& g : games_) order.push_back({g.second.last, g.first});
    std::stable_sort(order.begin(), order.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (const auto& o : order) {
        if (out.size() >= limit) break;
        out.push_back(o.second);
    }
    return out;
}

namespace {

void put_be(std::vector<std::uint8_t>& out, std::size_t at, std::uint64_t value, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) {
        out[at + i] = static_cast<std::uint8_t>(value & 0xFF);
        value >>= 8;
    }
}

// UTF-8 to UTF-16 code units; anything malformed becomes '?'.
std::vector<std::uint16_t> utf16_of(const std::string& s) {
    std::vector<std::uint16_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        std::uint32_t cp = extra == 0 ? c : extra == 1 ? (c & 0x1F) : extra == 2 ? (c & 0x0F) : (c & 0x07);
        bool ok = extra >= 0 && i + extra < s.size() + (extra == 0 ? 1 : 0);
        for (int k = 1; ok && k <= extra; ++k) {
            const unsigned char d = static_cast<unsigned char>(s[i + k]);
            ok = (d & 0xC0) == 0x80;
            cp = (cp << 6) | (d & 0x3F);
        }
        if (!ok) {
            out.push_back('?');
            ++i;
            continue;
        }
        i += 1 + extra;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<std::uint16_t>(0xD800 | (cp >> 10)));
            out.push_back(static_cast<std::uint16_t>(0xDC00 | (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<std::uint16_t>(cp));
        }
    }
    return out;
}

}  // namespace

std::vector<std::uint8_t> play_log_record(const std::string& utf8_name, const std::string& game_id,
                                          std::uint64_t ticks) {
    std::vector<std::uint8_t> out(kPlayLogBytes, 0);
    std::vector<std::uint16_t> name = utf16_of(utf8_name);
    if (name.size() > 39) {
        name.resize(39);
        // Not half a surrogate pair at the cut.
        if (name.back() >= 0xD800 && name.back() < 0xDC00) name.pop_back();
    }
    for (std::size_t i = 0; i < name.size(); ++i) put_be(out, 0x04 + 2 * i, name[i], 2);
    put_be(out, 0x58, ticks, 8);
    put_be(out, 0x60, ticks, 8);
    for (std::size_t i = 0; i < 6 && i < game_id.size(); ++i) out[0x68 + i] = static_cast<std::uint8_t>(game_id[i]);
    std::uint32_t sum = 0;
    for (std::size_t at = 0x04; at < kPlayLogBytes; at += 4) {
        sum += (std::uint32_t(out[at]) << 24) | (std::uint32_t(out[at + 1]) << 16) |
               (std::uint32_t(out[at + 2]) << 8) | out[at + 3];
    }
    put_be(out, 0, sum, 4);
    return out;
}

}  // namespace riftwii
