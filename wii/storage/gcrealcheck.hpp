// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace riftwii::wii {

// What the check before a GameCube launch read from a real memory card.
struct GcRealCardCheck {
    int result = 0;                     // libogc's CARD_ERROR_* (0 or more: read)
    std::vector<std::uint8_t> system;   // the card's system area, when read
    unsigned bytes = 0;                 // the card's size in bytes, when opened
};

// Opens the card in `slot` (0: A, 1: B) and reads its system area on a
// thread of its own, waiting at most `timeout_ms`. libogc's probe and
// mount, and the reads after them, wait for the card without a time limit:
// a card that never answers froze the menu on a tester's Wii. A check that
// runs out of time is left running and reported as CARD_ERROR_NOCARD with
// `timed_out` set; while it still runs, later checks fail at once
// (CARD_ERROR_BUSY) instead of waiting on the same slot again.
// `waiting` is called about every 100 ms after the first second, so the
// caller can show that it waits.
GcRealCardCheck CheckGcRealCard(int slot, unsigned timeout_ms, bool& timed_out, void (*waiting)(void*),
                                void* context);

}  // namespace riftwii::wii
