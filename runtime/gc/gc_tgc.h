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

#endif
