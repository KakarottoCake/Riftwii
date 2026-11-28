// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include "riftwii/profiles.hpp"  // tile_game_id

// Play time per game (#642). A Wii game's SDK keeps the Wii Menu's play log
// (play_rec.dat, riftwii/playhistory.hpp's layout): when the game started
// and when it was last seen running. Once after a launch, at RiftWii's next
// start, the record is read (never changed, the Message Board still needs
// it) and, if it is that launch's own, the time between those two is added
// to the launched tile's total.
namespace riftwii {

// What a launch leaves for the next start (sd:/riftwii/playtime_pending.txt):
// the tile, when it was launched (Unix time) and the emulated NAND folder the
// game ran in, if any. Read once and removed at the next start, whatever
// the result, so a launch is looked at once.
constexpr const char* kPlayMarkerPath = "sd:/riftwii/playtime_pending.txt";
struct PlayMarker {
    std::string key;
    std::int64_t launch = 0;
    std::string emu_nand;  // empty: the Wii's own NAND
};
std::string serialize_play_marker(const PlayMarker& marker);
bool parse_play_marker(const std::string& text, PlayMarker& out);

// The totals (sd:/riftwii/playtime.txt), apart from history.txt so a build
// without this feature, which rewrites history.txt whole, cannot erase them:
// "<tile key>\t<seconds>\t<start of the session added last, Wii ticks>".
constexpr const char* kPlayTotalsPath = "sd:/riftwii/playtime.txt";
struct PlayTotal {
    std::uint64_t seconds = 0;
    std::uint64_t session_start = 0;
};
class PlayTotals {
public:
    void parse(const std::string& text);  // lenient: lines it cannot read are dropped
    std::string serialize() const;
    const PlayTotal* find(const std::string& key) const;
    // Saturates; remembers the session's start.
    void add(const std::string& key, std::uint64_t seconds, std::uint64_t session_start);
    void forget(const std::string& key) { totals_.erase(key); }
    std::size_t size() const { return totals_.size(); }

private:
    std::map<std::string, PlayTotal> totals_;
};

// Wii time base ticks per second (243 MHz / 4), and the Wii's 2000 epoch
// in Unix seconds.
constexpr std::uint64_t kWiiTicksPerSecond = 60750000;
constexpr std::uint64_t kWiiEpochUnix = 946684800;
// A session longer than this is a clock gone wrong, not a play.
constexpr std::uint64_t kMaxSessionSeconds = 24 * 60 * 60;
// A record may start this much before the launch time (clock steps).
constexpr std::int64_t kLaunchToleranceSeconds = 120;
// ... and at most this much after it (a clock jumped forwards is no play).
constexpr std::int64_t kLaunchWindowSeconds = 24 * 60 * 60;

struct PlayLogSession {
    std::string game_id;      // the six characters at 0x68
    std::uint64_t start = 0;  // ticks since 2000, at 0x58
    std::uint64_t last = 0;   // ticks since 2000, at 0x60
};

// The record's fields; false when it is shorter than a record or the ID is
// not six letters and digits.
bool parse_play_log(const std::uint8_t* data, std::size_t size, PlayLogSession& out);

enum class SessionVerdict {
    counted,          // `seconds` is the time to add
    other_game,       // a record of a game other than the tile launched
    before_launch,    // it started before the launch: an older play's
    after_launch,     // it started over 24 hours after the launch: a clock jump
    counted_already,  // this start time was added before
    no_start,         // its start time is 0
    backwards,        // it ends before it starts
    too_long,         // over 24 hours
};

// Whether the session belongs to the launch of `tile_key` at `launch_unix`
// and how many whole seconds it adds. `counted_start` is the start time
// added last for that tile (0 when none): the same start is never added
// twice.
SessionVerdict judge_play_session(const PlayLogSession& session, const std::string& tile_key,
                                  std::uint64_t counted_start, std::int64_t launch_unix, std::uint64_t& seconds);

}  // namespace riftwii
