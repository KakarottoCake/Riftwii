// SPDX-License-Identifier: GPL-3.0-or-later
#include "memlimits.hpp"

#include <gccore.h>
#include <ogc/machine/processor.h>
#include <ogc/system.h>

#include <cstddef>
#include <malloc.h>

#include "ios_reload.hpp"
#include "log.hpp"

namespace riftwii::wii::mem {
namespace {

u32 g_mem1_floor = 0;      // the end of the loader's image: arena 1's low end at start
u32 g_mem2_libogc_lo = 0;  // arena 2's low end as libogc left it (the reload area's start)
u32 g_mem2_top = 0;        // arena 2's high end at start: IOS above
volatile u32 g_refused = 0;     // heap growth refused for lack of room
volatile u32 g_violations = 0;  // heap growth outside the limits, rolled back
bool g_dolphin = false;

u32 Address(const void* p) { return reinterpret_cast<u32>(p); }

u32 Kib(u32 bytes) { return bytes / 1024; }

}  // namespace

void Init() {
    g_mem1_floor = Address(SYS_GetArena1Lo());
    g_mem2_libogc_lo = Address(SYS_GetArena2Lo());
    g_mem2_top = Address(SYS_GetArena2Hi());
    if (Address(SYS_GetArena1Hi()) > kMem1Ceiling) SYS_SetArena1Hi(reinterpret_cast<void*>(kMem1Ceiling));
    if (Address(SYS_GetArena2Lo()) < kMem2Floor + kRestartBytes)
        SYS_SetArena2Lo(reinterpret_cast<void*>(kMem2Floor + kRestartBytes));
    // Asked now, while IOS is up: PoisonReloadArea runs mid-reload.
    g_dolphin = running_in_dolphin();
}

void LogLimits() {
    const u32 mem1_size = *reinterpret_cast<volatile u32*>(0x80000028);
    const u32 mem2_size = *reinterpret_cast<volatile u32*>(0x80003118);
    logf("Memory: MEM1 %u MiB, MEM2 %u MiB%s\n", mem1_size >> 20, mem2_size >> 20,
         mem1_size == 24u << 20 && mem2_size == 64u << 20 ? "" : " (not a Wii's 24 and 64: an emulator override)");
    logf("Memory: limits MEM1 0x%08x-0x%08x, MEM2 0x%08x-0x%08x (below 0x%08x: IOS reloads%s)\n", g_mem1_floor,
         kMem1Ceiling, kMem2Floor, g_mem2_top, kMem2Floor, g_dolphin ? ", poisoned in Dolphin" : "");
}

void LogUsage(const char* when) {
    const u32 mem1_lo = Address(SYS_GetArena1Lo());
    const u32 mem1_hi = Address(SYS_GetArena1Hi());
    const u32 mem2_lo = Address(SYS_GetArena2Lo());
    const u32 mem2_hi = Address(SYS_GetArena2Hi());
    // Taken: by the heap, the menu's textures and font, never given back.
    // (mallinfo would count the gap between the banks as in use once the
    // heap has spilled into MEM2.)
    logf("Memory (%s): MEM1 %u KiB taken, %u KiB free; MEM2 %u KiB taken, %u KiB free\n", when,
         Kib(mem1_lo - g_mem1_floor), Kib(mem1_hi > mem1_lo ? mem1_hi - mem1_lo : 0),
         Kib(mem2_lo > kMem2Floor ? mem2_lo - kMem2Floor : 0), Kib(mem2_hi > mem2_lo ? mem2_hi - mem2_lo : 0));
    if (mem1_hi > kMem1Ceiling || mem2_lo < kMem2Floor) {
        logf("Memory: LIMITS BROKEN: arena 1 ends at 0x%08x, arena 2 starts at 0x%08x\n", mem1_hi, mem2_lo);
    }
    if (g_refused != 0 || g_violations != 0) {
        logf("Memory: %u allocation(s) refused for lack of room, %u outside the limits\n",
             static_cast<unsigned>(g_refused), static_cast<unsigned>(g_violations));
    }
}

void CheckHeap(const char* when) {
    logf("Heap check (%s)\n", when);
    const struct mallinfo info = mallinfo();
    logf("Heap check (%s): OK, %u KiB free in the heap\n", when, Kib(static_cast<u32>(info.fordblks)));
}

void PoisonReloadArea() {
    if (!g_dolphin || g_mem2_libogc_lo >= kMem2Floor) return;
    u32* p = reinterpret_cast<u32*>(g_mem2_libogc_lo);
    u32* const end = reinterpret_cast<u32*>(kMem2Floor);
    while (p < end) *p++ = 0xDEADBEEF;
    DCFlushRange(reinterpret_cast<void*>(g_mem2_libogc_lo), kMem2Floor - g_mem2_libogc_lo);
}

}  // namespace riftwii::wii::mem

// Every heap growth goes through libogc's _sbrk_r (Makefile.wii links
// with --wrap=_sbrk_r). It already stays inside the arenas Init() set;
// this refuses, rather than hands out, anything that would not, so a
// broken limit shows as an allocation failure and a log line instead of
// memory an IOS reload or the apploader later overwrites.
extern "C" void* __real__sbrk_r(struct _reent* r, ptrdiff_t incr);
extern "C" void* __wrap__sbrk_r(struct _reent* r, ptrdiff_t incr) {
    using namespace riftwii::wii::mem;
    u32 level;
    _CPU_ISR_Disable(level);
    void* p = __real__sbrk_r(r, incr);
    if (p == reinterpret_cast<void*>(-1)) {
        if (incr > 0) g_refused = g_refused + 1;
    } else if (incr > 0) {
        const u32 start = reinterpret_cast<u32>(p);
        const u32 end = start + static_cast<u32>(incr);
        const bool in_mem1 = start >= g_mem1_floor && end <= kMem1Ceiling;
        const bool in_mem2 = start >= kMem2Floor && end <= g_mem2_top;
        if (!in_mem1 && !in_mem2) {
            __real__sbrk_r(r, -incr);
            g_violations = g_violations + 1;
            p = reinterpret_cast<void*>(-1);
        }
    }
    _CPU_ISR_Restore(level);
    return p;
}
