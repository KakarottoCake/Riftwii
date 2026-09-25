// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

// The Wii's memory as this loader may use it, in one place, and enforced.
//
// MEM1 (24 MiB, 0x80000000-0x81800000): the game's DOL fills it from
// 0x80004000 up and its apploader is loaded at 0x81200000. The loader is
// linked at 0x80A00000 (Makefile.wii); its heap runs from the end of its
// image to kMem1Ceiling.
//
// MEM2 (64 MiB, 0x90000000-0x94000000): IOS owns the top (above arena 2's
// high end, 0x933E0000 under the usual IOS), and an IOS reload stages the
// new kernel in the low part and overwrites it (1.0.5-1.0.8 crashed after
// every reload: heap chunks at 0x9011E000-0x9014C500 destroyed). Nothing
// of ours lives below kMem2Floor: the heap's MEM2 part, the menu's
// textures and its font sit between kMem2Floor and arena 2's high end.
//
// Dolphin reloads IOS without touching guest memory, so there
// PoisonReloadArea() overwrites that low part the way the Wii does: a bug
// that keeps something there then crashes in Dolphin too.
namespace riftwii::wii::mem {

constexpr u32 kMem1Ceiling = 0x81200000;  // the game's apploader
constexpr u32 kMem2Floor = 0x90800000;    // below: an IOS reload's
// The first bytes above the floor hold the restart snapshot and handoff
// (wii/restart.hpp); the heap starts after them.
constexpr u32 kRestartArea = kMem2Floor;
constexpr u32 kRestartBytes = 0x9000;

// First thing in main: applies the limits above to libogc's arenas.
void Init();

// session.log lines: the limits and the physical sizes (Dolphin's memory
// size override shows here), then, with LogUsage, what is used and free.
void LogLimits();
void LogUsage(const char* when);
// Walks the heap's free lists (mallinfo) after logging `when`: a heap
// damaged by an earlier step crashes here, right after that line, rather
// than somewhere later, so the log names the step that damaged it.
void CheckHeap(const char* when);

// In Dolphin only (a no-op on a Wii): fills MEM2 from where libogc left
// arena 2's low end up to kMem2Floor with 0xDEADBEEF. Called by
// reload_ios once the new IOS is launched.
void PoisonReloadArea();

}  // namespace riftwii::wii::mem
