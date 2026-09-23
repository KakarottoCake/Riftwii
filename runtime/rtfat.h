/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Resumable FAT32 engine for the resident runtime's save redirection
 * (docs/CONDUCTOR_REVIEW_2.md section 24): the file operations the
 * game's NAND library performs on its data directory, done on one folder
 * of the SD card. Freestanding C99 like rtable.c: no libc, no
 * allocation, no static data (the runtime blob must stay position
 * independent), so it compiles unchanged for the host test rig and for
 * the PowerPC runtime.
 *
 * The engine never does I/O itself. An operation lives in an rtfat_op;
 * rtfat_step advances its state machine until it either needs a transfer
 * of whole 512-byte device blocks (it then says which blocks, which way,
 * and into or out of which buffer, and returns RTFAT_IO) or is finished
 * (RTFAT_DONE, with an ISFS-style result in op->result). The caller
 * performs the transfer however it likes - synchronously, or as an
 * asynchronous SD request whose completion calls rtfat_step again - and
 * calls rtfat_step again with io_status set. The same engine therefore
 * serves the sync and the async IPC entry points.
 *
 * Volume: 512-byte sectors only (what SD cards are formatted with), one
 * directory per operation (the volume's, or the one op->dir_cluster
 * names) that grows by a cluster when its entries run out, 8.3
 * and long names (VFAT) read and written, every FAT copy updated, ISFS
 * error codes. Sizes and positions are 32-bit as
 * ISFS's are. Names are matched case-insensitively (FAT semantics) and
 * listed in their stored case.
 */
#ifndef RIFTWII_RTFAT_H
#define RIFTWII_RTFAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTFAT_SECTOR_BYTES 512u
/* A file name the game may open, create, rename or delete: anything that
 * fits in a 64-byte ISFS path. The Wii's own NAND stops at 12 characters,
 * but Riivolution maps save paths straight to the card, and packs rely on
 * longer names there (Galaxy 63's BlueCoinData.bin). Listings still hand
 * out at most RTFAT_LIST_NAME_MAX characters per name: a longer name is
 * listed under its short 8.3 alias, which opens the same file. */
#define RTFAT_NAME_MAX 63u
#define RTFAT_LIST_NAME_MAX 12u /* ISFS ReadDir name length */
#define RTFAT_SLOT_BYTES 13u /* ReadDir buffer bytes per name (12 characters and a NUL); IOS packs the
                              * names consecutively, each NUL-terminated, so the buffer is never overrun */

/* rtfat_step results. */
#define RTFAT_DONE 0
#define RTFAT_IO 1

/* ISFS result codes (wiibrew /dev/fs). */
#define RTFAT_OK 0
#define RTFAT_EINVAL (-101)
#define RTFAT_EACCESS (-102)
#define RTFAT_ECORRUPT (-103)
#define RTFAT_EEXIST (-105)
#define RTFAT_ENOENT (-106)
#define RTFAT_ENOINODES (-107)
#define RTFAT_ENOSPC (-108)
#define RTFAT_EIO (-114)

/* The geometry the loader hands over (its Fat32Volume mount, translated
 * to device blocks). All fields uint32: layouts match between host and
 * console. */
struct rtfat_volume {
    uint32_t sectors_per_cluster; /* 1..128 */
    uint32_t fat_lba;             /* device block of the first FAT */
    uint32_t fat_count;           /* 1 or 2; every copy is written */
    uint32_t fat_sectors;         /* sectors per FAT copy */
    uint32_t data_lba;            /* device block of cluster 2 */
    uint32_t cluster_count;       /* data clusters, numbered 2 .. cluster_count + 1 */
    uint32_t dir_cluster;         /* first cluster of the directory served */
    uint32_t alloc_hint;          /* next cluster to try when allocating (engine-updated) */
    /* Set when recovery from a failed directory extension cannot prove the
     * mirrored FATs consistent.  Reads may continue; mutations fail EIO
     * until the loader remounts the card. */
    uint32_t mutation_uncertain;
    /* The card's root directory (what ".." names as cluster 0), 0 when
     * unknown; folder creation writes it. */
    uint32_t root_cluster;
};

/* An open file as the engine tracks it. The directory entry's location is
 * kept so the size can be written back after a growing write. */
struct rtfat_file {
    uint32_t first_cluster;  /* 0 for an empty file */
    uint32_t size;
    uint32_t position;
    uint32_t entry_lba;      /* sector holding the short entry */
    uint32_t entry_index;    /* entry index within that sector (0..15) */
    uint32_t cache_index;    /* cluster index (0-based from the file start) of cache_cluster */
    uint32_t cache_cluster;  /* 0 = nothing cached */
};

/* What a directory scan reports about an entry. */
struct rtfat_dirent {
    uint32_t first_cluster;
    uint32_t size;
    uint32_t attributes;     /* FAT attribute byte */
    uint32_t entry_lba;      /* short entry location */
    uint32_t entry_index;
    uint32_t lfn_lba;        /* first long-name entry location (== the short one when none) */
    uint32_t lfn_index;
    uint32_t lfn_count;      /* long-name entries before the short one */
    char name[RTFAT_NAME_MAX + 1];
    char alias[RTFAT_LIST_NAME_MAX + 1];  /* the 8.3 name as displayed */
};

/* Operation kinds. */
#define RTFAT_OP_LOOKUP 1   /* name -> found */
#define RTFAT_OP_READ 2     /* file, buffer, length: at file->position, advances it; result = bytes */
#define RTFAT_OP_WRITE 3    /* file, buffer, length: at file->position, grows the file; result = bytes */
#define RTFAT_OP_CREATE 4   /* name: an empty file (EEXIST when present); found = its entry */
#define RTFAT_OP_DELETE 5   /* name: entries freed, chain released */
#define RTFAT_OP_RENAME 6   /* name -> name2 (EEXIST when name2 is present) */
#define RTFAT_OP_LIST 7     /* file names into `buffer` as 13-byte slots, up to `length` of them; result = count */
#define RTFAT_OP_COUNT 8    /* result = number of files */
#define RTFAT_OP_USAGE 9    /* result = number of files; usage_blocks = 16 KiB blocks their sizes take */
#define RTFAT_OP_MKDIR 10   /* name: a new folder, its "." and ".." written (EEXIST when the name is taken) */
#define RTFAT_OP_NEXT 11    /* the next entry after the cursor, file or folder ("." and ".." skipped), into
                             * `found`, the cursor moved past it; ENOENT at the end */

/*
 * One operation. The two sector buffers are the engine's working memory
 * (a FAT sector and a directory or data sector); `bounce` is a caller-
 * provided buffer of `bounce_bytes` (a multiple of 512, at least 512) for
 * data transfers. All three are 32-byte aligned when the caller's device
 * needs that (the SD path does).
 */
struct rtfat_op {
    uint32_t kind;
    uint32_t state;
    int32_t result;
    /* I/O request, valid after RTFAT_IO: `io_count` blocks from `io_lba`,
     * written to the device when io_write, else read; the memory is
     * `io_buffer`. The caller sets io_status (0 = success, else an error)
     * before calling rtfat_step again. */
    uint32_t io_lba;
    uint32_t io_count;
    uint32_t io_write;
    uint32_t io_buffer;
    int32_t io_status;
    uint32_t io_pending;     /* a transfer was requested and not yet reported */
    uint32_t io_transfers;   /* transfers completed for this operation (diagnostics) */
    /* Parameters. */
    struct rtfat_file* file;
    uint32_t buffer;         /* caller's data buffer (READ/WRITE/LIST) */
    uint32_t length;         /* bytes (READ/WRITE) or name slots (LIST) */
    char name[RTFAT_NAME_MAX + 1];
    char name2[RTFAT_NAME_MAX + 1];
    uint32_t bounce;
    uint32_t bounce_bytes;
    uint32_t want_hidden;    /* 0: normal scan / preserve rename attributes; 1: include hidden
                              * scans and force the new entry hidden; 2: include hidden scans and
                              * force a renamed entry visible.  A creation always sees hidden
                              * names: a name can exist on the card only once. */
    uint32_t dir_cluster;    /* the directory worked in; 0 = the volume's */
    uint32_t want_dirs;      /* LOOKUP: folders match too (only files otherwise) */
    /* NEXT's cursor (in and out): the cluster and sector the scan resumes
     * in and the entry index within that sector; cursor_cluster 0 = the
     * directory's start. */
    uint32_t cursor_cluster;
    uint32_t cursor_sector;
    uint32_t cursor_index;
    /* Results beyond `result`. */
    struct rtfat_dirent found;   /* LOOKUP, CREATE, RENAME (the new entry): the entry */
    struct rtfat_dirent source;  /* RENAME, DELETE: the entry removed */
    uint32_t usage_blocks;
    /* Scratch: the directory scan. */
    uint32_t scan_cluster;
    uint32_t scan_sector;        /* sector within the cluster */
    uint32_t scan_lba;
    uint32_t scan_clusters;      /* clusters visited (loop guard) */
    uint32_t scan_end;           /* an end-of-directory entry was seen */
    uint32_t scan_skip;          /* NEXT: entries of the first sector read that lie before the cursor */
    uint32_t lfn_ok;             /* long name collected so far is ASCII and within RTFAT_NAME_MAX */
    uint32_t lfn_expect;         /* next sequence number expected (counting down), 0 = none */
    uint32_t lfn_sum;            /* checksum the long entries carry */
    uint32_t lfn_count;
    uint32_t lfn_lba;
    uint32_t lfn_index;
    char lfn[RTFAT_NAME_MAX + 1];
    uint32_t count;              /* LIST/COUNT/USAGE: files seen */
    uint32_t list_bytes;         /* LIST: bytes of names written so far */
    /* Scratch: creating an entry. */
    uint8_t short11[11];
    uint8_t nt_flags;
    uint8_t use_lfn;
    uint8_t need_entries;
    uint8_t new_attr;
    uint32_t short_mask;         /* ~1..~9 aliases already taken for the base (bit n-1) */
    uint32_t free_lba;           /* first sector holding need_entries free entries in a row */
    uint32_t free_index;
    uint32_t free_run;           /* consecutive free entries seen at the scan position */
    uint32_t new_cluster;        /* first cluster the new entry gets (RENAME: the old file's) */
    uint32_t new_size;
    const char* new_name;        /* name or name2 */
    uint32_t scan_mode;          /* what the directory scan is for */
    uint32_t next_state;         /* state to enter when a sub-machine finishes */
    /* Scratch: the chain walk and allocation. */
    uint32_t walk_target;        /* cluster index wanted */
    uint32_t walk_index;         /* cluster index reached */
    uint32_t walk_cluster;       /* cluster at walk_index (0 = the chain ended before) */
    uint32_t walk_prev;          /* last cluster of the chain when it ended */
    uint32_t fat_cached_lba;     /* sector in fat_sector, 0 = none */
    uint32_t alloc_scan;         /* cluster being examined for allocation */
    uint32_t alloc_tried;        /* clusters examined */
    uint32_t alloc_copy;         /* FAT copy being written */
    uint32_t alloc_cluster;      /* the cluster just taken */
    uint32_t alloc_link;         /* cluster whose entry must point at alloc_cluster, 0 = none */
    uint32_t free_cluster;       /* DELETE: cluster being released */
    uint32_t free_next;
    /* Scratch: data transfer. */
    uint32_t want;               /* bytes the operation will move */
    uint32_t data_done;          /* bytes moved so far */
    uint32_t chunk_bytes;        /* bytes of the transfer in flight */
    uint32_t chunk_skip;         /* bytes to skip at the start of the bounce */
    uint32_t chunk_lba;
    uint32_t chunk_count;
    uint32_t grow_from;          /* WRITE: size before the operation */
    uint32_t entry_dirty;        /* WRITE: the directory entry must be rewritten */
    uint32_t grow_next;          /* CREATE/RENAME: state after the directory gained room */
    uint32_t dir_grow;           /* the allocation in progress is a directory cluster: zeroed before linked */
    uint32_t dir_txn;            /* directory extension owns alloc_cluster until its first entry is durable */
    uint32_t recovery_copy;      /* FAT copy currently being restored after a failed directory extension */
    uint32_t recovery_failed;    /* a recovery transfer failed: poison the volume after all copies were attempted */
    uint32_t entry_zero;         /* the sector at free_lba is past the last entry: zero it before filling */
    uint8_t fat_sector[RTFAT_SECTOR_BYTES] __attribute__((aligned(32)));
    uint8_t sector[RTFAT_SECTOR_BYTES] __attribute__((aligned(32)));
};

/* Starts an operation: the caller has set the parameters (name/name2
 * NUL-terminated, file/buffer/length/bounce/want_hidden as the kind
 * needs). Clears the scratch and the result. */
void rtfat_begin(struct rtfat_op* op, uint32_t kind);

/* Advances the operation. Returns RTFAT_IO with the transfer request
 * filled in, or RTFAT_DONE with op->result set. After RTFAT_IO the caller
 * performs the transfer, sets op->io_status and calls again. */
int rtfat_step(struct rtfat_volume* vol, struct rtfat_op* op);

/* Helpers exposed for the tests. */
int rtfat_valid_name(const char* name); /* an ISFS name the engine can store */
/* Whether `name` is an 8.3 name storable without long entries; fills the
 * 11-byte short form and the NT case flags either way (the alias base
 * with "~1" when it is not). */
int rtfat_fits_short(const char* name, uint8_t* out11, uint8_t* nt_flags);
uint32_t rtfat_short_checksum(const uint8_t* name11);
uint32_t rtfat_cluster_lba(const struct rtfat_volume* vol, uint32_t cluster);
uint32_t rtfat_cluster_bytes(const struct rtfat_volume* vol);

#ifdef __cplusplus
}
#endif

#endif
