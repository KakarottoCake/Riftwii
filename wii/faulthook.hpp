// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/dol.hpp"
#include "riftwii/patch.hpp"

// A game's crashes (runtime/fault): the blob hooks the game's
// __OSUnhandledException and writes what the processor held when the
// game crashed to the NAND (/shared2/riftwii/crash.bin); the menu makes
// sd:/riftwii/gamecrash.txt of it at its next start and offers to send a
// report (wii/reportsend.hpp). Crashes only: a game that freezes without
// an exception leaves nothing.
//
// With `answer_bca` it also answers the game's BCA read as a retail disc
// does (runtime/fault/fault_hook.h), for images that have none, when the
// resident runtime (which can do the same) is not installed anyway.
//
// The blob goes below the MEM1 arena's top, under the other blobs; its
// state (the record, an IPC request and its own 4 KB stack) at the bottom
// of the MEM2 arena, above theirs. Anything in the way turns it off with
// a reason for the log, never fails the launch.
//
// With `mem1_veneers` (a code build that moved its code list: Project+
// and PMEX Remix size their heaps to all of MEM1 and write over the top
// of the arena) the blob goes to MEM2 too, just below its state, and the
// game's functions reach it through 16-byte veneers from that MEM1
// address. Its bytes are staged until place_fault_hook copies them down
// at the jump (MEM2's bottom is this loader's until then).
namespace riftwii::wii {

struct FaultHook {
    bool active = false;
    std::uint32_t function = 0;    // __OSUnhandledException (0: not hooked)
    std::uint32_t ioctl = 0;       // IOS_IoctlAsync, for the BCA (0: not hooked)
    std::uint32_t code_base = 0;   // the blob (MEM1, or MEM2 with veneers)
    std::uint32_t code_bytes = 0;
    std::uint32_t state_base = 0;  // state (MEM2)
    std::uint32_t state_bytes = 0;
    std::uint32_t new_arena1_hi = 0;
    std::uint32_t new_arena2_lo = 0;
    std::uint32_t veneers = 0;     // MEM1 veneers' address (0: the blob is in MEM1)
    std::uint8_t* stage = nullptr; // the blob's bytes until the jump (MEM2 only)
};

// Before the <memory> patches; `arena1_hi` and `arena2_lo` are the arena
// ends the other blobs left.
bool plan_fault_hook(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                     std::uint32_t mem1_veneers, bool answer_bca, const std::vector<MemoryPatch>& patches,
                     FaultHook& out, std::string& why);

// After the patches, the cheats and the other blobs' hooks.
bool install_fault_hook(FaultHook& hook, std::string& why);

// At the jump, after the loader's last use of MEM2: a staged blob to its
// place. Nothing to do for one in MEM1.
void place_fault_hook(FaultHook& hook);

}  // namespace riftwii::wii
