// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "riftwii/gclayout.hpp"

namespace riftwii {

// The clocks a GameCube game's timing rests on, and keeping them true in
// Wii mode (docs/ARCHITECTURE.md, "Clock speed").
//
// The GameCube SDK reads the bus and CPU clocks from low memory
// (0x800000F8 and 0x800000FC) and turns time base ticks into time with
// them: the time base counts at a quarter of the bus clock. RiftWii's
// loader runs a GameCube game with Broadway at the Wii's speed, so it
// writes the Wii's clocks there. A game that builds a GameCube clock into
// its own code instead of reading those words would see its timers run
// half as fast again; those values are found here and moved to the clocks
// the console really runs at.

struct GcClocks {
    std::uint32_t bus = 0;  // Hz; the time base runs at bus / 4
    std::uint32_t cpu = 0;  // Hz
};

constexpr GcClocks kGameCubeClocks{162000000, 486000000};
constexpr GcClocks kWiiClocks{243000000, 729000000};

// HW_CLOCKS (Hollywood 0x0d800190): its lowest bit, SPEED, is set when
// Hollywood runs at a GameCube's 162 MHz (and Broadway at 486 MHz).
constexpr std::uint32_t kHwClocksAddress = 0x0D800190;
constexpr std::uint32_t kHwClocksGameCubeSpeed = 0x1;

// The clocks the console runs at, from HW_CLOCKS as read; a register that
// could not be read is passed as 0, the Wii's speed, which is how every
// Wii-mode launch finds it.
GcClocks gc_clocks_from_hw_clocks(std::uint32_t hw_clocks);

constexpr std::uint32_t gc_time_base_hz(const GcClocks& clocks) { return clocks.bus / 4; }

// One GameCube clock value a game builds with `lis rX,high` and then
// `ori rX,rX,low` or `addi rX,rX,low`, and the two words that build the
// running console's value instead.
struct GcClockFix {
    std::uint32_t high_at = 0;
    std::uint32_t low_at = 0;
    std::uint32_t old_value = 0;
    std::uint32_t new_value = 0;
    std::uint32_t high_word = 0;  // the new `lis`
    std::uint32_t low_word = 0;   // the new `ori` or `addi`
};

// The GameCube bus clock, CPU clock and time base frequency built in the
// game's code (`text`), each with the words that build the matching value
// of `running` instead. None when `running` is the GameCube's own clocks.
// The lower half has to come within four instructions of the upper one,
// with nothing between them that could change the register or branch away.
// This catches only a full Hz value built in place with immediates; folded
// constants (a division by 40500, a multiply by a magic number) and floats
// or doubles loaded from data are not detected, so finding none proves
// nothing about the game's timing.
std::vector<GcClockFix> find_gc_clock_values(const GcCodeReader& read, const std::vector<GcTextRange>& text,
                                             const GcClocks& running);

// A float or double in a game's data that holds one of the GameCube clocks
// (162e6, 486e6 or 40.5e6).
struct GcClockConstant {
    std::uint32_t address = 0;
    bool is_double = false;
    std::uint32_t hz = 0;  // the GameCube clock it holds
};

// Every big-endian float and double in `data` that holds a GameCube clock.
// Log-only: nothing is rewritten, as a float loaded from data may be used
// for more than the clock. Scans the ranges at word steps and reads them
// with `read`.
std::vector<GcClockConstant> find_gc_clock_constants(const GcCodeReader& read, const std::vector<GcTextRange>& data);

}  // namespace riftwii
