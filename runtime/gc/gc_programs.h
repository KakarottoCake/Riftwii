/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_GC_PROGRAMS_H
#define RIFTWII_GC_PROGRAMS_H

/* Programs the game loads itself (riftwii/gcprograms.hpp): EA's James Bond
 * games read their own ELF programs from the disc and jump to them. The
 * plugin patched each one at the launch; the blob puts those patches into
 * every read of the disc that brings their bytes (gc_programs_patch), and
 * when one of them starts (gc_on_program_start) sets RiftWii's words in
 * MEM1 to that program's (its hooks' replay slots, the context's function
 * addresses), as the plugin left them for it. Shared with the host tests. */

#include <stdint.h>

#define GC_PROGRAMS_MAGIC 0x52575047u  /* "RWPG" */
#define GC_PROGRAMS_MAX 8u

/* A word written over what the disc has at `offset`. */
struct gc_disc_patch {
    uint32_t offset;
    uint32_t word;
};

/* In MEM2, loader-placed; the context's `programs` points at it. */
struct gc_programs {
    uint32_t magic;
    uint32_t patch_count;
    uint32_t patches;        /* struct gc_disc_patch[patch_count], by offset */
    uint32_t program_count;
    uint32_t word_count;     /* RiftWii's words each program sets */
    uint32_t addresses;      /* uint32_t[word_count]: where they are in MEM1 */
    uint32_t values;         /* uint32_t[program_count][word_count] */
    uint32_t pad;
};

/* Writes the patches that fall in [offset, offset + length) of the disc
 * into `bytes` (what was read from there), byte by byte, as a read may
 * start or end in the middle of a word. The number of patches touched. */
static inline uint32_t gc_programs_patch(const struct gc_disc_patch* patches, uint32_t count, uint8_t* bytes,
                                         uint32_t offset, uint32_t length) {
    uint32_t low = 0, high = count, touched = 0;
    const uint64_t end = (uint64_t)offset + length;
    /* The first patch whose last byte is at or after the read's start. */
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2;
        if ((uint64_t)patches[middle].offset + 4 <= offset) low = middle + 1;
        else high = middle;
    }
    for (; low < count && patches[low].offset < end; ++low) {
        uint32_t b;
        for (b = 0; b < 4; ++b) {
            const uint64_t at = (uint64_t)patches[low].offset + b;
            if (at < offset || at >= end) continue;
            bytes[at - offset] = (uint8_t)(patches[low].word >> (24 - 8 * b));
        }
        ++touched;
    }
    return touched;
}

#endif
