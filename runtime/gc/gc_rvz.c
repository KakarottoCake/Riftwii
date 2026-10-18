/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GameCube disc reads from an RVZ (gc_rvz.h). No globals and no library
 * calls beyond Zstandard's, like the rest of the GameCube blob. */
#include "gc_rvz.h"

#ifdef RT_TARGET_PPC
/* Zstandard for the blob (runtime/resident/rt_zstd.c): addresses are
 * 32-bit words there. */
uint32_t rt_zstd_init(uint32_t workspace, uint32_t bytes);
int32_t rt_zstd_decode(uint32_t dctx, uint8_t* dst, uint32_t capacity, const uint8_t* src, uint32_t length);

static uintptr_t gc_rvz_zstd_init(uintptr_t workspace, uint32_t bytes) {
    return rt_zstd_init((uint32_t)workspace, bytes);
}

static int32_t gc_rvz_zstd_decode(uintptr_t dctx, uint8_t* dst, uint32_t capacity, const uint8_t* src,
                                  uint32_t length) {
    return rt_zstd_decode((uint32_t)dctx, dst, capacity, src, length);
}

/* The decoder (rt_zstd.c) lowers some of its copies to memcpy; nothing
 * else in the blob calls it. */
void* memcpy(void* dst, const void* src, __SIZE_TYPE__ n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}
#else
/* The host tests: the host's Zstandard, with full-width pointers. */
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

static uintptr_t gc_rvz_zstd_init(uintptr_t workspace, uint32_t bytes) {
    return (uintptr_t)ZSTD_initStaticDCtx((void*)workspace, bytes);
}

static int32_t gc_rvz_zstd_decode(uintptr_t dctx, uint8_t* dst, uint32_t capacity, const uint8_t* src,
                                  uint32_t length) {
    const size_t got = ZSTD_decompressDCtx((ZSTD_DCtx*)dctx, dst, capacity, src, length);
    if (ZSTD_isError(got) || got > 0x7FFFFFFFu) return -1;
    return (int32_t)got;
}
#endif

static uint32_t gc_rvz_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void gc_rvz_copy_bytes(uint8_t* dst, const uint8_t* src, uint32_t n) {
    while (n--) *dst++ = *src++;
}

static void gc_rvz_zero(uint8_t* dst, uint32_t n) {
    while (n--) *dst++ = 0;
}

static int gc_rvz_fail(struct gc_rvz* rvz, uint32_t error, uint32_t offset) {
    rvz->failures++;
    rvz->last_error = error;
    rvz->last_failed_offset = offset;
    return 0;
}

/* `count` sectors of a file from its sector `file_sector` into `target`,
 * through the pieces the loader gave: one card request per piece, at
 * most GC_RVZ_REQUEST_SECTORS at a time. 1: read; 0: a sector outside
 * the pieces (*missing set) or a failed request. */
static int gc_rvz_file_read(struct gc_rvz* rvz, void* io, const struct gc_rvz_extent* extents,
                            uint32_t extent_count, uint32_t file_sector, uint32_t count, uintptr_t target,
                            int* missing) {
    *missing = 0;
    while (count != 0) {
        const struct gc_rvz_extent* piece = 0;
        uint32_t i;
        uint32_t n;
        for (i = 0; i < extent_count; ++i) {
            if (file_sector >= extents[i].file_sector && file_sector - extents[i].file_sector < extents[i].count) {
                piece = &extents[i];
                break;
            }
        }
        if (piece == 0) {
            *missing = 1;
            return 0;
        }
        n = piece->count - (file_sector - piece->file_sector);
        if (n > count) n = count;
        if (n > GC_RVZ_REQUEST_SECTORS) n = GC_RVZ_REQUEST_SECTORS;
        rvz->card_requests++;
        if (!gc_rvz_device_read(io, piece->device_sector + (file_sector - piece->file_sector), n, target)) return 0;
        file_sector += n;
        count -= n;
        target += n * GC_RVZ_SECTOR;
    }
    return 1;
}

/* Group `group`'s two words from the table on the card: the sector that
 * holds them is read unless it is the one held. */
static int gc_rvz_entry(struct gc_rvz* rvz, void* io, uint32_t group, uint32_t offset, uint32_t* word0,
                        uint32_t* word1) {
    const uint32_t sector = group / GC_RVZ_ENTRIES_PER_SECTOR;
    const uint8_t* entry;
    if (rvz->entry_sector != sector) {
        int missing;
        rvz->entry_sector = GC_RVZ_NO_GROUP;  /* the buffer is about to change */
        if (!gc_rvz_file_read(rvz, io, rvz->table_extents, rvz->table_extent_count, sector, 1,
                              (uintptr_t)rvz->entries, &missing)) {
            return gc_rvz_fail(rvz, missing ? GC_RVZ_ERR_EXTENT : GC_RVZ_ERR_TABLE, offset);
        }
        rvz->entry_sector = sector;
        rvz->entry_loads++;
    }
    /* Big endian in the file, whatever reads it. */
    entry = (const uint8_t*)rvz->entries + 8u * (group % GC_RVZ_ENTRIES_PER_SECTOR);
    *word0 = gc_rvz_be32(entry);
    *word1 = gc_rvz_be32(entry + 4);
    return 1;
}

/* Makes `group` the one `slot` holds: its stored bytes fetched and, when
 * compressed, decoded. `payload` is how much of the disc it holds. */
static int gc_rvz_load(struct gc_rvz* rvz, void* io, struct gc_rvz_slot* slot, uint32_t group, uint32_t payload,
                       uint32_t offset) {
    uint32_t word0, word1, size, skip, sectors, room;
    uintptr_t target;
    int compressed;
    int missing;
    if (slot->group == group) return 1;
    slot->group = GC_RVZ_NO_GROUP;  /* its buffer is about to change */
    if (!gc_rvz_entry(rvz, io, group, offset, &word0, &word1)) return 0;
    size = word1 & GC_RVZ_SIZE_MASK;
    if (size == 0) {
        /* Every byte zero (WiaAndRvz.md: a group of size 0). */
        slot->data = 0;
        slot->bytes = 0;
        slot->packed = 0;
        slot->group = group;
        return 1;
    }
    /* The file offset is word0 * 4: its sector and the bytes into it,
     * without 64-bit arithmetic for files past 4 GiB. */
    skip = (word0 & 127u) * 4u;
    sectors = (skip + size + GC_RVZ_SECTOR - 1u) / GC_RVZ_SECTOR;
    compressed = (word1 & GC_RVZ_COMPRESSED) != 0;
    target = compressed ? rvz->stored_buffer : slot->buffer;
    room = compressed ? rvz->stored_bytes : slot->buffer_bytes;
    if (target == 0 || sectors > room / GC_RVZ_SECTOR) return gc_rvz_fail(rvz, GC_RVZ_ERR_TOO_BIG, offset);
    if (!gc_rvz_file_read(rvz, io, rvz->extents, rvz->extent_count, word0 >> 7, sectors, target, &missing)) {
        return gc_rvz_fail(rvz, missing ? GC_RVZ_ERR_EXTENT : GC_RVZ_ERR_CARD, offset);
    }
    if (compressed) {
        int32_t got;
        if (rvz->dctx == 0) rvz->dctx = gc_rvz_zstd_init(rvz->dctx_buffer, rvz->dctx_bytes);
        if (rvz->dctx == 0) return gc_rvz_fail(rvz, GC_RVZ_ERR_DECODER, offset);
        got = gc_rvz_zstd_decode(rvz->dctx, (uint8_t*)slot->buffer, slot->buffer_bytes,
                                 (const uint8_t*)(target + skip), size);
        if (got < 0) return gc_rvz_fail(rvz, GC_RVZ_ERR_DECODE, offset);
        slot->data = slot->buffer;
        slot->bytes = (uint32_t)got;
    } else {
        slot->data = target + skip;
        slot->bytes = size;
    }
    slot->packed = (word1 & GC_RVZ_PACKED) != 0;
    /* A packed stream's length says nothing of what it unpacks to; the
     * unpacking checks that itself. */
    if (!slot->packed && slot->bytes < payload) return gc_rvz_fail(rvz, GC_RVZ_ERR_DATA, offset);
    slot->group = group;
    rvz->groups_loaded++;
    return 1;
}

int gc_rvz_read(struct gc_rvz* rvz, void* io, uint32_t slot_index, uint8_t* dst, uint32_t length, uint32_t offset) {
    struct gc_rvz_slot* slot = &rvz->slots[slot_index == GC_RVZ_SLOT_STREAM ? GC_RVZ_SLOT_STREAM : GC_RVZ_SLOT_GAME];
    uint8_t* const start_dst = dst;
    const uint32_t start = offset;
    if (offset > rvz->disc_bytes || length > rvz->disc_bytes - offset || rvz->chunk_bytes == 0) {
        return gc_rvz_fail(rvz, GC_RVZ_ERR_PAST_END, offset);
    }
    while (length != 0) {
        const uint32_t group = offset / rvz->chunk_bytes;
        const uint32_t group_start = group * rvz->chunk_bytes;
        const uint32_t within = offset - group_start;
        const uint32_t left_on_disc = rvz->disc_bytes - group_start;
        const uint32_t payload = left_on_disc < rvz->chunk_bytes ? left_on_disc : rvz->chunk_bytes;
        uint32_t n = payload - within;
        if (n > length) n = length;
        if (group >= rvz->group_count) return gc_rvz_fail(rvz, GC_RVZ_ERR_PAST_END, offset);
        if (!gc_rvz_load(rvz, io, slot, group, payload, offset)) return 0;
        if (slot->data == 0) {
            gc_rvz_zero(dst, n);
        } else if (slot->packed) {
            /* Unpacked straight into the game's buffer. GameCube padding
             * is generated from disc offsets: the group's start is where
             * its stream's first byte sits for the generator. */
            if (rtrvz_unpack((const uint8_t*)slot->data, slot->bytes, group_start, within, dst, n, &rvz->junk) !=
                RTRVZ_OK) {
                slot->group = GC_RVZ_NO_GROUP;
                return gc_rvz_fail(rvz, GC_RVZ_ERR_DATA, offset);
            }
        } else {
            gc_rvz_copy_bytes(dst, (const uint8_t*)(slot->data + within), n);
        }
        dst += n;
        offset += n;
        length -= n;
    }
    /* The disc's first bytes are kept in the RVZ's own header, not in its
     * first group. */
    if (start < GC_RVZ_HEADER_BYTES) {
        const uint32_t end = offset < GC_RVZ_HEADER_BYTES ? offset : GC_RVZ_HEADER_BYTES;
        gc_rvz_copy_bytes(start_dst, rvz->header + start, end - start);
    }
    return 1;
}
