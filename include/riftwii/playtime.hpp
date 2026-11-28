// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "riftwii/profiles.hpp"  // tile_game_id

// Play time per game (#642). A Wii game's SDK keeps the Wii Menu's play log
// (play_rec.dat, riftwii/playhistory.hpp's layout): when the game started
// and when it was last seen running. At RiftWii's start the record of the
// game launched last is read (never changed, the Message Board still needs
// it) and the time between those two is added to that tile's total.
namespace riftwii {

// The emulated NAND folder the last launch ran in (one line), written by the
// launch and removed by the next RecordPlay; absent for the Wii's own NAND.
// Tells the next start where the play log is.
constexpr const char* kLastNandPath = "sd:/riftwii/last_nand.txt";

// Wii time base ticks per second (243 MHz / 4).
constexpr std::uint64_t kWiiTicksPerSecond = 60750000;
// A session longer than this is a clock gone wrong, not a play.
constexpr std::uint64_t kMaxSessionSeconds = 24 * 60 * 60;

struct PlayLogSession {
    std::string game_id;      // the six characters at 0x68
    std::uint64_t start = 0;  // ticks since 2000, at 0x58
    std::uint64_t last = 0;   // ticks since 2000, at 0x60
};

// The record's fields; false when it is shorter than a record or the ID is
// not six letters and digits.
bool parse_play_log(const std::uint8_t* data, std::size_t size, PlayLogSession& out);

enum class SessionVerdict {
    counted,     // `seconds` is the time to add
    other_game,  // a record of a game other than the tile launched last
    counted_already,  // this start time was added before
    backwards,   // it ends before it starts
    too_long,    // over 24 hours
};

// Whether the session belongs to the tile `tile_key` ("RMCE01" or
// "RMCE01@profile") and how many whole seconds it adds. `counted_start` is
// the start time added last for that tile (0 when none): the same start is
// never added twice, however often the record is read.
SessionVerdict judge_play_session(const PlayLogSession& session, const std::string& tile_key,
                                  std::uint64_t counted_start, std::uint64_t& seconds);

}  // namespace riftwii
