// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "stash.hpp"

#include <gccore.h>
#include <ogc/cache.h>

#include <cstring>

#include "log.hpp"
#include "memlimits.hpp"
#include "riftwii/stashimage.hpp"

// libogc's linker script: the end of .bss. The heap (__Arena1Lo) starts
// at it rounded up to 32 bytes, and tools/dol_stash.py puts the stash there.
extern "C" char __bss_end[];

namespace riftwii::wii::stash {
namespace {

// Before the image in MEM2: what crt0 did this start. Written by crt0,
// so it lives at a fixed address, not in .data or .bss.
struct Note {
    u32 magic;
    u32 outcome;
    u32 mem1_address;  // where the DOL's copy was
    u32 reserved[5];
};
static_assert(sizeof(Note) == 32, "the image after the note stays 32-byte aligned");

constexpr u32 kNoteMagic = 0x52575354;  // "RWST"
constexpr u32 kTakenNow = 1;            // copied from the DOL's section this start
constexpr u32 kKept = 2;                // none in MEM1: the copy from an earlier start (a warm restart)
constexpr u32 kMem1Top = 0x81800000;

Note* note() { return reinterpret_cast<Note*>(mem::kStashArea); }
u8* image() { return reinterpret_cast<u8*>(mem::kStashArea + sizeof(Note)); }
constexpr u32 kAvailable = kStashMaxBytes;

u32 Mem1Address() { return (reinterpret_cast<u32>(__bss_end) + 31) & ~31u; }

bool g_checked = false;
u32 g_total = 0;  // the good copy's size, 0 when there is none

}  // namespace
}  // namespace riftwii::wii::stash

// Runs inside crt0, before .bss is cleared and before libogc is up: fixed
// addresses, the pure checks of stashimage.cpp and memcpy only.
extern "C" void riftwii_take_stash() {
    using namespace riftwii::wii::stash;
    const u32 from = Mem1Address();
    const u8* fresh = reinterpret_cast<const u8*>(from);
    const u32 total = riftwii::stash_check(fresh, kMem1Top - from, RIFTWII_VERSION);
    Note* n = note();
    n->magic = kNoteMagic;
    n->mem1_address = from;
    if (total != 0) {
        std::memcpy(image(), fresh, total);
        // A warm restart finds heap bytes here; never the image again.
        *reinterpret_cast<volatile u32*>(from) = 0;
        n->outcome = kTakenNow;
    } else {
        n->outcome = kKept;
    }
    DCFlushRange(n, sizeof(Note) + (total + 31) / 32 * 32);
}

namespace riftwii::wii::stash {

void Check() {
    if (g_checked) return;
    g_checked = true;
    g_total = riftwii::stash_check(image(), kAvailable, RIFTWII_VERSION);
}

u32 ReservedEnd() {
    Check();
    if (g_total == 0) return mem::kStashArea;
    return (mem::kStashArea + sizeof(Note) + g_total + 0xFFF) & ~0xFFFu;
}

bool Find(const char* name, const u8*& data, u32& size) {
    Check();
    if (g_total == 0) return false;
    return riftwii::stash_find(image(), name, data, size);
}

void Log() {
    Check();
    const Note* n = note();
    const char* how = "not recorded";
    if (n->magic == kNoteMagic && n->outcome == kTakenNow) how = "copied from the DOL at this start";
    if (n->magic == kNoteMagic && n->outcome == kKept) how = "none in the DOL at this start, kept from an earlier one";
    if (g_total != 0) {
        logf("Start-up data: %u KiB in MEM2 at 0x%08x (%s); its %u KiB in MEM1 at 0x%08x are the heap's\n",
             static_cast<unsigned>(g_total >> 10), static_cast<unsigned>(reinterpret_cast<u32>(image())), how,
             static_cast<unsigned>(g_total >> 10), static_cast<unsigned>(n->mem1_address));
    } else {
        logf("Start-up data: MISSING (%s; riftwii.elf has none, start the .dol): no menu font, translations or license text\n",
             how);
    }
}

}  // namespace riftwii::wii::stash
