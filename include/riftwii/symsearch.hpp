// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Finds the SDK's IPC entry points in a loaded game without SDK-specific
// byte patterns. The DVD driver issues every disc command as
//     li r4, <DI command>   ...   bl IOS_IoctlAsync
// so the branch target shared by the call sites of several distinct DI
// commands (0x71 DVDLowRead among them) is IOS_IoctlAsync. Likewise the
// open-partition site (`li r4, 0x8B` with `li r5, 3` and `li r6, 2` for
// the vector counts) names IOS_IoctlvAsync. Command numbers are from the
// wiibrew /dev/di table. Verified on Mario Kart Wii (RMCE01).
namespace riftwii {

struct CodeRange {
    std::uint32_t address = 0;     // load address of bytes[0]
    const std::uint8_t* bytes = nullptr;  // big-endian instructions
    std::size_t size = 0;
};

struct IpcSymbols {
    std::uint32_t ioctl_async = 0;
    unsigned ioctl_async_commands = 0;  // distinct DI commands whose sites agree
    std::uint32_t ioctlv_async = 0;     // 0 when not found (optional)
};

// Needs at least `min_commands` distinct DI commands (default 3), always
// including 0x71, to agree on one target inside the ranges.
bool find_ipc_symbols(const std::vector<CodeRange>& text, IpcSymbols& out, std::string& error,
                      unsigned min_commands = 3);

// The SDK's whole IPC API: the asynchronous and synchronous function per
// IPC command 1..7 (open, close, read, write, seek, ioctl, ioctlv),
// compiled together in one stretch of text around IOS_IoctlAsync. Each
// allocates its request block with the same helper (64 bytes, 32-byte
// aligned), stores its command number into the block (`li rX, N` then
// `stw rX, 0(rY)`) and hands the block to the same submit routine with
// the callback in r4: a register copy in the asynchronous form, `li r4,
// 0` in the synchronous one. Functions the game never calls are not in
// its DOL (the linker drops them) and stay 0 here: nothing calls them,
// so nothing needs hooking. The reboot forms of ioctlv (relaunch set in
// the block) are left out. Section 24.3 of docs/CONDUCTOR_REVIEW_2.md;
// verified on four titles (2007-2009 SDKs) with tools/ipcscan.cpp.
enum IpcCommand { kIpcOpen = 1, kIpcClose, kIpcRead, kIpcWrite, kIpcSeek, kIpcIoctlCmd, kIpcIoctlvCmd };

struct IpcApi {
    std::uint32_t async[8] = {};  // indexed by IpcCommand (0 unused); 0 = not in the game
    std::uint32_t sync[8] = {};
};

bool find_ipc_api(const std::vector<CodeRange>& text, const IpcSymbols& known, IpcApi& out, std::string& error);

// The SDK's GameCube controller functions, for the adapter hook
// (runtime/pad). Only games that support the controller link them.
//  - PADRead(PADStatus status[4]): for each port that cannot be read it
//    stores the error into the status's `err` byte (offset 10) and clears
//    the 10 bytes before it, `stb rE, 10(rS)` then `mr r3, rS`, `li r4, 0`,
//    `li r5, 10`, `bl memset`: at least 3 such sites, one memset.
//  - PADControlMotor(chan, command): reads the rumble-off flag at
//    0x800030E3 (`lis rX, 0x8000` with `lbz rY, 0x30E3(rX)`) and builds
//    the motor command with `oris rA, rB, 0x40`.
// Each is the function (from its `stwu r1` and `mflr r0`) around them.
// Verified on Super Smash Bros. Brawl and Mario Kart Wii; none of Super
// Mario Galaxy 2, Kirby's Epic Yarn or Wario Land: Shake It! (no
// GameCube controller support) has either.
struct PadSymbols {
    std::uint32_t read = 0;           // 0: the game has no PADRead
    unsigned read_sites = 0;
    std::uint32_t control_motor = 0;  // 0: not found (no rumble)
};

// False with `error` when the matches are ambiguous; true with read == 0
// when the game has none.
bool find_pad_symbols(const std::vector<CodeRange>& text, PadSymbols& out, std::string& error);

}  // namespace riftwii
