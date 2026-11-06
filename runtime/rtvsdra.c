/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The virtual SD card's read-ahead buffer (rtvsdra.h). */
#include "rtvsdra.h"

int rtvsd_ra_on(const struct rtvsd_ra* ra) {
    const uint32_t w = ra->window;
    const int power_of_two = w != 0 && (w & (w - 1u)) == 0;
    return ra->buffer != 0 && power_of_two && w >= RTVSD_RA_MIN_SECTORS && w <= RTVSD_RA_MAX_SECTORS &&
           ra->slots != 0 && ra->slots <= RTVSD_RA_MAX_SLOTS && ra->slots * w <= RTVSD_RA_MAX_SECTORS;
}

/* A sector's check value: its 128 words folded in one at a time, each
 * mixed in by a multiply (FNV-1a over words) and the high half shifted
 * back down. Not a guard against a forger, only against bytes that
 * changed. A rotate-xor fold gave every sector of one repeated word the
 * same value (the 128 rotations cancel in fours); plain FNV-1a still did
 * for words differing only in their top bit (0x00000000 and 0x80000000),
 * since a multiply by an odd number never moves that bit down. */
static uint32_t sector_check(uint32_t address) {
    const uint32_t* w = (const uint32_t*)(uintptr_t)address;
    uint32_t h = 0x811C9DC5u, i;
    for (i = 0; i < RTVSD_SECTOR_BYTES / 4u; ++i) {
        h = (h ^ w[i]) * 0x01000193u;
        h ^= h >> 16;
    }
    return h;
}

/* Where slot `i`'s bytes start. */
static uint32_t slot_address(const struct rtvsd_ra* ra, uint32_t i) {
    return ra->buffer + i * ra->window * RTVSD_SECTOR_BYTES;
}

uint32_t rtvsd_ra_window(const struct rtvsd_ra* ra, const struct rtvsd_card* card, uint32_t sector, uint32_t* start) {
    const uint32_t mask = ra->window - 1u;
    uint32_t piece_first = 0, piece_count, piece_end, into, window_start, window_end;
    if (!rtvsd_ra_on(ra) || sector >= card->sectors) return 0;
    piece_count = rtvsd_piece(card, sector, &piece_first);
    if (piece_count == 0) return 0;
    /* How far `sector` is into its window: windows line up with the data
     * area's clusters, which start at `anchor`, not at a multiple of the
     * window (sector 2323 in Project M EX Remix's image, 794 in Project+'s).
     * The first window may start before sector 0, the last end past the
     * top of the sector numbers: both are cut. */
    into = (sector - ra->anchor) & mask;
    window_start = into <= sector ? sector - into : 0u;
    window_end = sector + (ra->window - into);
    if (window_end < sector) window_end = 0xFFFFFFFFu;
    /* Cut to the piece holding the sector (one request on the device),
     * and to the image. */
    piece_end = piece_first + piece_count;
    if (window_start < piece_first) window_start = piece_first;
    if (window_end > piece_end) window_end = piece_end;
    if (window_end > card->sectors) window_end = card->sectors;
    *start = window_start;
    return window_end - window_start;
}

uint32_t rtvsd_ra_holds(struct rtvsd_ra* ra, uint32_t sector, uint32_t count, uint32_t* from) {
    uint32_t i;
    if (!rtvsd_ra_on(ra)) return 0;
    for (i = 0; i < ra->slots; ++i) {
        uint32_t into, left;
        if (ra->held[i] == 0 || sector < ra->first[i]) continue;
        into = sector - ra->first[i];
        if (into >= ra->held[i]) continue;
        left = ra->held[i] - into;
        if (left > count) left = count;
        {
            const uint32_t first_place = i * ra->window + into;
            uint32_t k;
            for (k = 0; k < left; ++k) {
                const uint32_t at = slot_address(ra, i) + (into + k) * RTVSD_SECTOR_BYTES;
                if (sector_check(at) != ra->check[first_place + k]) {
                    ra->held[i] = 0;
                    ++ra->clobbered;
                    return 0;
                }
            }
        }
        ra->used[i] = ++ra->clock;
        *from = slot_address(ra, i) + into * RTVSD_SECTOR_BYTES;
        return left;
    }
    return 0;
}

uint32_t rtvsd_ra_direct(const struct rtvsd_ra* ra, const struct rtvsd_card* card, uint32_t sector, uint32_t count) {
    uint32_t last_window = 0;
    if (count == 0) return 0;
    if (rtvsd_ra_window(ra, card, sector + count - 1u, &last_window) == 0) return count;
    return sector < last_window ? last_window - sector : 0;
}

uint32_t rtvsd_ra_take(struct rtvsd_ra* ra) {
    uint32_t i, oldest = 0;
    for (i = 0; i < ra->slots; ++i) {
        if (ra->held[i] == 0) {
            oldest = i;
            break;
        }
        /* Ages, counted back from the clock (it may wrap). */
        if (ra->clock - ra->used[i] > ra->clock - ra->used[oldest]) oldest = i;
    }
    ra->held[oldest] = 0;
    ra->filling = oldest;
    return slot_address(ra, oldest);
}

void rtvsd_ra_filled(struct rtvsd_ra* ra, uint32_t start, uint32_t count) {
    const uint32_t i = ra->filling < ra->slots ? ra->filling : 0u;
    uint32_t k;
    if (count > ra->window) count = ra->window;
    for (k = 0; k < count; ++k) ra->check[i * ra->window + k] = sector_check(slot_address(ra, i) + k * RTVSD_SECTOR_BYTES);
    ra->first[i] = start;
    ra->held[i] = count;
    ra->used[i] = ++ra->clock;
}

void rtvsd_ra_wrote(struct rtvsd_ra* ra, uint32_t sector, uint32_t count, const uint8_t* data) {
    const uint32_t write_end = sector + count;
    uint32_t i;
    if (count == 0) return;
    for (i = 0; i < ra->slots && i < RTVSD_RA_MAX_SLOTS; ++i) {
        const uint32_t held_end = ra->first[i] + ra->held[i];
        uint32_t from, to, n, s;
        const uint8_t* src;
        uint8_t* dst;
        if (ra->held[i] == 0) continue;
        from = sector > ra->first[i] ? sector : ra->first[i];
        to = write_end < held_end ? write_end : held_end;
        if (from >= to) continue;
        src = data + (from - sector) * RTVSD_SECTOR_BYTES;
        dst = (uint8_t*)(uintptr_t)slot_address(ra, i) + (from - ra->first[i]) * RTVSD_SECTOR_BYTES;
        for (n = (to - from) * RTVSD_SECTOR_BYTES; n != 0; --n) *dst++ = *src++;
        for (s = from; s < to; ++s)
            ra->check[i * ra->window + (s - ra->first[i])] =
                sector_check(slot_address(ra, i) + (s - ra->first[i]) * RTVSD_SECTOR_BYTES);
    }
}

void rtvsd_ra_drop(struct rtvsd_ra* ra) {
    uint32_t i;
    for (i = 0; i < RTVSD_RA_MAX_SLOTS; ++i) {
        ra->first[i] = 0;
        ra->held[i] = 0;
    }
}
