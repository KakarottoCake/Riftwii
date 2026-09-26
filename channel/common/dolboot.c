// SPDX-License-Identifier: GPL-3.0-or-later
#include "dolboot.h"

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

u8* dolboot_read(const char* path, u32* size, void* (*alloc)(u32 size)) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* data = NULL;
    if (n > 0x100 && (u32)n <= DOL_MAX) data = (u8*)alloc((u32)n);
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) data = NULL;
    fclose(f);
    *size = (u32)n;
    return data;
}

static bool overlaps(u32 lo, u32 hi, u32 other_lo, u32 other_hi) { return lo < other_hi && hi > other_lo; }

bool dolboot_valid(const u8* dol, u32 size) {
    extern u8 __app_start[], __bss_end[];
    const u32 self_lo = (u32)__app_start, self_hi = (u32)__bss_end;
    const u32 file_lo = (u32)dol, file_hi = file_lo + size;
    const DolHeader* h = (const DolHeader*)dol;
    for (int i = 0; i < 18; ++i) {
        if (h->size[i] == 0) continue;
        const u32 lo = h->address[i], hi = lo + h->size[i];
        if (h->offset[i] + h->size[i] > size || lo < MEM1_LO || hi > MEM1_HI) return false;
        if (overlaps(lo, hi, self_lo, self_hi) || overlaps(lo, hi, file_lo, file_hi)) return false;
    }
    if (h->bss_size) {
        const u32 lo = h->bss_address, hi = lo + h->bss_size;
        if (lo < MEM1_LO || hi > MEM1_HI || overlaps(lo, hi, self_lo, self_hi) || overlaps(lo, hi, file_lo, file_hi)) {
            return false;
        }
    }
    return h->entry >= MEM1_LO && h->entry < MEM1_HI;
}

// argv[0] goes where the Homebrew Channel puts it: the program's "_arg"
// block after its entry branch points to the string, placed after its
// BSS, where libogc keeps its arena clear of it.
static void set_argv(const DolHeader* h, const char* path) {
    const u32 text0 = h->address[0];
    struct __argv* args = (struct __argv*)(text0 + 8);
    if (*(u32*)(text0 + 4) != ARGV_MAGIC) return;
    u32 end = 0;
    for (int i = 0; i < 18; ++i) {
        if (h->size[i] && h->address[i] + h->size[i] > end) end = h->address[i] + h->size[i];
    }
    if (h->bss_address + h->bss_size > end) end = h->bss_address + h->bss_size;
    char* line = (char*)((end + 31) & ~31u);
    const u32 len = strlen(path) + 1;
    memcpy(line, path, len);
    line[len] = 0;
    DCFlushRange(line, (len + 32) & ~31u);
    args->argvMagic = ARGV_MAGIC;
    args->commandLine = line;
    args->length = (int)len + 1;
    DCFlushRange(args, sizeof(*args));
}

void dolboot_run(const u8* dol, const char* argv0) {
    static char path[256];
    strncpy(path, argv0, sizeof(path) - 1);
    const DolHeader h = *(const DolHeader*)dol;
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    IRQ_Disable();
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
    set_argv(&h, path);
    ((void (*)(void))h.entry)();
    for (;;) {
    }
}
