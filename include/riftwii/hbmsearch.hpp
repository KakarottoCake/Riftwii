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
    std::uint32_t status_bytes = 0;  // S: 136 (2006), 132 (2007), 176 (2009), 240 (2009-2010)
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

// KPADGetUnifiedWpadStatus(chan, status[], count): the SDK's copy of a
// channel's newest raw Wii Remote statuses. Mario Kart Wii reads a Classic
// Controller's buttons from it, not from the KPAD read (9 Nov), so the
// stand-in fills it too where it is found; most games leave it out (none
// of the other 14 programs on hand has it). The one function that limits
// its count to 16 (`cmplwi r5,16` and `li rX,16`, or later libraries'
// count-relative `addi r0,r3,16; cmplw r5,r0`, as Donkey Kong Country
// Returns has it), indexes the channel's KPAD
// block (`mulli rD,r3,K`, K at least 256) and copies E-byte statuses
// (`mulli rD,rA,E` and `li r5,E` for the copy, E from 40 to 96).
struct KpadUnified {
    std::uint32_t address = 0;       // the function's `stwu`, 0: not exactly one
    std::uint32_t status_bytes = 0;  // E: 56 in Mario Kart Wii
};
KpadUnified find_kpad_unified(const std::vector<CodeRange>& text);

// KPADSetConnectCallback(chan, callback): the newer KPAD's (2008 on)
// setter for the game's own "a controller connected or left" callback,
// kept in KPAD's per-channel block. GoldenEye 007 reads only the Wii Remote
// channels that callback told it are connected (9 Nov), so the stand-in
// calls it too where the game set one. Found as the one function that
// indexes the block by the KPAD read's channel stride (`mulli r0,rX,K`,
// then `lis`/`addi` the block, `add`) and swaps one word in it (`lwz
// rA,O(rB)` then `stw rC,O(rB)`, the old callback returned). The game's
// callback for channel c is then the word at block + c*K + O.
struct KpadConnect {
    std::uint32_t address = 0;  // the setter, 0: not exactly one (an older KPAD has none)
    std::uint32_t block = 0;    // KPAD's per-channel block
    std::uint32_t stride = 0;   // K, the block's size per channel
    std::uint32_t offset = 0;   // O, the callback's place in it
};
KpadConnect find_kpad_connect(const std::vector<CodeRange>& text, std::uint32_t kpad_read);

// WPADGetInfoAsync(chan, info, callback), which a 2006 KPAD game (Excite
// Truck) waits on before it uses a Wii Remote: the one function that
// indexes the channels as WPADProbe does (`slwi r0,r3,2`), answers busy
// (`li rX,-2`), queues the status request (report 0x15, `li rX,0x15`) in a
// 48-byte command (`mulli rX,rY,48`), and keeps its info and callback
// arguments (`mr` from r4 and from r5) to store them into it. One in each of
// the 29 Wii games dumped (2006 to 2010). 0 when not exactly one.
std::uint32_t find_wpad_get_info(const std::vector<CodeRange>& text);

// The later WPAD's WPADGetInfoAsync, by its blocking caller WPADGetInfo: a
// short framed function that makes the address of WPAD's control-block
// table `block_table` (its connect callbacks' table,
// find_wpad_connect_later) and calls a function whose result it checks
// for busy at once (`bl X; cmpwi r3,-2`); X is WPADGetInfoAsync (Mario
// Strikers Charged: WPADGetInfo 0x803CD2A4 calls 0x803CD35C). The 2006
// finder's marks also fit another function in that WPAD. 0 when not exactly
// one.
std::uint32_t find_wpad_get_info_later(const std::vector<CodeRange>& text, std::uint32_t block_table);

// A later WPAD's blocking WPADGetInfo with the request built in, for a game
// with no WPADGetInfoAsync to answer busy (Scarface: The World Is Yours:
// 0x80523914). A framed function that makes the address of WPAD's
// control-block table `block_table`, checks a result for busy (`cmpwi rN,-2`),
// builds the status request (`li rN,0x15`) with the address of its reply
// handler, and sleeps on a queue in the channel's block (`addi r3,rX,Q` just
// before a call), the handler waking that same queue (it makes
// `block_table`'s address, then `addi r3,r3,Q` and a tail branch, within its
// first eight words and before any return). With no Wii Remote on the
// channel it sends nothing and still sleeps. 0 when not exactly one.
std::uint32_t find_wpad_get_info_blocking(const std::vector<CodeRange>& text, std::uint32_t block_table);

// Where the 2006 WPAD (Excite Truck, Marvel Ultimate Alliance: both its
// Oct 3 2006 build) keeps each channel's connect callback, which those games
// wait on before they use a Wii Remote (KPAD has none of its own there):
// WPADSetConnectCallback is the one function that takes the channel's
// control block from a table (`lis`/`addi` for the table, `lwzx` by the
// channel) and swaps the callback at its +2020 (`lwz` and `stw` there).
// The block's address is the table's word at channel * 4, filled when the
// game starts WPAD. Both 0 when not exactly one.
struct WpadConnect2006 {
    std::uint32_t table = 0;
    std::uint32_t offset = 0;
};
WpadConnect2006 find_wpad_connect_2006(const std::vector<CodeRange>& text);

// The same for the later WPAD (2007 on), for a game that reads its Wii
// Remotes below KPAD and hears of them only through WPAD's connect callback
// (Punch-Out!!: its own pad manager calls WPADRead for the players that
// callback told it of). WPAD has several setters of that shape (the
// connect, sampling and extension callbacks, side by side in the channel's
// control block), so the connect one is told by how WPAD calls its slot:
// through `lwz r12,O(rB)` and `bctrl` with `li r4,-1` (no controller, as
// a Wii Remote leaves) in between, which the other two never get. The table
// and O (+2212 in Punch-Out!!'s 2008 WPAD); both 0 when not exactly one.
WpadConnect2006 find_wpad_connect_later(const std::vector<CodeRange>& text);

// The game's WPAD extension callback (WPADSetExtensionCallback's slot),
// which WPAD calls with the channel and the attachment's type once it has
// read it (Super Mario Galaxy 2 notes "Nunchuk attached" only from it):
// the word just before `connect`'s slot in the same control block (+2016
// by the 2006 WPAD's +2020, +2208 by +2212 in 2007-2008, +2276 by +2280
// from 2009), when exactly one setter of the same shape swaps it. Both 0
// otherwise, or when `connect` is none.
WpadConnect2006 find_wpad_extension(const std::vector<CodeRange>& text, const WpadConnect2006& connect);

// WPADRead(chan, status), which copies a channel's newest report into the
// game's WPADStatus: the one function that indexes the channels
// (`slwi rX,r3,2`), copies 42 bytes (`li r5,42`, the bare Wii Remote's
// status, for a report whose error is -2 or -7: `cmpwi` with both) and
// stores the error into the game's status (`stb` at 41 through its second
// argument, kept with `mr` from r4). 0 when not exactly one.
std::uint32_t find_wpad_read(const std::vector<CodeRange>& text);

// WPADSetDataFormat(chan, format): the one function that answers no
// controller (`li rX,-1`, or `cmpwi` with -1 where it returns the probe's
// result) and busy (`li rX,-2`), compares the format it is
// given (kept with `mr` from r4) with the channel's (`cmplw`), and stores
// it there (`stw` of that register at an offset of at least 0x400, the
// control block's). 0 when not exactly one.
std::uint32_t find_wpad_set_format(const std::vector<CodeRange>& text);

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
// `select` answers which button the menu ended on (-1 while it runs). In
// the C++ HOME Menu (2008 on: Dr. Mario Online Rx, Contra ReBirth, the
// later Virtual Console builds) HBMCalc returns it from a short leaf it
// calls last, right before its epilogue, and that leaf is `select`.
// Otherwise it is HBMGetSelectBtnNum, the function after HBMDraw (which
// follows HBMCalc): a short wrapper that takes no argument and calls out,
// or a short leaf answering -1 (2007: Brawl, Mario Kart Wii, Cruis'n USA).
// 0 when neither fits (the HOME Menu works without it; the stand-in then
// can't tell the Operations Guide from Close).
struct HbmFunctions {
    std::uint32_t init = 0;
    std::uint32_t calc = 0;
    std::uint32_t select = 0;
};
HbmFunctions find_hbm_functions(const std::vector<CodeRange>& text, const std::vector<CodeRange>& sections,
                                std::uint32_t small_data_base);

}  // namespace riftwii
