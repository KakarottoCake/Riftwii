// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

// Own client for the IOS SD slot (/dev/sdio/slot0), for the phase after
// the IOS reload when libogc's SD driver is shut down: the loader brings
// the card up here and hands the fd to the resident runtime, which reads
// through the game's IPC with the same request format. The command set
// is the one documented at https://wiibrew.org/wiki//dev/sdio/slot0 in
// the order libogc's wiisd.c uses it (see NOTICE.md). Only cards IOS has
// already initialised are accepted (the usual case); the host-controller
// dance for cards IOS refuses is not implemented.
namespace riftwii::wii::sdio {

struct Card {
    std::int32_t fd = -1;
    std::uint16_t rca = 0;
    bool sdhc = false;      // CMD18 takes a sector number instead of a byte offset
    bool selected = false;  // in the transfer state, ready for CMD18
    bool d2x = false;       // the shared handle to d2x's /dev/sdio/sdhc (d2xsd.hpp), not slot0
};

// Opens the device, checks the card, selects it, sets 512-byte blocks,
// the 4-bit bus and the clock. Leaves the card selected.
bool open_card(Card& card, std::string& error);
// CMD18 into `buffer` (32-byte aligned, count * 512 bytes).
bool read_sectors(const Card& card, std::uint32_t sector, std::uint32_t count, void* buffer, std::string& error);
// Deselects and closes; not called across the handoff to the game.
void close_card(Card& card);

}  // namespace riftwii::wii::sdio
