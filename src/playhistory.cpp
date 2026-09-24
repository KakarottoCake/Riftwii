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

}  // namespace riftwii
