// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Which games were started from RiftWii, how often and when
// (sd:/riftwii/history.txt): "<ID>\t<count>\t<unix time>" lines. It
// drives the Home screen's "Recently played" view, opening on the last
// game played, and the game page's play count.
namespace riftwii {

struct PlayRecord {
    std::uint32_t count = 0;
    std::int64_t last = 0;  // seconds since 1970, the Wii's clock
};

class PlayHistory {
public:
    // Lenient: lines it cannot read are dropped.
    void parse(const std::string& text);
    std::string serialize() const;

    void record(const std::string& game_id, std::int64_t now);
    const PlayRecord* find(const std::string& game_id) const;
    // Game IDs, the most recently played first (at most `limit`).
    std::vector<std::string> recent(std::size_t limit = 64) const;
    std::size_t size() const { return games_.size(); }

private:
    std::map<std::string, PlayRecord> games_;
};

}  // namespace riftwii
