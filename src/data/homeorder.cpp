// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/homeorder.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace riftwii {
namespace {

bool plausible_id(const std::string& id) {
    if (id.size() < 4 || id.size() > 6) return false;
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

// "usb:", "sd:" or "wii:", a game ID, and "/2" to "/9" for a later disc.
bool plausible_key(const std::string& key) {
    std::size_t at = 0;
    if (key.compare(0, 4, "usb:") == 0 || key.compare(0, 4, "wii:") == 0) at = 4;
    else if (key.compare(0, 3, "sd:") == 0) at = 3;
    else return false;
    const std::size_t slash = key.find('/', at);
    const std::string id = key.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
    if (!plausible_id(id)) return false;
    if (slash == std::string::npos) return true;
    const std::string disc = key.substr(slash + 1);
    return disc.size() == 1 && disc[0] >= '2' && disc[0] <= '9';
}

}  // namespace

std::string home_order_key(const std::string& place, const std::string& game_id, unsigned disc_number) {
    std::string key = place + ":" + game_id;
    // The header counts discs from 0; the file names the second disc "/2".
    if (disc_number > 0 && disc_number < 9) key += "/" + std::to_string(disc_number + 1);
    return key;
}

std::vector<std::string> parse_home_order(const std::string& text) {
    std::vector<std::string> order;
    std::set<std::string> seen;
    std::istringstream in(text);
    std::string line;
    while (order.size() < kHomeOrderMaxGames && std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (!plausible_key(line)) continue;
        if (!seen.insert(line).second) continue;
        order.push_back(line);
    }
    return order;
}

std::string home_order_whole_lines(const std::string& head, bool cut) {
    if (!cut) return head;
    const std::size_t lastNewline = head.rfind('\n');
    return lastNewline == std::string::npos ? std::string() : head.substr(0, lastNewline + 1);
}

std::string serialize_home_order(const std::vector<std::string>& order) {
    std::string out = "# RiftWii Home order: one game a line (drive:ID), the first tile first\n";
    const std::size_t count = std::min(order.size(), kHomeOrderMaxGames);
    for (std::size_t i = 0; i < count; ++i) out += order[i] + "\n";
    return out;
}

std::vector<std::size_t> arrange_home_order(const std::vector<std::string>& order, const std::vector<std::string>& keys) {
    std::map<std::string, std::size_t> place;
    for (std::size_t i = 0; i < order.size(); ++i) place.emplace(order[i], i);
    // A game the file does not list ranks after every listed one.
    const std::size_t unlisted = order.size();
    std::vector<std::size_t> rank(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto it = place.find(keys[i]);
        rank[i] = it == place.end() ? unlisted : it->second;
    }
    std::vector<std::size_t> shown(keys.size());
    for (std::size_t i = 0; i < shown.size(); ++i) shown[i] = i;
    std::stable_sort(shown.begin(), shown.end(), [&](std::size_t a, std::size_t b) { return rank[a] < rank[b]; });
    return shown;
}

std::vector<std::string> merge_home_order(const std::vector<std::string>& first, const std::vector<std::string>& rest) {
    std::vector<std::string> merged;
    std::set<std::string> seen;
    for (const std::vector<std::string>* part : {&first, &rest}) {
        for (const std::string& key : *part)
            if (seen.insert(key).second) merged.push_back(key);
    }
    return merged;
}

std::vector<std::string> refill_home_order(const std::vector<std::string>& order, const std::vector<std::string>& shown) {
    // The same game twice among the tiles (two copies on one drive) has
    // one place: its first tile's.
    std::vector<std::string> moved;
    std::set<std::string> movedSet;
    for (const std::string& key : shown)
        if (movedSet.insert(key).second) moved.push_back(key);
    std::vector<std::string> out = order;
    std::size_t next = 0;
    for (std::string& key : out) {
        if (movedSet.count(key) == 0) continue;
        if (next < moved.size()) key = moved[next++];
    }
    // Games shown that `order` did not name yet go at its end.
    std::set<std::string> placed(out.begin(), out.end());
    for (; next < moved.size(); ++next)
        if (placed.insert(moved[next]).second) out.push_back(moved[next]);
    return out;
}

}  // namespace riftwii
