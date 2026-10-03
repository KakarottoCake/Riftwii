// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <new>
#include <string>

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

// Test builds only (make WII_DEFINES=-DRIFTWII_TEST_LIMITS): once the SD
// card is up, sd:/riftwii/test_limits.txt's "ballast_kib = N" holds N KiB
// of the heap back for the whole run, in "chunk_kib = M" pieces (default
// 256), so Dolphin can run as short of memory as a tester's Wii. A no-op
// in other builds.
void TestBallast();

// session.log lines: the limits and the physical sizes (Dolphin's memory
// size override shows here), then, with LogUsage, what is used and free.
void LogLimits();
void LogUsage(const char* when);
// Walks every chunk of the heap after logging `when`: their sizes, and a
// free chunk's links and end. A damaged heap is logged there (the chunk,
// the one before it, and the bytes around as hex and text: an overrun
// shows what wrote it) instead of crashing later, so the log names the
// step that damaged it. Then the free lists (mallinfo), for the total.
// False when the heap is damaged.
bool CheckHeap(const char* when);

// The same check without logging, for while the log is closed (across an
// IOS reload): false, with `problem` saying what and where, when the heap
// is damaged.
bool HeapIntact(std::string& problem);

// For the menu's own steps (a cover download, a disc probe, a screen
// change): silent while the heap is whole; the first damage of the run is
// logged as CheckHeap logs it, so a report names the step it followed.
bool WatchHeap(const char* when);

// A launch that runs out of memory (a big pack: RiiMajor's 5.9 MB main.dol
// and its files) throws std::bad_alloc; uncaught, it aborted RiftWii straight
// back to the Homebrew Channel with nothing said. Run through this, it is a
// failed launch with `error` saying so. For every launch: the menu's,
// autorun's and a headless one's.
template <typename Run>
bool OutOfMemoryAsError(std::string& error, Run run) {
    try {
        return run();
    } catch (const std::bad_alloc&) {
        error = "RiftWii ran out of memory while starting the game (a big pack?); "
                "turn off menu music or other packs and try again";
        return false;
    }
}

// In Dolphin only (a no-op on a Wii): fills MEM2 from where libogc left
// arena 2's low end up to kMem2Floor with 0xDEADBEEF. Called by
// reload_ios once the new IOS is launched.
void PoisonReloadArea();

}  // namespace riftwii::wii::mem
