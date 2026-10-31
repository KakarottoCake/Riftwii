// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "riftwii/symsearch.hpp"

namespace riftwii {

// A sound engine's ARAM pool clamped to ARGetSize(), in the shape MusyX's
// ARAM setup has (Doshin the Giant PAL, GKDP01, at 0x8008e384):
//
//     bl   ARGetSize          ; lwz r3,d(r13), blr
//     lwz  rA,top(r13)        ; the pool's top: its base plus its length
//     cmplw rA,r3
//     ble  +12                ; the top is within ARAM: kept
//     bl   ARGetSize
//     stw  r3,top(r13)        ; else the top is ARAM's end
//
// The GameCube loader raises the game's ARAM addresses into MEM2's window
// (0x10004000 up, gcnative.cpp's raise_aram_base), but ARGetSize still
// answers 16 MB (0x01000000). The clamp then put the pool's top far below
// its base, every stream allocation failed, and Doshin printed "Cant alloc
// snd Stream" 0.2 s in and restarted itself through its error screen, over
// and over. The loader gives both calls ARAM's raised end instead
// (`lis r3,0x1100`), so the clamp keeps its meaning in the raised
// addresses: ARAM is 16 MB on every GameCube.
struct GcAramClamp {
    std::uint32_t first_call = 0;   // the `bl ARGetSize` before the comparison
    std::uint32_t second_call = 0;  // the one whose answer is stored
};

// Every clamp of that shape in the game's code, in address order.
std::vector<GcAramClamp> find_gc_aram_size_clamps(const std::vector<CodeRange>& text);

// The word that replaces each of a clamp's calls: `lis r3,0x1100`, ARAM's
// end in the raised addresses.
constexpr std::uint32_t kGcAramRaisedEndWord = 0x3C601100;

// An allocator's top a little below ARAM's end, in the shape EA's ARAM
// code has (007: NightFire, Nightfire.elf at 0x80033d64; "GC ARAM.cpp"):
//
//     bl   ARGetSize          ; lwz r3,d(r13), blr
//     addi r0,r3,-0x4000      ; 16 KB below the end
//     mr   r3,r0
//     stw  r0,top(r13)        ; the top every allocation is checked against
//
// With the addresses raised, the first allocation (0x10004000) was past
// that top (0x00FFC000) and the game stopped in OSPanic, "ARAM Full", on
// a black screen. Its call answers ARAM's raised end too, so the top is
// 16 KB below 0x11000000. Taken: an `addi rD,r3,-N` right after the call
// (N up to 64 KB, a multiple of 32), rD stored to a small-data variable
// within the next two words. The calls, in address order.
std::vector<std::uint32_t> find_gc_aram_tops_below_size(const std::vector<CodeRange>& text);

// A pool size from ARAM's base to a fixed end, in the shape 007:
// NightFire's driving.elf has in its sound code (at 0x80211320):
//
//     bl   ARGetBaseAddress   ; li r3,0x4000, blr
//     addi r6,r3,0x100
//     lis  r0,0x80            ; the pool's end: 8 MB
//     ...
//     subf r0,r6,r0           ; its size: the end less the base
//
// With the base raised to 0x10004000 the size came out negative, the
// driving mission's sound allocations failed and it read through a null
// pointer while loading. Such an end is raised with the base (`lis
// r0,0x1080`), so the size stays the same. Taken: a `lis rX,H` (H up to
// 0x100, an end within 16 MB) in the three words after the call, and a
// `subf` with rX as the minuend in the four words after the `lis`. Found
// before the base itself is raised (ARGetBaseAddress is recognised by its
// `li r3,0x4000`). The `lis` addresses, in address order.
std::vector<std::uint32_t> find_gc_aram_ends_after_base(const std::vector<CodeRange>& text);

}  // namespace riftwii
