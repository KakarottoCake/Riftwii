// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The card log the resident runtime keeps during a game (runtime/resident/
// rt_hook.h, struct rt_cardlog): one 512-byte sector of big-endian words
// noting what went wrong with the SD card, read back by the loader at its
// next start. The layout here mirrors rt_hook.h (tests/hook_tests.cpp
// checks the two agree).
namespace riftwii {

constexpr std::uint32_t kCardLogMagic = 0x52574344u;  // "RWCD"
constexpr std::uint32_t kCardLogVersion = 1;
constexpr std::size_t kCardLogBytes = 512;
constexpr std::size_t kCardLogRecords = 15;
constexpr std::uint32_t kCardLogNoWrite = 0xFFFFFFFFu;

struct CardLogEvent {
    unsigned kind = 0;      // 1 refused, 2 failed, 3 status failed, 4 wait timeout, 5 card error, 6 gave up, 7 mod read
    unsigned attempt = 0;   // 1-based try (0: not a save transfer)
    unsigned flags = 0;     // 1 write, 2 async, 4 a mod read in flight
    std::int32_t result = 0;
    std::uint32_t sector = 0;
    std::uint32_t count = 0;
    std::uint32_t status = 0;       // the card's R1 status
    std::uint32_t time = 0;         // time base (60.75 MHz)
    std::uint32_t since_write = 0;  // ticks since the last save write, kCardLogNoWrite: none
    std::uint32_t polls = 0;
};

struct CardLog {
    std::uint32_t events = 0;  // noted, kept or not
    std::uint32_t path = 0;    // 0 slot0 SDSC, 1 slot0 SDHC, 2 d2x
    std::uint32_t transfers = 0;
    std::uint32_t failures = 0;
    std::uint32_t retries = 0;
    std::uint32_t settle_polls = 0;
    std::vector<CardLogEvent> kept;
};

// False when `bytes` is not a card log (a blank sector, another version).
bool parse_card_log(const std::uint8_t* bytes, std::size_t size, CardLog& out);
// The log as text lines: a summary, then one line per kept event.
std::vector<std::string> describe_card_log(const CardLog& log);

}  // namespace riftwii
