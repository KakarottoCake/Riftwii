// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/symsearch.hpp"

struct gc_tri;

namespace riftwii {

// Same order as runtime/gc/gc_tri.h's enum gc_tri_fn and gc_entry.S's table.
constexpr unsigned kGcTriHooks = 15;

// The arcade (Triforce) SDK's functions RiftWii replaces in a Triforce
// game (runtime/gc/gc_tri.h), found by what each one does rather than by
// address, so another game on the same SDK is found too:
//  - SITransfer: the function the JVS packet send calls with channel 0,
//    128 bytes out and 128 bytes in (li r3,0 / li r5,0x80 / li r7,0x80).
//  - The baseboard's EXI functions: each builds its 4-byte command with
//    one checksum helper (the function holding "lis rX,0x8D80", the
//    checksum's polynomial), the command byte in the top of r3 (lis or
//    oris r3): 0x01 then 0x02 writes a byte of backup memory, 0x01 then
//    0x03 reads one (or a block, when the function takes a sixth
//    argument), 0x01 then 0x05 writes a block, 0x82/0x83 read and write
//    the interrupt status, 0x86/0x87 the interrupt mask, 0xFF the network
//    controller. The connect function compares the EXI ID with the
//    baseboard's 0x06041000 (addis r0,r3,0xF9FC; cmplwi r0,0x1000).
//  - The media board's disc commands: the functions that pass 0xAA (a
//    write), 0x12 (the inquiry) and an "ori r3,rX,0xAB" (the execute
//    command) to the helper that scrambles every command (the one both
//    0xA8 and 0xAA are passed to).
//  - The baseboard's interrupt handler setter: the short function after
//    the network controller's one, with a single call (EXISetExiCallback).
// Behaviour read from naari3's Mario Kart Arcade GP2 decompilation (CC0).
struct GcTriforceFunctions {
    std::array<std::uint32_t, kGcTriHooks> hooks{};
    // The SDK names of what was not found, for the log ("" when all were).
    std::string missing;
    bool complete() const { return missing.empty(); }
};

GcTriforceFunctions find_gc_triforce_functions(const std::vector<CodeRange>& text);

// The name of hook `index` (gc_tri.h's order), for the log.
const char* gc_triforce_function_name(unsigned index);

// The boards' identity in `t` (zeroed by the caller): what the baseboard,
// the JVS I/O board and the media board answer with, which the blob
// cannot hold itself (no constant data). `region` is the disc ID's fourth
// letter: 'J' answers as a Japanese cabinet, 'P' as an export one, the
// rest as a USA one. The I/O board is Namco's FCA-1 (Mario Kart Arcade
// GP's): a player with 16 switches, 2 coin slots, 7 analog channels, 6
// output bytes and 4 analog outputs.
void gc_tri_identity(gc_tri& t, char region);

}  // namespace riftwii
