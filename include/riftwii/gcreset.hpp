// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/symsearch.hpp"

// A GameCube game's restart (the console's Reset button), found in its
// code before it starts (plugins/gamecube/gcnative.cpp hooks it, runtime/gc
// does the restart).
//
// On a GameCube the game itself watches Reset and, once it has faded out
// and finished any save, calls OSResetSystem(reset, code, menu). The OS
// runs its reset functions (the drive, the memory cards, the
// controllers), stops the DSP and the audio, and for a hot reset turns
// the picture off and asks the hardware to reset the console through
// PI's reset register (0xCC003024): a short routine that writes 3, then
// the reset code shifted by 3, there and spins. The start-up code then
// boots the disc again. A restart (reset 0) reboots the disc from the OS
// itself instead, through the apploader, and reset 2 only shuts the OS
// down. In Wii mode there is no start-up code to boot the disc: the
// hardware reset would leave the console stopped, and the apploader
// would load the game again without RiftWii's changes.
namespace riftwii {

struct GcResetFunctions {
    std::uint32_t reset_system = 0;    // OSResetSystem(reset, code, menu); 0: not found
    std::uint32_t hardware_reset = 0;  // the routine that writes PI's reset register; 0: not found
};

// Both, or neither: the routine is the one function writing 3 and then r3
// to 0x24 past 0xCC003000 (`ori r8,r8,0x3000`, `li r4,3`, `stw r4,0x24(r8)`,
// `stw r3,0x24(r8)`), and OSResetSystem the one function that calls it,
// directly or through one function between (__OSDoHotReset, which some
// SDKs build into OSResetSystem), keeps its first argument in a register
// in its first words (`mr rN,r3`) and compares that register with 2 and
// with 1.
GcResetFunctions find_gc_reset_functions(const std::vector<CodeRange>& text);

// A switch to another program on the disc. The SDK's OSExec (Sonic Gems
// Collection starts Sonic CD, Sonic R and Sonic the Fighters this way) and
// a restart into a program the game named both end in
// __OSBootDolSimple(doloffset, restartCode, regionStart, regionEnd,
// argsUseDefault, argc, argv), which loads the disc's apploader and runs
// it. It tells the apploaders after 2004 from older ones by comparing the
// apploader's date with "2004/02/01": the function loads the string's
// address (`lis` of its upper half, then `addi` of its lower half within
// 16 words) and, taking seven arguments, keeps argv (r9) in a register of
// its own within its first 24 words (`mr rN,r9`). The SDK's own helper for
// the date (IsNewApploader) loads the string too, and some builds keep it
// as a function of its own (Sonic Gems Collection's sonicr.dol), but takes
// one argument. 0 when the string is not in the game's data, or not
// exactly one function is both (an SDK before 2004 has neither the string
// nor the function).
std::uint32_t find_gc_boot_dol(const std::vector<CodeRange>& text, const std::vector<CodeRange>& data);

// The program a switch starts gets its arguments where the SDK's start-up
// code (__start) reads them: in BI2 (0x2000 bytes, its address at
// 0x800000F4), whose word at 8 is the offset of argc, followed by argc + 1
// offsets of the strings (the last 0), all from BI2's start, the strings
// at its end. The SDK's OSExec packs them the same way and keeps the
// list in BI2's upper half. `bi2` is the disc's copy; its word at 8 is
// set (0 with no arguments). False when they do not fit in the upper half.
bool gc_pack_exec_args(std::vector<std::uint8_t>& bi2, const std::vector<std::string>& args);

// The restart puts MEM1 back as the loader left it at the jump, from a
// copy kept in MEM2 (the blob's gc_restart_region table, runtime/gc), then
// starts the game at its entry again: what a GameCube's start-up code does
// after a hot reset, without the hardware reset Wii mode cannot do.
//
// A piece of MEM1 to put back: copied from the jump's bytes, or zeroed
// (the game's BSS, the DI registers' stand-in).
struct GcRestartPiece {
    std::uint32_t address = 0;
    std::uint32_t length = 0;
    bool zero = false;
};
// As the blob reads it: `source` is where the bytes are kept in MEM2, 0 for
// a zeroed piece.
struct GcRestartRegion {
    std::uint32_t address = 0;
    std::uint32_t length = 0;
    std::uint32_t source = 0;
};
struct GcRestartPlan {
    std::vector<GcRestartRegion> regions;  // in the order they are put back
    std::uint32_t table = 0;               // where the regions' table goes (the window's start)
    std::uint32_t end = 0;                 // the end of what the window holds
};

// Lays `pieces` out in MEM2's window [window, window + window_bytes): the
// table of `max_regions` entries (12 bytes each) first, then each copied
// piece's bytes, 32-byte aligned. The pieces are put back in the order
// given, so a zeroed range goes before the copies that lie inside it.
// An empty plan when there are more pieces than `max_regions`, a piece
// is empty or outside MEM1, or they do not fit.
GcRestartPlan plan_gc_restart(const std::vector<GcRestartPiece>& pieces, std::uint32_t window,
                              std::uint32_t window_bytes, std::uint32_t max_regions);

}  // namespace riftwii
