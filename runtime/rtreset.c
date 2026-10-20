/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rtreset.h"

static uint32_t rtrst_word(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* Code the game runs is among what is written (the runtime's hooks, the
 * packs' patches): each line goes out of the data cache and out of the
 * instruction cache, as the loader does for them at the launch. */
static void rtrst_sync(uintptr_t address, uint32_t length) {
#ifdef RT_TARGET_PPC
    uintptr_t line = address & ~(uintptr_t)31u;
    const uintptr_t end = address + length;
    while (line < end) {
        __asm__ volatile("dcbst 0, %0; sync; icbi 0, %0" : : "r"(line) : "memory");
        line += 32u;
    }
    __asm__ volatile("sync; isync" : : : "memory");
#else
    (void)address;
    (void)length;
#endif
}

int32_t rtrst_apply(const uint8_t* list, uintptr_t bias) {
    uint32_t count;
    uint32_t i;
    const uint8_t* p;
    if (list == 0 || rtrst_word(list) != RTRST_MAGIC) return -1;
    count = rtrst_word(list + 4);
    if (count > RTRST_MAX_RECORDS) return -1;
    p = list + 8;
    for (i = 0; i < count; ++i) {
        const uint32_t address = rtrst_word(p);
        const uint32_t length = rtrst_word(p + 4);
        uint8_t* to = (uint8_t*)(address + bias);
        uint32_t n;
        p += 8;
        /* Word by word: the list's bytes are already in the console's
         * order, and the targets are word-aligned. */
        for (n = 0; n + 4 <= length; n += 4) {
            to[n] = p[n];
            to[n + 1] = p[n + 1];
            to[n + 2] = p[n + 2];
            to[n + 3] = p[n + 3];
        }
        rtrst_sync((uintptr_t)to, length);
        p += length;
    }
    return (int32_t)count;
}
