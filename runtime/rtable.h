/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Redirect table: the flat, sorted map from virtual disc byte ranges to
 * where the bytes really come from. The loader builds it (see
 * include/riftwii/redirect.hpp) and the resident runtime walks it on every
 * intercepted disc read. This file is freestanding C99: no libc, no
 * allocation, no assumptions beyond <stdint.h>/<stddef.h>, so it compiles
 * unchanged for the host test rig and for the PowerPC runtime.
 *
 * Gaps between entries mean "pass the read through to the real disc at the
 * same offset". The table lives in memory the runtime owns, in the CPU's
 * native byte order (loader and runtime run on the same CPU).
 */
#ifndef RIFTWII_RTABLE_H
#define RIFTWII_RTABLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_MAGIC 0x52575254u /* 'RWRT' */
#define RT_VERSION 3u
#define RT_SECTOR_BYTES 512u

enum rt_kind {
    RT_KIND_PASSTHROUGH = 0, /* only produced by rt_lookup for gaps */
    RT_KIND_ZERO = 1,        /* bytes read as zero */
    RT_KIND_MEM = 2,         /* bytes live in memory at `source` */
    RT_KIND_SD = 3,          /* bytes live on the SD card: sector `source`, byte `skip` */
    RT_KIND_DISC = 4,        /* bytes live on the real disc at byte offset `source` */
    RT_KIND_USB = 5          /* bytes live on the USB drive (d2x's /dev/usb2): sector `source`, byte `skip` */
};

/* Wide entry and loader-side working representation, 24 bytes. The table
 * lives in the game's MEM2 arena, one entry per replaced or created file
 * piece, so every byte here is taken from the game's heaps thousands of times. A disc file is under 4 GiB (the
 * FST's sizes are 32-bit), so one entry's length fits 32 bits. */
typedef struct rt_entry {
    uint64_t vstart;   /* first virtual byte covered */
    uint64_t source;   /* see rt_kind; zero for RT_KIND_ZERO */
    uint32_t length;   /* bytes covered; never zero */
    uint16_t skip;     /* RT_KIND_SD, RT_KIND_USB: byte offset inside sector `source`; else zero */
    uint8_t kind;      /* one of rt_kind, never PASSTHROUGH */
    uint8_t reserved;  /* zero */
} rt_entry;

/* The common layout is 16 bytes. Word addressing covers exclusive virtual
 * ends below 16 GiB; the remaining two end bits preserve byte patches.
 * SD/USB sources are sectors, so 32 source bits cover up to 2 TiB. Any
 * source or virtual end outside this layout selects wide entries for the
 * whole table. A single stride keeps binary search and sequential reads simple. */
typedef struct rt_compact_entry {
    uint32_t vend_words;
    uint32_t source;
    uint32_t length;
    uint16_t skip;
    uint8_t kind;
    uint8_t vend_low;   /* 0..3; exclusive virtual end */
} rt_compact_entry;

#define RT_ENTRY_COMPACT 16u
#define RT_ENTRY_WIDE 24u
#define RT_INDEX_BUCKETS 64u
#define RT_INDEX_MIN_ENTRIES 64u

typedef struct rt_header {
    uint32_t magic;       /* RT_MAGIC */
    uint32_t version;     /* RT_VERSION */
    uint32_t entry_count;
    uint32_t sdio_fd;     /* IOS fd of /dev/sdio/slot0 the loader opened, or 0xFFFFFFFF */
    uint64_t tag;         /* loader-chosen identity (disc id / partition) */
    uint32_t entries_crc; /* rt_crc32 over encoded entries and search index */
    uint32_t entry_bytes; /* RT_ENTRY_COMPACT or RT_ENTRY_WIDE */
    /* entry_count encoded entries follow immediately. Tables with at least
     * RT_INDEX_MIN_ENTRIES then carry RT_INDEX_BUCKETS + 1 indices. These
     * are lower bounds in equal-width buckets of exclusive ends (words for
     * compact, bytes for wide). The first end is the base, and the bucket
     * width is derived from the end span. The CRC includes the index. */
} rt_header;

/* One piece of a request after splitting against the table. */
typedef struct rt_run {
    uint64_t vstart;  /* virtual byte where this run starts */
    uint64_t length;
    uint64_t source;  /* MEM: address; SD: sector; DISC: disc byte offset; else 0 */
    uint32_t skip;    /* SD: bytes into sector `source` where the run starts */
    uint32_t kind;    /* rt_kind, including RT_KIND_PASSTHROUGH */
} rt_run;

enum rt_status {
    RT_OK = 0,
    RT_ERR_MAGIC = -1,
    RT_ERR_VERSION = -2,
    RT_ERR_SIZE = -3,   /* table does not fit in the bytes provided */
    RT_ERR_ORDER = -4,  /* entries unsorted or overlapping */
    RT_ERR_CRC = -5,
    RT_ERR_ENTRY = -6,  /* zero length, bad kind, bad skip, wrapping range */
    RT_ERR_RUNS = -7,   /* more runs than the caller can take */
    RT_ERR_RANGE = -8   /* request wraps past the 64-bit space */
};

/* Reflected CRC-32 (IEEE 802.3), computed bitwise so no table is needed. */
uint32_t rt_crc32(const void* data, size_t length);

/* Bytes occupied by a header, entries and any search index. Returns zero
 * when the format is invalid or the byte count overflows. */
size_t rt_table_bytes(uint32_t entry_count, uint32_t entry_bytes);

/* Choose a single lossless layout for these loader-side entries. */
uint32_t rt_entry_format(const rt_entry* entries, uint32_t entry_count);

/* Encode entries using the initialized header's layout and update its CRC.
 * The caller has reserved rt_table_bytes() bytes. Source and destination
 * must not overlap. Structural validation is a separate final step. */
int rt_encode_entries(rt_header* table, const rt_entry* entries);

/* Decode one entry of a validated table; index must be below entry_count. */
rt_entry rt_read_entry(const rt_header* table, uint32_t index);

/* Full structural check; call once before trusting a table. */
int rt_validate(const rt_header* table, size_t available_bytes);

/*
 * Splits [offset, offset + length) into runs in ascending order. Runs
 * never cross an entry boundary or a gap boundary. Returns RT_OK and sets
 * *run_count, or RT_ERR_RUNS when max_runs is too small (runs already
 * written are valid), or RT_ERR_RANGE. A zero-length request yields no
 * runs. The table must have passed rt_validate.
 */
int rt_lookup(const rt_header* table, uint64_t offset, uint64_t length,
              rt_run* runs, uint32_t max_runs, uint32_t* run_count);

#ifdef __cplusplus
}
#endif

#endif /* RIFTWII_RTABLE_H */
