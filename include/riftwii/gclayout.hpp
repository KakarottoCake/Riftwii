// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace riftwii {

// Where RiftWii's GameCube loader (wii/launch/gcnative.cpp) puts its part
// of a running GameCube game, and what of the game's own layout it has to
// stay clear of.
//
// A GameCube game's OS arena runs from ArenaLo up to ArenaHi, and RiftWii
// keeps its blob above ArenaHi, under the FST. Most games link their stack
// right after their sections, far below. Some link it at the very top of
// MEM1 instead: Mario Golf: Toadstool Tour and Mario Power Tennis set r1 to
// 0x817dc000 in __init_registers, with ArenaLo right there. An ArenaHi
// below that has OSInit clear the arena with a size that wraps, past
// MEM1's end, and the stack runs down through whatever lies under it.

// One instruction word of the game's code at `address`; false where the
// DOL has no text.
using GcCodeReader = std::function<bool(std::uint32_t address, std::uint32_t& word)>;

// One text section's addresses, [start, end).
struct GcTextRange {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};

struct GcStackTop {
    std::uint32_t address = 0;  // r1 as __init_registers sets it; 0: not found
    std::uint32_t set_at = 0;   // the instruction that completes it
};

// The game's initial stack pointer: the `lis r1` and `ori r1,r1` (or
// `addi r1,r1`) pair of __init_registers, the first call of __start at
// the DOL's entry point, or the same pair in __start itself.
GcStackTop find_gc_stack_top(const GcCodeReader& read, std::uint32_t entry);

// The SDK's ArenaLo when the boot info leaves it 0 (as RiftWii does):
// OSInit's `lwz rX,0x30(rY)` of the boot info's arenaLo, `cmplwi rX,0`,
// and, where it is 0, `lis rX` and `addi rX,rX` of the linked __ArenaLo.
// 0 when the sequence is not there.
std::uint32_t find_gc_arena_lo(const GcCodeReader& read, const std::vector<GcTextRange>& text);

// The pieces RiftWii keeps in MEM1, placed under the FST around the DI
// registers' stand-in (whose address has to end in 0x6000, as the game
// adds that to the upper half it loads), with BI2's copy under all of
// them and the arena's top right there.
struct GcMem1Piece {
    std::uint32_t bytes = 0;
    std::uint32_t address = 0;  // out
};

struct GcMem1Plan {
    std::uint32_t di_regs = 0;
    std::uint32_t bi2 = 0;
    std::uint32_t arena_hi = 0;  // = bi2
};

constexpr std::uint32_t kGcDiRegsBytes = 0x40;

// Places `pieces` in the order given (the biggest first packs best): each
// goes right under the FST, above the DI stand-in, while there is room
// there, and under the DI stand-in otherwise. The stand-in is the highest
// address ending in 0x6000 that leaves it under the FST: with every piece
// together under 64 KB, a lower one could only lower the arena's top.
GcMem1Plan plan_gc_mem1(std::uint32_t fst_addr, std::vector<GcMem1Piece>& pieces, std::uint32_t bi2_bytes);

}  // namespace riftwii
