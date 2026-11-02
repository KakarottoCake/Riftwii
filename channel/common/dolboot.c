// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "dolboot.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define DOL_MAX (16u << 20)
#define MEM1_LO 0x80003f00u
#define MEM1_HI 0x81700000u  // above: what IOS and the loaders keep

typedef struct {
    u32 offset[18];
    u32 address[18];
    u32 size[18];
    u32 bss_address;
    u32 bss_size;
    u32 entry;
} DolHeader;

u8* dolboot_read(const char* path, u32* size, void* (*alloc)(u32 size), void (*release)(void* data)) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* data = NULL;
    if (n > 0x100 && (u32)n <= DOL_MAX) data = (u8*)alloc((u32)n);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
        release(data);  // up to 16 MB, for the next path's try
        data = NULL;
    }
    fclose(f);
    *size = (u32)n;
    return data;
}

static bool overlaps(u32 lo, u32 hi, u32 other_lo, u32 other_hi) { return lo < other_hi && hi > other_lo; }

// [address, address + bytes) inside MEM1's free part, the sum not wrapping
// (a damaged header passed as 2 GB copied over MEM1: a black screen).
static bool in_mem1(u32 address, u32 bytes) {
    return address >= MEM1_LO && address <= MEM1_HI && bytes <= MEM1_HI - address;
}

#define DOLBOOT_LINE_MAX 1024
static char line_copy[DOLBOOT_LINE_MAX];

// The end of everything the DOL fills, its sections and its BSS.
static u32 dol_end(const DolHeader* h) {
    u32 end = 0;
    for (int i = 0; i < 18; ++i) {
        if (h->size[i] && h->address[i] + h->size[i] > end) end = h->address[i] + h->size[i];
    }
    // An empty BSS has no validated address. Counting it can wrap the
    // aligned command-line address to zero for a damaged DOL header.
    if (h->bss_size && h->bss_address + h->bss_size > end) end = h->bss_address + h->bss_size;
    return end;
}

// Whether the DOL reads an argument block, from its first text section's
// first bytes (`text0`: in place once copied, or in the file copy).
static bool takes_args(const DolHeader* h, const u8* text0) {
    // A program without a first text section, or one too short for the
    // block, has no "_arg" block to read (MIN-10): its address may be 0.
    if (h->size[0] < 8 + sizeof(struct __argv)) return false;
    return *(const u32*)(text0 + 4) == ARGV_MAGIC;
}

// Where an argument line of `length` bytes goes, right after everything
// the DOL fills, or 0 when it would reach what IOS and the loaders keep.
static u32 line_address(const DolHeader* h, u32 length) {
    const u32 line = (dol_end(h) + 31) & ~31u;
    if (line > MEM1_HI || length > MEM1_HI - line) return 0;
    return line;
}

// Whether the DOL's sections, its BSS or its argument line (when
// `line_at` isn't 0) land on [self_lo, self_hi).
static bool reaches(const DolHeader* h, u32 line_at, u32 length, u32 self_lo, u32 self_hi) {
    for (int i = 0; i < 18; ++i) {
        if (h->size[i] && overlaps(h->address[i], h->address[i] + h->size[i], self_lo, self_hi)) return true;
    }
    if (h->bss_size && overlaps(h->bss_address, h->bss_address + h->bss_size, self_lo, self_hi)) return true;
    return line_at && overlaps(line_at, line_at + length, self_lo, self_hi);
}

// ------------------------------------------------------- the far copy ----
// A DOL that lands where this program is (WiiSX and Wii64 fill MEM1 up to
// about 0x80bc0000, through RiftWii at 0x80a00000) can't be copied by this
// program's own code, which the copy would overwrite while it runs. So, as
// the Homebrew Channel does, the last step goes to a small routine
// (dolfar.S) put where neither program is: the top of MEM2's free part.
// libogc's heap grows from that part's low end and what is taken from its
// top lowers its high end, so nothing of this program is there, and a DOL
// only ever lands in MEM1. The routine follows a plan written next to it:
// the copies, the BSS to clear, the argument line and its "_arg" block.
extern const u8 dolfar_code[], dolfar_code_end[];

#define FAR_CODE_MAX 0x100u  // dolfar.S checks its size against this
#define FAR_STACK 0x100u
#define FAR_COPIES (18 + 3)  // the sections, the BSS, the argument line, the "_arg" block

// The plan as dolfar.S reads it: 32-bit words, whatever the compiler.
typedef struct {
    u32 count;
    u32 copy[FAR_COPIES][3];  // to, from (0: clear), bytes
    // The "_arg" block's first words as the program reads them: the
    // magic, the line's address, its length (struct __argv on the Wii).
    u32 args[3];
    char line[DOLBOOT_LINE_MAX];
} FarPlan;

// The routine, then its plan, then its stack.
#define FAR_PLAN_AT FAR_CODE_MAX
#define FAR_BYTES ((FAR_PLAN_AT + (u32)sizeof(FarPlan) + FAR_STACK + 31u) & ~31u)

// The bytes of the file copy the routine reads: the header and every
// section.
static u32 file_end(const DolHeader* h) {
    u32 end = sizeof(DolHeader);
    for (int i = 0; i < 18; ++i) {
        if (h->size[i] && h->offset[i] + h->size[i] > end) end = h->offset[i] + h->size[i];
    }
    return end;
}

// Where the far copy goes: the top of [free_lo, free_hi), clear of the
// file copy at [file_lo, file_hi) and of everything the plan writes. 0 when
// it doesn't fit there.
static u32 far_block_in(u32 free_lo, u32 free_hi, const DolHeader* h, u32 file_lo, u32 file_hi, u32 line_at,
                        u32 length) {
    free_hi &= ~31u;
    if (free_hi < free_lo || free_hi - free_lo < FAR_BYTES) return 0;
    const u32 lo = free_hi - FAR_BYTES;
    if (overlaps(lo, free_hi, file_lo, file_hi)) return 0;
    if (reaches(h, line_at, length, lo, free_hi)) return 0;
    return lo;
}

static void far_add(FarPlan* plan, u32 to, u32 from, u32 bytes) {
    plan->copy[plan->count][0] = to;
    plan->copy[plan->count][1] = from;
    plan->copy[plan->count][2] = bytes;
    ++plan->count;
}

// Fills the plan in the order the in-place start below follows: the
// sections, the BSS cleared, then (when `line_at` isn't 0) the argument
// line and the "_arg" block, both staged in the plan itself. `plan_at` and
// `file_at` are the plan's and the file copy's addresses on the Wii.
static void far_plan_fill(FarPlan* plan, u32 plan_at, const DolHeader* h, u32 file_at, u32 line_at, const char* text,
                          u32 length) {
    plan->count = 0;
    for (int i = 0; i < 18; ++i) {
        if (h->size[i]) far_add(plan, h->address[i], file_at + h->offset[i], h->size[i]);
    }
    if (h->bss_size) far_add(plan, h->bss_address, 0, h->bss_size);
    if (!line_at) return;
    memcpy(plan->line, text, length);
    far_add(plan, line_at, plan_at + (u32)offsetof(FarPlan, line), length);
    plan->args[0] = ARGV_MAGIC;
    plan->args[1] = line_at;
    plan->args[2] = length;
    far_add(plan, h->address[0] + 8, plan_at + (u32)offsetof(FarPlan, args), sizeof(plan->args));
}

// The DOL checks, with the file copy at `file_lo`, this program at
// [self_lo, self_hi) and MEM2's free part at [free_lo, free_hi) given, so
// host tests can place them.
static bool dol_fits(const DolHeader* h, u32 size, u32 file_lo, u32 self_lo, u32 self_hi, u32 free_lo, u32 free_hi) {
    const u32 file_hi = file_lo + size;
    bool entry_loaded = false;
    for (int i = 0; i < 18; ++i) {
        if (h->size[i] == 0) continue;
        if (h->offset[i] > size || h->size[i] > size - h->offset[i] || !in_mem1(h->address[i], h->size[i])) return false;
        const u32 lo = h->address[i], hi = lo + h->size[i];
        if (overlaps(lo, hi, file_lo, file_hi)) return false;
        if (h->entry >= lo && h->entry < hi) entry_loaded = true;
    }
    if (h->bss_size) {
        if (!in_mem1(h->bss_address, h->bss_size)) return false;
        if (overlaps(h->bss_address, h->bss_address + h->bss_size, file_lo, file_hi)) return false;
    }
    // A MEM1 address alone is not executable: it must come from this DOL.
    if (!entry_loaded) return false;
    // Over this program, the far copy must have its room.
    return !reaches(h, 0, 0, self_lo, self_hi) || far_block_in(free_lo, free_hi, h, file_lo, file_hi, 0, 0) != 0;
}

// The running program is [__app_start, __bss_end): libogc keeps its main
// and exception stacks in .bss (default_stacks.o, checked in the forwarder's
// and installer's link maps), so they are inside it too.
bool dolboot_valid(const u8* dol, u32 size) {
    extern u8 __app_start[], __bss_end[];
    return dol_fits((const DolHeader*)dol, size, (u32)dol, (u32)__app_start, (u32)__bss_end, (u32)SYS_GetArena2Lo(),
                    (u32)SYS_GetArena2Hi());
}

// Where dolboot_run_line puts the far copy for `dol` with an argument line
// of `length` bytes, or 0 when the DOL is copied in place.
static u32 far_address(const u8* dol, u32 length) {
    extern u8 __app_start[], __bss_end[];
    const DolHeader* h = (const DolHeader*)dol;
    const u32 line_at = takes_args(h, dol + h->offset[0]) ? line_address(h, length) : 0;
    if (!reaches(h, line_at, length, (u32)__app_start, (u32)__bss_end)) return 0;
    return far_block_in((u32)SYS_GetArena2Lo(), (u32)SYS_GetArena2Hi(), h, (u32)dol, (u32)dol + file_end(h), line_at,
                        length);
}

u32 dolboot_far_address(const u8* dol, u32 length) { return far_address(dol, length); }

static void set_argv(const DolHeader* h, const char* text, u32 length) {
    if (!takes_args(h, (const u8*)h->address[0])) return;
    struct __argv* args = (struct __argv*)(h->address[0] + 8);
    const u32 line = line_address(h, length);
    if (!line) return;
    // The line and its closing zero stay off this program, still running
    // (a DOL wholly below it would have its line written over this code
    // and `path` itself).
    extern u8 __app_start[], __bss_end[];
    if (overlaps(line, line + length, (u32)__app_start, (u32)__bss_end)) return;
    memcpy((char*)line, text, length);
    DCFlushRange((char*)line, (length + 31) & ~31u);
    args->argvMagic = ARGV_MAGIC;
    args->commandLine = (char*)line;
    args->length = (int)length;
    DCFlushRange(args, sizeof(*args));
}

void dolboot_system_call_vector(void) {
    static const u32 handler[] = {
        0x7D30FAA6,  // mfspr r9, HID0
        0x612A0008,  // ori   r10, r9, 8
        0x7D50FBA6,  // mtspr HID0, r10
        0x4C00012C,  // isync
        0x7C0004AC,  // sync
        0x7D30FBA6,  // mtspr HID0, r9
        0x4C000064,  // rfi
    };
    u32* const vector = (u32*)0x80000C00;
    for (unsigned i = 0; i < sizeof(handler) / sizeof(handler[0]); ++i) vector[i] = handler[i];
    DCFlushRange(vector, sizeof(handler));
    ICInvalidateRange(vector, sizeof(handler));
}

// The routine and its plan to `at`, then into the routine, with
// interrupts already off.
static void far_run(u32 at, const DolHeader* h, const u8* dol, const char* text, u32 length) {
    u8* const block = (u8*)at;
    memcpy(block, dolfar_code, (u32)(dolfar_code_end - dolfar_code));
    FarPlan* const plan = (FarPlan*)(block + FAR_PLAN_AT);
    const u32 line_at = takes_args(h, dol + h->offset[0]) ? line_address(h, length) : 0;
    far_plan_fill(plan, (u32)plan, h, (u32)dol, line_at, text, length);
    DCFlushRange(block, FAR_BYTES);
    ICInvalidateRange(block, FAR_BYTES);
    dolboot_system_call_vector();
    void (*const routine)(u32 plan, u32 entry, u32 stack_top) = (void (*)(u32, u32, u32))at;
    routine((u32)plan, h->entry, at + FAR_BYTES);
    for (;;) {
    }
}

void dolboot_run_line(const u8* dol, const char* line, u32 length) {
    u32 n = length;
    if (n > sizeof(line_copy)) n = sizeof(line_copy);
    if (n != 0) memcpy(line_copy, line, n);
    while (n > 0 && line_copy[n - 1] != 0) --n;
    if (n == 0) {
        line_copy[0] = 0;
        n = 1;
    }
    const DolHeader h = *(const DolHeader*)dol;
    const u32 far_at = far_address(dol, n);
    extern u8 __app_start[], __bss_end[];
    if (!far_at && reaches(&h, 0, 0, (u32)__app_start, (u32)__bss_end)) {
        // dolboot_valid found the far copy room, which a heap grown since
        // has taken. Never copy over this program: the Wii Menu instead.
        SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
        for (;;) {
        }
    }
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    IRQ_Disable();
    if (far_at) far_run(far_at, &h, dol, line_copy, n);
    for (int i = 0; i < 18; ++i) {
        if (h.size[i] == 0) continue;
        memmove((void*)h.address[i], dol + h.offset[i], h.size[i]);
        DCFlushRange((void*)h.address[i], h.size[i]);
        ICInvalidateRange((void*)h.address[i], h.size[i]);
    }
    if (h.bss_size) {
        memset((void*)h.bss_address, 0, h.bss_size);
        DCFlushRange((void*)h.bss_address, h.bss_size);
    }
    set_argv(&h, line_copy, n);
    dolboot_system_call_vector();
    ((void (*)(void))h.entry)();
    for (;;) {
    }
}

void dolboot_run(const u8* dol, const char* argv0) {
    static char single[256];
    u32 n = (u32)strlen(argv0);
    if (n > sizeof(single) - 2) n = sizeof(single) - 2;
    memcpy(single, argv0, n);
    single[n] = 0;
    single[n + 1] = 0;
    dolboot_run_line(dol, single, n + 2);
}
