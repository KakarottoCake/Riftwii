/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Which disc reads of a bonus disc's menu start a game packed in a TGC
 * image (gc_on_tgc_read): kept apart from the blob so the host tests can
 * check the rule.
 *
 * Two reads count:
 *  - the read of the TGC's header, at the TGC's first byte. The Zelda
 *    Collector's Edition menu (PZLE01) opens the chosen TGC and reads its
 *    64-byte header just before it starts the game, then loads the game's
 *    program with a loader of its own (execD.img) that drives the disc
 *    interface itself, so none of the program's reads come through the
 *    game's DVDLowRead, where the blob sees reads;
 *  - any read of the TGC's program, for a menu that loads it through its
 *    own DVD calls. */
#ifndef RIFTWII_GC_TGC_H
#define RIFTWII_GC_TGC_H

#include <stdint.h>

/* 1 when a read of `length` bytes at `offset` starts the game of the TGC
 * at `tgc_offset`, whose program lies at [dol_start, dol_end). No sum that
 * could wrap: every value comes from the disc. */
static inline int gc_tgc_read_starts_game(uint32_t tgc_offset, uint32_t dol_start, uint32_t dol_end, uint32_t offset,
                                          uint32_t length) {
    if (length == 0) return 0;
    if (offset == tgc_offset) return 1;
    if (offset >= dol_start) return offset < dol_end;
    return dol_start - offset < length;
}

/* The arguments a bonus disc's menu hands its game (gc_on_tgc_start)
 * are carried whole or not at all: 1 when `count` of them, each
 * `lengths[i]` bytes, fit as "gc_arg=" lines (GC_TGC_ARG_LINE_EXTRA more
 * each) in the `sector_bytes` of exec.txt after `at`, the sector's last
 * two kept; 0 when there are more than GC_TGC_ARGS, one is longer than
 * `longest`, or they do not all fit. No sum that could wrap. */
#define GC_TGC_ARGS 8u
#define GC_TGC_ARG_LINE_EXTRA 8u /* "gc_arg=" and the newline */
static inline int gc_tgc_args_fit(const uint32_t* lengths, uint32_t count, uint32_t longest, uint32_t at,
                                  uint32_t sector_bytes) {
    uint32_t i, room;
    if (count > GC_TGC_ARGS || sector_bytes < 2u || at > sector_bytes - 2u) return 0;
    room = sector_bytes - 2u - at;
    for (i = 0; i < count; ++i) {
        if (lengths[i] > longest) return 0;
        if (room < GC_TGC_ARG_LINE_EXTRA || lengths[i] > room - GC_TGC_ARG_LINE_EXTRA) return 0;
        room -= lengths[i] + GC_TGC_ARG_LINE_EXTRA;
    }
    return 1;
}

#endif
