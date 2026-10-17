/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GameCube games from an RVZ (docs/RVZ.md, "GameCube games"). A GameCube
 * disc has no partitions and no hashes: the whole disc is raw data, cut
 * into groups of `chunk_bytes` each, stored as they are, compressed with
 * Zstandard, or RVZ-packed (runs of the disc's padding kept as the seed
 * of the generator that made them).
 *
 * The loader writes the RVZ's group table for the disc to the card
 * (sd:/riftwii/rvz/<ID>.groups, 8 bytes a group in disc order, as for
 * Wii games: the file offset / 4, then the stored size with the two
 * flags below) and gives this state the sectors of that file and of the
 * RVZ. A read finds its group, reads the table sector that holds its
 * entry, fetches the group's stored bytes, decodes them into a cache and
 * copies out. The last group decoded is kept, in one of two slots: one
 * for the game's own reads and one for disc audio streaming, so the music
 * read ahead does not throw out the group the game is reading through.
 *
 * Freestanding C99 like rtrvz.c, built into the GameCube blob's RVZ
 * variant (Makefile.runtime) and for the host tests. Reading the card is
 * the caller's: gc_rvz_device_read, defined by the blob (gc_rt.c) and by
 * the tests. Addresses are uintptr_t so the host tests can use their own
 * memory; on the Wii they are 32-bit words like everything else. */
#ifndef RIFTWII_GC_RVZ_H
#define RIFTWII_GC_RVZ_H

#include <stdint.h>

#include "rtrvz.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GC_RVZ_MAGIC 0x52574752u            /* "RWGR" */
#define GC_RVZ_COMPRESSED 0x80000000u       /* stored with Zstandard */
#define GC_RVZ_PACKED 0x40000000u           /* RVZ packing */
#define GC_RVZ_SIZE_MASK 0x3FFFFFFFu
#define GC_RVZ_SECTOR 512u
#define GC_RVZ_ENTRIES_PER_SECTOR 64u
#define GC_RVZ_REQUEST_SECTORS 128u         /* one card request moves at most 64 KiB */
#define GC_RVZ_TABLE_EXTENTS 32u            /* pieces of the group table's file */
#define GC_RVZ_MAX_EXTENTS 1024u            /* pieces of the RVZ file */
#define GC_RVZ_NO_GROUP 0xFFFFFFFFu
#define GC_RVZ_HEADER_BYTES 0x80u           /* the disc's first bytes, kept in the RVZ's own header */
#define GC_RVZ_SLOT_GAME 0u
#define GC_RVZ_SLOT_STREAM 1u
#define GC_RVZ_SLOTS 2u

/* gc_rvz.last_error */
#define GC_RVZ_ERR_PAST_END 1u   /* a read past the disc's end */
#define GC_RVZ_ERR_TABLE 2u      /* the group table's sector could not be read */
#define GC_RVZ_ERR_CARD 3u       /* a group's stored bytes could not be read */
#define GC_RVZ_ERR_TOO_BIG 4u    /* a group larger than its buffer */
#define GC_RVZ_ERR_DECODER 5u    /* the Zstandard decoder did not fit its workspace */
#define GC_RVZ_ERR_DECODE 6u     /* Zstandard refused the group */
#define GC_RVZ_ERR_DATA 7u       /* a group holds less than it should, or its packing is damaged */
#define GC_RVZ_ERR_EXTENT 8u     /* a file sector outside the pieces the loader gave */

/* `count` sectors of a file from `file_sector` lie at `device_sector`. */
struct gc_rvz_extent {
    uint32_t file_sector;
    uint32_t device_sector;
    uint32_t count;
};

/* One kept group: its decoded (or stored) bytes in `buffer`. */
struct gc_rvz_slot {
    uintptr_t buffer;        /* loader-filled, 32-byte aligned */
    uint32_t buffer_bytes;
    uint32_t group;          /* GC_RVZ_NO_GROUP: none */
    uintptr_t data;          /* where its bytes start (0: a group of zeros) */
    uint32_t bytes;
    uint32_t packed;         /* 1: `data` is an RVZ-packed stream */
};

/* In the blob's MEM2 data (loader-placed, gc_rt_context.rvz). */
struct gc_rvz {
    uint32_t magic;               /* GC_RVZ_MAGIC */
    /* Loader-filled. */
    uint32_t disc_bytes;          /* the disc's size: reads past it fail */
    uint32_t chunk_bytes;         /* disc bytes a group holds */
    uint32_t group_count;         /* entries in the group table */
    uintptr_t stored_buffer;      /* a compressed group as stored, 32-byte aligned (0: none is) */
    uint32_t stored_bytes;
    uintptr_t dctx_buffer;        /* the Zstandard decoder's workspace, 8-byte aligned */
    uint32_t dctx_bytes;
    uint32_t extent_count;        /* of `extents` */
    uint32_t table_extent_count;  /* of `table_extents` */
    struct gc_rvz_slot slots[GC_RVZ_SLOTS];
    uint8_t header[GC_RVZ_HEADER_BYTES];
    /* State. */
    uintptr_t dctx;               /* the decoder, set up at the first compressed group */
    uint32_t entry_sector;        /* the table sector in `entries`, GC_RVZ_NO_GROUP: none */
    /* Counters, for the trace and a memory dump. */
    uint32_t groups_loaded;
    uint32_t entry_loads;
    uint32_t card_requests;
    uint32_t failures;
    uint32_t last_error;          /* GC_RVZ_ERR_* */
    uint32_t last_failed_offset;
    uint32_t entries[GC_RVZ_ENTRIES_PER_SECTOR * 2] __attribute__((aligned(32)));  /* one table sector */
    rtrvz_junk junk;
    struct gc_rvz_extent table_extents[GC_RVZ_TABLE_EXTENTS];
    /* Last: the loader makes room for `extent_count` of them only
     * (gc_rvz_state_bytes). */
    struct gc_rvz_extent extents[GC_RVZ_MAX_EXTENTS];
};

/* The state's size with room for `extent_count` pieces of the RVZ. */
#define gc_rvz_state_bytes(extent_count)                                    \
    ((uint32_t)__builtin_offsetof(struct gc_rvz, extents) +                 \
     (uint32_t)(extent_count) * (uint32_t)sizeof(struct gc_rvz_extent))

/* `length` bytes of the disc from `offset` into `dst`, through cache
 * slot `slot`. 1: done; 0: failed (last_error says why). Decodes at most
 * one group for a read that stays inside one group. `io` goes to
 * gc_rvz_device_read as it is. */
int gc_rvz_read(struct gc_rvz* rvz, void* io, uint32_t slot, uint8_t* dst, uint32_t length, uint32_t offset);

/* The caller's: `count` sectors of the card from `sector` into `target`
 * (32-byte aligned). 1: read; 0: failed. */
int gc_rvz_device_read(void* io, uint32_t sector, uint32_t count, uintptr_t target);

#ifdef __cplusplus
}
#endif

#endif
