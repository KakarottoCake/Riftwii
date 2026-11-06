/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The virtual SD card's read-ahead buffer: a game's small read of its card
 * image brings the whole window around it (a FAT cluster, or an even part
 * of one) into memory with one request on the device, and its next reads
 * inside that window are answered from memory.
 *
 * Why: on a Wii each request on the SD card costs about 2 ms whatever its
 * size, and code builds read their images in small pieces. REX's start-up
 * on the console made 13,311 reads of about 1.5 KiB in its first 30 s and
 * spent 25 s of them waiting on the card; only 4-7% of what it read was
 * read again, so keeping old data would not help, but reading each
 * cluster once instead of piece by piece does.
 *
 * The rules (the blob, runtime/vsd, moves the bytes):
 *  - Windows are `window` sectors (a power of two), aligned to the image's
 *    data area so a window is a cluster (or an even part of one), and cut
 *    at the edge of the piece of the image file on the device that holds
 *    the sector: one window is always one request on the device. Images
 *    can be in hundreds of pieces (the console's REX image: 435).
 *  - The buffer holds `slots` windows at once, the one used longest ago
 *    making room for the next: Project M EX Remix's start-up goes back and
 *    forth between a few folders' clusters, and one window alone was read
 *    again and again (12 times the data it asked for, in Dolphin).
 *  - A read is answered from the buffer where it holds the sectors. The
 *    part of a read up to the window holding its last sector goes straight
 *    to the game's buffer as before (a big read stays one big request);
 *    that last window is read into the buffer, then copied.
 *  - A write updates the sectors the buffer holds once the device has
 *    taken them; a write that failed drops the buffer, since the card may
 *    hold part of it.
 *  - Every held sector has a check value, taken when it is read in (or
 *    written), and is handed to the game only while it still matches:
 *    the buffer is memory the game does not know about, and a mod's own
 *    code may write there. In Dolphin, with RiftWii's block 40 KiB lower
 *    than usual, Project M EX Remix's code menu wrote its strings over the
 *    windows; the game took them for folder sectors, failed to open its
 *    files and wrote them back to its image. A window that no longer
 *    matches is dropped (and counted) and read again from the device.
 *
 * Freestanding C99 like rtvsd.c: no libc, no static data, no division
 * (the blob links without libgcc), so it builds unchanged for the host
 * tests and the blob. */
#ifndef RIFTWII_RTVSDRA_H
#define RIFTWII_RTVSDRA_H

#include <stdint.h>

#include "rtvsd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The largest window: 64 sectors (32 KiB), one request on every device
 * (the blob's bounce buffer, d2x's USB driver's pieces). */
#define RTVSD_RA_MAX_SECTORS 64u
#define RTVSD_RA_MIN_SECTORS 8u
#define RTVSD_RA_MAX_SLOTS 8u

/* The buffer. The loader fills `buffer`, `window`, `slots` and `anchor`;
 * all zero means off. All uint32 so the layout is the same on the host. */
struct rtvsd_ra {
    uint32_t buffer;  /* slots * window * 512 bytes, 32-aligned, in MEM2 (the USB drive's DMA reaches MEM2 only) */
    uint32_t window;  /* sectors: a power of two from RTVSD_RA_MIN_SECTORS to RTVSD_RA_MAX_SECTORS */
    uint32_t slots;   /* windows held at once, 1 to RTVSD_RA_MAX_SLOTS */
    uint32_t anchor;  /* windows start at image sectors anchor + k * window: the data area's first sector */
    uint32_t clock;   /* counts uses, for which slot was used longest ago */
    uint32_t filling; /* the slot rtvsd_ra_take handed out */
    uint32_t first[RTVSD_RA_MAX_SLOTS];  /* each slot's image sectors: first .. first + held - 1 */
    uint32_t held[RTVSD_RA_MAX_SLOTS];   /* 0: nothing */
    uint32_t used[RTVSD_RA_MAX_SLOTS];   /* the clock at its last use */
    uint32_t clobbered;                  /* windows dropped because something else wrote over them */
    uint32_t check[RTVSD_RA_MAX_SECTORS];  /* each held sector's check value, by its place in the buffer */
};

/* Whether the buffer is set up (and its window a size it can be, its
 * windows all within RTVSD_RA_MAX_SECTORS). */
int rtvsd_ra_on(const struct rtvsd_ra* ra);

/* The window holding image `sector`: its first sector in `*start` and its
 * length, cut to the piece of the image holding `sector` and to the
 * image's end. 0 when the buffer is off or `sector` is outside the image. */
uint32_t rtvsd_ra_window(const struct rtvsd_ra* ra, const struct rtvsd_card* card, uint32_t sector, uint32_t* start);

/* How many sectors from `sector` the buffer holds, at most `count`, and
 * where the first of them is (`*from`, an address in the buffer). Counts
 * as a use of that window. Those sectors are checked first: a window with
 * one that changed is dropped (`clobbered` counts it) and 0 returned. */
uint32_t rtvsd_ra_holds(struct rtvsd_ra* ra, uint32_t sector, uint32_t count, uint32_t* from);

/* A read of `count` sectors from `sector` not held: how many to send
 * straight to the game's buffer before the window of its last sector, the
 * one read into the buffer. 0 when `sector` is in that window (read it
 * into the buffer now); `count` when the buffer is off. */
uint32_t rtvsd_ra_direct(const struct rtvsd_ra* ra, const struct rtvsd_card* card, uint32_t sector, uint32_t count);

/* A slot for a window about to be read: an empty one, else the one used
 * longest ago, emptied. Returns its address in the buffer. */
uint32_t rtvsd_ra_take(struct rtvsd_ra* ra);

/* The slot rtvsd_ra_take handed out now holds the window from `start`. */
void rtvsd_ra_filled(struct rtvsd_ra* ra, uint32_t start, uint32_t count);

/* The device took a write of `count` sectors from `sector`, `data` their
 * bytes: what the buffer holds of them is updated. */
void rtvsd_ra_wrote(struct rtvsd_ra* ra, uint32_t sector, uint32_t count, const uint8_t* data);

/* Forgets everything the buffer holds (after a write that failed, or
 * before the bounce buffer it lives in is used for something else). */
void rtvsd_ra_drop(struct rtvsd_ra* ra);

#ifdef __cplusplus
}
#endif

#endif
