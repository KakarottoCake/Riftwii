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

}  // namespace riftwii
