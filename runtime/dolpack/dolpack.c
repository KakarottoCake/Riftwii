/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The packed DOL's stub (dolpack.h): unpacks the DOL it stands for and
 * starts it, as the Homebrew Channel would have started it unpacked. */
#include <stddef.h>
#include <stdint.h>

#include "dolpack.h"

#define ARGV_MAGIC 0x5f617267u

/* The decoder's workspace: its static context (about 95 KB with a 4 KB
 * internal buffer); the window is the section being written itself. */
#define DOLPACK_WORKSPACE_BYTES (160u * 1024u)

uint32_t dolpack_zstd_init(void* workspace, uint32_t bytes);
int32_t dolpack_zstd_decode(uint32_t dctx, void* dst, uint32_t capacity, const void* src, uint32_t length);
void dolpack_main(void);

extern uint32_t dolpack_argv[6];
extern uint32_t dolpack_table_address;
extern uint8_t __dolpack_bss_start[], __dolpack_bss_end[];

static uint8_t g_workspace[DOLPACK_WORKSPACE_BYTES] __attribute__((aligned(32)));

/* The decoder copies literals and stored blocks with these: word by word
 * where both ends allow it, since they move most of the image. */
void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0) {
        while (n >= 4) {
            *(uint32_t*)(void*)d = *(const uint32_t*)(const void*)s;
            d += 4;
            s += 4;
            n -= 4;
        }
    }
    while (n--) *d++ = *s++;
    return dst;
}

void* memset(void* dst, int value, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = (uint8_t)value;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        while (n--) d[n] = s[n];
    }
    return dst;
}

/* Reached only through the decoder's dictionary cleanup, which frees
 * nothing in a static decoder. */
void free(void* p) { (void)p; }

/* What was written reaches memory, and no stale instructions stay in the
 * instruction cache over it. */
static void sync_range(uint32_t address, uint32_t bytes) {
    const uint32_t end = address + bytes;
    uint32_t line;
    for (line = address & ~31u; line < end; line += 32) __asm__ volatile("dcbst 0,%0" : : "r"(line) : "memory");
    __asm__ volatile("sync" : : : "memory");
    for (line = address & ~31u; line < end; line += 32) __asm__ volatile("icbi 0,%0" : : "r"(line) : "memory");
    __asm__ volatile("sync; isync" : : : "memory");
}

/* A damaged packed DOL: the frame on screen turns red and the stub stops,
 * rather than jumping into half-written code. The frame is the one the
 * video interface shows (its top field's address register). */
static void fail(void) {
    const uint32_t tfbl = *(volatile uint32_t*)0xCC00201Cu;
    const uint32_t physical = (tfbl & 0x10000000u) ? (tfbl & 0x00FFFFFFu) << 5 : (tfbl & 0x00FFFFFFu);
    if (physical >= 0x4000u && physical + 640u * 480u * 2u <= 0x01800000u) {
        volatile uint32_t* frame = (volatile uint32_t*)(uintptr_t)(physical | 0xC0000000u);
        uint32_t i;
        for (i = 0; i < 640u * 480u / 2u; ++i) frame[i] = 0x515A51F0u;  /* red: Y 81, U 90, Y 81, V 240 */
    }
    for (;;) {
    }
}

/* The command line the loader put after the stub, copied to where it
 * would have put it for the unpacked DOL (after its last section and
 * BSS). Done first, while the loader's copy is certainly whole; nothing
 * unpacked goes that far. Returns the copy, or 0 when there is none. */
static char* copy_command_line(const struct dolpack_table* t, uint32_t* length) {
    const char* from = (const char*)(uintptr_t)dolpack_argv[1];
    char* to;
    *length = dolpack_argv[2];
    if (dolpack_argv[0] != ARGV_MAGIC || from == 0 || *length == 0 || *length > 0x10000u) return 0;
    to = (char*)(uintptr_t)((t->end + 31u) & ~31u);
    memmove(to, from, *length);
    to[*length] = 0;
    sync_range((uint32_t)(uintptr_t)to, *length + 1);
    return to;
}

/* Once the sections are in place: the DOL's own "_arg" block points at
 * the copy. libogc builds argc and argv from the line, as after the
 * Homebrew Channel. */
static void point_at_command_line(const struct dolpack_table* t, char* line, uint32_t length) {
    uint32_t* real = (uint32_t*)(uintptr_t)(t->text0 + 4);
    if (line == 0 || real[0] != ARGV_MAGIC) return;
    real[1] = ARGV_MAGIC;
    real[2] = (uint32_t)(uintptr_t)line;
    real[3] = length;
    real[4] = 0;  /* argc, argv and endARGV: libogc's to fill */
    real[5] = 0;
    real[6] = 0;
    sync_range((uint32_t)(uintptr_t)real, 28);
}

void dolpack_main(void) {
    const struct dolpack_table* t = (const struct dolpack_table*)(uintptr_t)dolpack_table_address;
    uint32_t dctx, i, line_length = 0;
    char* line;
    memset(__dolpack_bss_start, 0, (size_t)(__dolpack_bss_end - __dolpack_bss_start));
    if (t == 0 || t->magic != DOLPACK_TABLE_MAGIC || t->version != DOLPACK_TABLE_VERSION ||
        t->count == 0 || t->count > DOLPACK_MAX_SECTIONS) {
        fail();
    }
    line = copy_command_line(t, &line_length);
    dctx = dolpack_zstd_init(g_workspace, sizeof(g_workspace));
    if (dctx == 0) fail();
    for (i = 0; i < t->count; ++i) {
        const struct dolpack_section* s = &t->section[i];
        const int32_t got = dolpack_zstd_decode(dctx, (void*)(uintptr_t)s->address, s->size,
                                                (const uint8_t*)t + s->offset, s->bytes);
        if (got < 0 || (uint32_t)got != s->size) fail();
        sync_range(s->address, s->size);
    }
    if (t->bss_size != 0) {
        memset((void*)(uintptr_t)t->bss_address, 0, t->bss_size);
        sync_range(t->bss_address, t->bss_size);
    }
    point_at_command_line(t, line, line_length);
    ((void (*)(void))(uintptr_t)t->entry)();
    fail();
}
