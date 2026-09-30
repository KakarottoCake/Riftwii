// SPDX-License-Identifier: GPL-3.0-or-later
#include "memlimits.hpp"

#include <gccore.h>
#include <ogc/machine/processor.h>
#include <ogc/system.h>

#include <cstddef>
#include <malloc.h>
#include <reent.h>

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

namespace {

// The heap's pieces as _sbrk_r handed them out (below): MEM1's, then
// MEM2's once MEM1 is full.
struct Segment {
    u32 start, end;
};
Segment g_segments[8];
volatile u32 g_segment_count = 0;

void NoteGrowth(u32 start, s32 incr) {
    const u32 n = g_segment_count;
    if (n > 0 && g_segments[n - 1].end == start) {
        g_segments[n - 1].end = start + static_cast<u32>(incr);
    } else if (incr > 0 && n < sizeof(g_segments) / sizeof(g_segments[0])) {
        g_segments[n] = Segment{start, start + static_cast<u32>(incr)};
        g_segment_count = n + 1;
    }
}

bool InHeap(u32 address) {
    for (u32 i = 0; i < g_segment_count; ++i)
        if (address >= g_segments[i].start && address < g_segments[i].end) return true;
    return false;
}

// 96 bytes around `at`, as hex and as text: an overrun shows what wrote it.
void Dump(u32 at) {
    const u32 from = (at - 48) & ~15u;
    for (u32 line = from; line < from + 96; line += 16) {
        if (!InHeap(line) && !InHeap(line + 15)) continue;
        char hex[3 * 16 + 1], text[17];
        for (u32 i = 0; i < 16; ++i) {
            const u8 b = InHeap(line + i) ? *reinterpret_cast<const u8*>(line + i) : 0;
            static const char kDigits[] = "0123456789abcdef";
            hex[3 * i] = kDigits[b >> 4];
            hex[3 * i + 1] = kDigits[b & 15];
            hex[3 * i + 2] = ' ';
            text[i] = b >= 0x20 && b < 0x7F ? static_cast<char>(b) : '.';
        }
        hex[3 * 16] = 0;
        text[16] = 0;
        logf("  %08x  %s %s\n", line, hex, text);
    }
}

}  // namespace

// newlib's malloc (dlmalloc 2.6): each chunk is prev_size, size (low bit:
// the one before is in use), then for a free chunk its bin links; the top
// chunk is __malloc_av_[2], the bins' headers sit in __malloc_av_ itself.
extern "C" void* __malloc_av_[];
extern "C" void __malloc_lock(struct _reent*);
extern "C" void __malloc_unlock(struct _reent*);

namespace {
bool WalkHeap(const char* when, std::string* problem);
}

bool CheckHeap(const char* when) {
    logf("Heap check (%s)\n", when);
    // Other threads (the GUI's) allocate too: none while the walk runs.
    __malloc_lock(_REENT);
    const bool ok = WalkHeap(when, nullptr);
    __malloc_unlock(_REENT);
    return ok;
}

namespace {
bool WalkHeap(const char* when, std::string* problem) {
    char text[160];
    const u32 top = Address(__malloc_av_[2]);
    const u32 bins_lo = Address(__malloc_av_) - 8, bins_hi = Address(__malloc_av_) + 258 * 4;
    const auto link_ok = [&](u32 p) { return InHeap(p) || (p >= bins_lo && p < bins_hi); };
    u32 chunks = 0;
    for (u32 s = 0; s < g_segment_count; ++s) {
        const u32 end = g_segments[s].end;
        u32 p = (g_segments[s].start + 7) & ~7u, before = 0;
        while (p + 8 <= end && p != top) {
            const u32 word = *reinterpret_cast<const u32*>(p + 4);
            const u32 size = word & ~3u;
            if (size == 4) break;  // a fencepost: the segment's end as malloc sees it
            const char* why = nullptr;
            if (size < 16 || (size & 7) != 0 || p + size > end) why = "a size that does not fit";
            else if (p + size != top && p + size + 8 <= end) {
                const u32 next_word = *reinterpret_cast<const u32*>(p + size + 4);
                if ((next_word & 1) == 0) {  // this chunk is free
                    const u32 fd = *reinterpret_cast<const u32*>(p + 8), bk = *reinterpret_cast<const u32*>(p + 12);
                    if (!link_ok(fd) || !link_ok(bk)) why = "a free chunk whose links leave the heap";
                    else if (*reinterpret_cast<const u32*>(p + size) != size) why = "a free chunk whose end disagrees";
                }
            }
            if (why) {
                std::snprintf(text, sizeof(text), "%s at 0x%08x (size word 0x%08x, %u chunks in; the one before at 0x%08x)",
                              why, p, word, static_cast<unsigned>(chunks), before);
                if (problem) {
                    *problem = text;
                    return false;
                }
                logf("Heap check (%s): BROKEN: %s\n", when, text);
                Dump(p);
                return false;
            }
            before = p;
            p += size;
            ++chunks;
        }
    }
    // The free lists, as mallinfo walks them (each bin's back links until
    // they come round to the bin): a link into memory that is not a chunk
    // made mallinfo itself fault (a tester's Wii U, after an IOS249
    // reload), so they are followed here first, each step checked.
    for (u32 bin = bins_lo + 16; bin + 16 <= bins_hi; bin += 8) {
        u32 p = *reinterpret_cast<const u32*>(bin + 12), steps = 0;
        while (p != bin) {
            const char* why = nullptr;
            if (!InHeap(p) || (p & 7) != 0) why = "a free list that leaves the heap";
            else if (++steps > chunks + 1) why = "a free list longer than the heap";
            if (why) {
                std::snprintf(text, sizeof(text), "%s: bin 0x%08x, link 0x%08x after %u step(s)", why, bin, p,
                              static_cast<unsigned>(steps));
                if (problem) {
                    *problem = text;
                    return false;
                }
                logf("Heap check (%s): BROKEN: %s\n", when, text);
                if (InHeap(p)) Dump(p);
                return false;
            }
            p = *reinterpret_cast<const u32*>(p + 12);
        }
    }
    if (problem) return true;
    const struct mallinfo info = mallinfo();
    logf("Heap check (%s): OK, %u chunks, %u KiB free in the heap\n", when, static_cast<unsigned>(chunks),
         Kib(static_cast<u32>(info.fordblks)));
    return true;
}
}  // namespace

bool HeapIntact(std::string& problem) {
    __malloc_lock(_REENT);
    const bool ok = WalkHeap("", &problem);
    __malloc_unlock(_REENT);
    return ok;
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
        } else {
            NoteGrowth(start, static_cast<s32>(incr));
        }
    } else if (incr < 0) {
        NoteGrowth(reinterpret_cast<u32>(p), static_cast<s32>(incr));
    }
    _CPU_ISR_Restore(level);
    return p;
}
