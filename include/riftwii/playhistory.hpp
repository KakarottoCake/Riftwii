// SPDX-FileCopyrightText: 2026 RiftWii contributors
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
//
// The play time of a tile is kept in a "#playtime\t<key>\t<seconds>\t<start>"
// line after the others. A tab-separated fourth column on the game's own
// line would make builds before this one drop that line (their time field
// must end the line), losing the game from the history; a comment line is
// skipped by them whole.
namespace riftwii {

struct PlayRecord {
    std::uint32_t count = 0;
    std::int64_t last = 0;  // seconds since 1970, the Wii's clock
    // Play time (#642), from the Wii Menu's play log (riftwii/playtime.hpp):
    // the seconds added so far, and the start time (Wii ticks) of the
    // session added last, so a session is counted once.
    std::uint64_t play_seconds = 0;
    std::uint64_t session_start = 0;
};

class PlayHistory {
public:
    // Lenient: lines it cannot read are dropped.
    void parse(const std::string& text);
    std::string serialize() const;

    // `game_id` may also be a profile's tile key ("RMCE01@retro-rewind").
    void record(const std::string& game_id, std::int64_t now);
    void forget(const std::string& game_id);
    // Adds a played session to a tile already in the history (nothing for
    // another) and remembers its start time. Saturates.
    bool add_play_time(const std::string& game_id, std::uint64_t seconds, std::uint64_t session_start);
    const PlayRecord* find(const std::string& game_id) const;
    // Game IDs, the most recently played first (at most `limit`).
    std::vector<std::string> recent(std::size_t limit = 64) const;
    std::size_t size() const { return games_.size(); }

private:
    void parse_play_time_line(const std::string& line);
    std::map<std::string, PlayRecord> games_;
};

// The automatic start uses the history while it is enabled. With history
// off, only last_tile.txt's first line is used; missing or invalid text
// never falls back to an older game recorded before history was disabled.
std::string last_game_key(const PlayHistory& history, bool history_enabled, const std::string& last_tile_text);

// The Wii Menu's play log, /title/00000001/00000002/data/play_rec.dat
// (layout as wiibrew documents it): 0x80 bytes, big-endian. A checksum
// (the sum of the 31 words after it); at 0x04 the game's name in UTF-16
// (40 units, the last a terminator); at 0x58 the start time and at 0x60
// the time last seen (both `ticks`, Wii time base ticks since 2000; the
// 64-bit fields are 8-byte aligned, as a record a game left in Dolphin's
// NAND shows); at 0x68 the six-character game ID; zeros. The game's SDK moves "last seen" on while it runs; the Wii
// Menu turns the record into a Message Board entry with the time played.
constexpr std::size_t kPlayLogBytes = 0x80;
std::vector<std::uint8_t> play_log_record(const std::string& utf8_name, const std::string& game_id,
                                          std::uint64_t ticks);

}  // namespace riftwii
