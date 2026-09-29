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
// The blob goes below the MEM1 arena's top, under the other blobs; its
// state (the record, an IPC request and its own 4 KB stack) at the bottom
// of the MEM2 arena, above theirs. Anything in the way turns it off with
// a reason for the log, never fails the launch.
namespace riftwii::wii {

struct FaultHook {
    bool active = false;
    std::uint32_t function = 0;    // __OSUnhandledException
    std::uint32_t code_base = 0;   // the blob (MEM1)
    std::uint32_t code_bytes = 0;
    std::uint32_t state_base = 0;  // state (MEM2)
    std::uint32_t state_bytes = 0;
    std::uint32_t new_arena1_hi = 0;
    std::uint32_t new_arena2_lo = 0;
};

// Before the <memory> patches; `arena1_hi` and `arena2_lo` are the arena
// ends the other blobs left.
bool plan_fault_hook(const DolHeader& dol, std::uint32_t arena1_hi, std::uint32_t mem1_floor, std::uint32_t arena2_lo,
                     const std::vector<MemoryPatch>& patches, FaultHook& out, std::string& why);

// After the patches, the cheats and the other blobs' hooks.
bool install_fault_hook(FaultHook& hook, std::string& why);

}  // namespace riftwii::wii
