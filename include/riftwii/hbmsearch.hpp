// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "riftwii/symsearch.hpp"

// The game's HOME Menu, opened from a GameCube controller (#224 part 1):
// the three SDK functions the pad blob (runtime/pad) hooks to stand a
// Classic Controller in on a free Wii Remote channel while the HOME Menu
// is wanted, found by their shape in the loaded game. The game itself then
// opens its HOME Menu on that controller's HOME, or shows its crossed-out
// HOME icon when it won't (a save, a loading screen), and the menu takes
// the controller's stick and buttons.
//
// Checked on Super Smash Bros. Brawl, Mario Kart Wii, Naruto: Clash of
// Ninja Revolution 3, Kamen Rider: Super Climax Heroes, Super Sentai
// Battle: Ranger Cross, Tatsunoko vs. Capcom: Ultimate All-Stars and Star
// Wars: The Force Unleashed II (SDKs from 2007 to 2010), with
// tools/ipcscan.cpp: each match the function found by hand.
namespace riftwii {

// The controller read (KPADReadEx in newer SDKs; KPADRead there is a
// wrapper that calls it with no error address): the one function that
// walks its ring buffer of statuses backwards (`mulli rD,rA,S` and at least
// three `addi rD,rA,-S`, S the status's size) and stores the status's
// device type and error (`stb` at 0x5C and 0x5D). Brawl has a second ring
// walker (S = 144) without those stores.
struct KpadRead {
    std::uint32_t address = 0;       // the function's `stwu`, 0: not exactly one
    std::uint32_t status_bytes = 0;  // S: 132 (2007), 176 (2009), 240 (2009-2010)
    // Its fourth argument is the error's address (the newer KPADReadEx,
    // which copies r6 to a register of its own as it starts); the 2007
    // read takes three, and r6 means nothing to it.
    bool takes_error = false;
};
KpadRead find_kpad_read(const std::vector<CodeRange>& text);

// WPADProbe(chan, &type): the one function that indexes the channel table
// (`slwi r0,r3,2`, `lwzx`), stores the control block's device type through
// its second argument (`stw r0,0(rA)`), compares that type with 253 (no
// device known yet) and returns -1 (no controller) and -2 (busy). The
// same 140 bytes in every build seen; only the control block's offsets
// move. Its address, or 0 when there is not exactly one.
std::uint32_t find_wpad_probe(const std::vector<CodeRange>& text);

// The small-data base the game's start-up code loads into r13 (`lis
// r13,hi` then `ori` or `addi r13,r13,lo`), within its first 256 words
// from `entry`; 0 when not found.
std::uint32_t find_small_data_base(const std::vector<CodeRange>& sections, std::uint32_t entry);

// HBMInit and HBMCalc. Every HOME Menu build carries its version banner,
// "<< RVL_SDK - HBM release build: ... >>", and one data word pointing at
// it; exactly one function loads that word (through r13, or `lis` and
// `lwz`) and calls a function (OSRegisterVersion): HBMInit. The SDK's
// wrappers follow it in a fixed order, so HBMCalc, which runs once a frame
// exactly while the HOME Menu is open, is the function right after
// HBMInit's `blr` (past any padding), starting with `stwu` and calling out
// within its first 32 words. Both 0 when anything doesn't fit.
struct HbmFunctions {
    std::uint32_t init = 0;
    std::uint32_t calc = 0;
};
HbmFunctions find_hbm_functions(const std::vector<CodeRange>& text, const std::vector<CodeRange>& sections,
                                std::uint32_t small_data_base);

}  // namespace riftwii
