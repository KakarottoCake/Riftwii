// SPDX-FileCopyrightText: 2026 RiftWii contributors
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
    // The submit routine every one of them hands its block to (with the
    // callback in r4): one place that sees each request the game makes.
    std::uint32_t submit = 0;
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
//  - The screen-dimming reset for the controller ports (__VIResetSIIdle):
//    when a pad's buttons or sticks change, PADRead tells the video code
//    the ports are in use by calling a four-word function that clears a
//    small-data flag and returns 1 (`li r0, 0`, `stw r0, d(r13)` and
//    `li r3, 1` in either order, then `blr`). Its call sits in PADRead
//    itself or, in older SDKs, in the status helper linked right after
//    it, so only calls in the kIdleResetWindow bytes from PADRead's start
//    count, and they must all reach the same function. It must also follow
//    the Wii Remotes' reset, the same four words for the flag 4 bytes
//    above, as the video library lays the two out. Found in Brawl
//    (the helper, PADRead + 0xB14), Mario Kart Wii, Tatsunoko vs. Capcom:
//    Ultimate All-Stars, Naruto: Clash of Ninja Revolution 3 (PADRead +
//    0x410) and Kamen Rider: Super Climax Heroes (+ 0x414).
struct PadSymbols {
    std::uint32_t read = 0;           // 0: the game has no PADRead
    unsigned read_sites = 0;
    std::uint32_t control_motor = 0;  // 0: not found (no rumble)
    std::uint32_t idle_reset = 0;     // 0: not found (adapter pads don't keep the screen lit)
};

// False with `error` when the matches are ambiguous; true with read == 0
// when the game has none.
bool find_pad_symbols(const std::vector<CodeRange>& text, PadSymbols& out, std::string& error);

// PADControlMotor(chan, command) in a GameCube game's own SDK (the one the
// game was linked with, not the Wii's): after its checks it keeps the
// command's two low bits (`rlwinm rX, rY, 0, 30, 31`), ORs them into the
// analog mode with 0x40 in the top half (`oris rA, rB, 0x40`, the serial
// interface's motor command) and calls SISetCommand and SITransferCommands,
// two `bl` in a row. The function runs from the word after the previous
// `blr`, and is only taken when that word starts like a function (`mflr r0`
// or `stwu r1,-N(r1)` within three words, else an early return's `blr`
// could put a hook mid-function) and none of its first four words, which
// the hook's replay slot runs from elsewhere, is a relative branch. The
// same in Super Mario Sunshine and Luigi's Mansion. `out` is 0 with `error`
// empty when the game has none, and 0 with `error` set when several
// functions match or the one found is not safe to hook.
bool find_gc_pad_motor(const std::vector<CodeRange>& text, std::uint32_t& out, std::string& error);

// The SDK's four SI type words, found through PADRead's SIGetStatus call:
// it reads SISR, selects the channel byte, and on NOREP writes 8 to the
// channel's cached type unless BUSY is set. Returns 0 for an unsupported
// instruction layout or conflicting tables. The caller must also check
// that all 16 bytes belong to the game's data or BSS before reading them.
std::uint32_t find_gc_si_type_cache(const std::vector<CodeRange>& text, std::uint32_t pad_read);

// PADRead's EnabledBits load relative to r13, followed by its channel-mask
// test and disabled-port err -1/zero-status path. The mask must be formed
// by shifting 0x80000000 right by the channel. Zero means unsupported or
// ambiguous; callers must validate the resulting small-data address.
std::int32_t find_gc_pad_enabled_offset(const std::vector<CodeRange>& text, std::uint32_t pad_read);

// The SDK's SIProbe(chan), which answers SIDecodeType(SIGetType(chan)):
// a small function of a prologue, those two calls in a row and an
// epilogue, where the first is one of PADRead's own calls (SIGetType) and
// the second gives a WaveBird's type, 0x8B100000 (SIDecodeType). Its first
// word is never a branch, so the hook's replay slot can run it. 0 when
// there is no PADRead, no such function or more than one.
std::uint32_t find_gc_si_probe(const std::vector<CodeRange>& text, std::uint32_t pad_read);

// The SDK's __OSUnhandledException(exception, context, dsisr, dar): where
// every exception the game has no handler of its own for ends up, and
// the game's own crash screens (installed with OSSetErrorHandler) are
// called from. It reports "Unhandled Exception %d" or "Non-recoverable
// Exception %d" before it dumps the context: the function whose code
// builds either string's address (`lis` and `addi`/`ori`, directly or as
// an offset from a string pool's base) is the one. `data` is every
// section the strings may be in. 0 with `error` when no or several
// functions match.
bool find_unhandled_exception(const std::vector<CodeRange>& text, const std::vector<CodeRange>& data,
                              std::uint32_t& out, std::string& error);

// The SDK's OSReturnToMenu(): the game's own way to the Wii Menu, the one
// the HOME Menu's "Wii Menu" button takes. It shuts the game down as the
// SDK means it to and ends in the System Menu's launch, which
// patch_return_to (riftwii/returnto.hpp) points at the RiftWii channel.
// Release builds keep its panic text (the misspelling is the SDK's): the
// one function whose code builds that string's address, as
// find_unhandled_exception reads them, is OSReturnToMenu. __OSReturnToMenu
// has its own text, which only ends the same way. Found in Super Smash
// Bros. Brawl, Mario Kart Wii, Naruto: Clash of Ninja Revolution 3 and
// Kamen Rider: Super Climax Heroes (SDKs from 2007 to 2010). 0 with
// `error` when no or several functions use it.
inline constexpr const char* kOsReturnToMenuText = "OSReturnToMenu(): Falied to boot system menu.\n";
bool find_os_return_to_menu(const std::vector<CodeRange>& text, const std::vector<CodeRange>& data,
                            std::uint32_t& out, std::string& error);

}  // namespace riftwii
