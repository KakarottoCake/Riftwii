/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_RT_HOOK_H
#define RIFTWII_RT_HOOK_H

/*
 * Layout shared by the resident runtime (freestanding, position-independent
 * PowerPC code copied to the top of the MEM1 arena; its table and buffers
 * go to the top of the MEM2 arena) and the loader that installs it.
 * Every field is a 32-bit unsigned (or a struct of them) so the layout is
 * identical on the host, where the C part is unit-tested, and on the
 * console.
 *
 * The blob starts with rt_blob_header. The loader copies the blob, fills the
 * context, writes the four original instructions of the hooked function into
 * the replay slot, patches the continuation slot with an absolute jump to the
 * hooked function + 16, and finally overwrites the hooked function's first
 * four instructions with an absolute jump to the trampoline.
 *
 * Redirection (E3+): when a DVDLowRead touches the redirect table, the hook
 * swaps the game's IPC callback for the runtime's completion entry and
 * remembers the game's callback in a pending record. The disc read runs as
 * usual; on its reply the completion entry rewrites the MEM/ZERO runs in
 * the game's buffer and then tail-calls the game's callback, so the DVD
 * driver always sees an ordinary asynchronous completion.
 *
 * Virtual window (E5+): files the loader resized or created live at
 * partition offsets above any physical disc (word offsets from
 * virtual_start_words). A read there is rewritten to fetch the same length
 * from the partition start, so the drive never sees the virtual offset,
 * and on completion every byte of the buffer is supplied from the table
 * (gaps read as zero).
 *
 * SD-backed runs (E4+): after the disc reply, the completion entry fetches
 * each SD run in chunks through the game's IOS_IoctlvAsync on the
 * /dev/sdio/slot0 fd the loader opened (SENDCMD CMD18 into the record's
 * bounce buffer, then copied into the game's buffer), registering itself
 * as the callback of every request; the game's callback runs when the
 * last chunk has landed. The card is left selected by the loader.
 *
 * DISC runs (E7+): original bytes of a relocated or partially patched
 * file are fetched the same way with a DVDLowRead on the game's /dev/di
 * fd through the unhooked IOS_IoctlAsync entry (the replay slot), 32-byte
 * aligned into the bounce buffer.
 */

#include <stdint.h>

#include "rtable.h"
#include "rtfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT_BLOB_MAGIC 0x5257484Bu    /* 'RWHK' */
#define RT_BLOB_VERSION 4u
#define RT_CONTEXT_MAGIC 0x52574358u /* 'RWCX' */

/* The resident table always has these entries, even when a game's DOL did
 * not link one of the SDK forms.  Async commands 1..7 occupy 0..6; sync
 * commands 1..7 occupy 7..13.  Loader installation decides which nonzero
 * SDK entry points are actually patched. */
#define RT_IPC_COMMANDS 7u
#define RT_IPC_ENTRIES 14u
#define RT_IPC_ASYNC(command) ((uint32_t)(command) - 1u)
#define RT_IPC_SYNC(command) (RT_IPC_COMMANDS + (uint32_t)(command) - 1u)
#define RT_IPC_ASYNC_IOCTL RT_IPC_ASYNC(6u)

/* rt_context.flags */
#define RT_FLAG_GECKO 0x1u /* report DI reads over the USB Gecko in EXI channel gecko_channel */
#define RT_FLAG_FS 0x2u    /* route savegame calls through rtfs (docs section 24) */

#define RT_MAX_PENDING 4u /* outstanding redirected reads (the DVD driver issues one at a time) */
#define RT_MAX_RUNS 8u    /* pieces one read may split into; more passes through unmodified */
#define RT_GECKO_MAX_FAILURES 32u /* refused bytes after which Gecko reporting turns itself off */
#define RT_BOUNCE_BYTES 0x8000u   /* bytes one SD request fetches (64 sectors), per pending record */

/* rt_pending.phase */
#define RT_PHASE_DISC 0u     /* waiting for the disc reply */
#define RT_PHASE_SD 1u       /* an SD request is in flight */
#define RT_PHASE_DISC_RUN 2u /* a DVDLowRead for a DISC run is in flight */

/* DI results as the DVD driver sees them (wiibrew /dev/di). */
#define RT_DI_SUCCESS 1
#define RT_DI_ERROR 2 /* handed to the game when an SD read fails: the driver's error path, not silence */

struct rt_blob_header {
    uint32_t magic;                        /* RT_BLOB_MAGIC */
    uint32_t version;                      /* RT_BLOB_VERSION */
    uint32_t blob_size;                    /* bytes, header included */
    uint32_t context_offset;               /* struct rt_context */
    uint32_t hook_offset[RT_IPC_ENTRIES];  /* wrapper entry points */
    uint32_t replay_offset[RT_IPC_ENTRIES]; /* 4 displaced instructions, loader-filled */
    uint32_t continue_offset[RT_IPC_ENTRIES]; /* absolute continuation jumps */
    uint32_t complete_di_offset;           /* completion entry the hook installs as the IPC callback */
    uint32_t complete_fs_offset;           /* savegame completion entry (slice 4B3) */
};

typedef char rt_blob_header_layout[(sizeof(struct rt_blob_header) == 192u) ? 1 : -1];

/* /dev/sdio/slot0 SENDCMD request (wiibrew, libogc wiisd.c). 36 bytes. */
struct rt_sdio_request {
    uint32_t cmd;
    uint32_t cmd_type;
    uint32_t rsp_type;
    uint32_t arg;
    uint32_t blk_cnt;
    uint32_t blk_size;
    uint32_t dma_addr;
    uint32_t isdma;
    uint32_t pad0;
};

/* One IPC vector. */
struct rt_ioctlv {
    uint32_t data;
    uint32_t len;
};

/*
 * One redirected read in flight. The IPC buffers (request, response,
 * vectors) each sit in their own 32-byte lines because IOS flushes and
 * invalidates them by line; the runs are computed once when the read is
 * taken (the hook and the completion entry may run on an interrupted
 * thread's stack, so no scratch array lives on the stack). 480 bytes.
 */
struct rt_pending {
    uint32_t in_use;
    uint32_t callback;     /* the game's IPC callback (may be 0) */
    uint32_t user_data;    /* the game's user data */
    uint32_t out;          /* the game's destination buffer */
    uint32_t length;       /* bytes requested */
    uint32_t word_offset;  /* disc offset in 4-byte words, as the game asked */
    uint32_t is_virtual;   /* the offset lies in the virtual window: nothing came from the disc */
    uint32_t run_count;
    /* SD chain state. */
    uint32_t phase;        /* RT_PHASE_* */
    uint32_t run_index;    /* run being served */
    uint32_t run_done;     /* bytes of that run already in the game's buffer */
    uint32_t chunk_bytes;  /* bytes of the run the in-flight request delivers */
    uint32_t chunk_skip;   /* bytes to skip at the start of the bounce buffer */
    uint32_t bounce;       /* RT_BOUNCE_BYTES, 32-byte aligned (loader-filled) */
    uint32_t di_result;    /* the disc reply, handed to the game at the end */
    uint32_t reserved;
    struct rt_sdio_request request;  /* offset 0x40 */
    uint32_t pad_request[7];
    uint32_t response[4];            /* offset 0x80 */
    uint32_t pad_response[4];
    struct rt_ioctlv vec[3];         /* offset 0xA0 */
    uint32_t pad_vec[2];
    uint32_t di_command[8];          /* offset 0xC0: DVDLowRead block for DISC runs */
    rt_run runs[RT_MAX_RUNS];        /* offset 0xE0 */
};

/* Async game callbacks are plain C functions taking (result, user_data).
 * The completion entry tail-calls them from the IPC interrupt, as IOS
 * would; the dispatcher calls one directly only as a last resort (no
 * IOS round trip could be issued), and on the host, where the address
 * would truncate, tests observe that through a hook instead. */
typedef void (*rt_game_callback_fn)(int32_t result, uint32_t user_data);

/*
 * Savegame requests (docs section 24). One request runs at a time on the
 * one engine record; the FS completion entry is the callback of every
 * IOS request the runtime issues for it, told by the tag which record
 * to continue:
 *   FILE    the transfer of the in-flight request (tag: &pend); the
 *           completion sets its status, steps the engine, issues the next
 *           transfer or hands the game's callback to the tail call.
 *   SNOOP   an async open of /dev/fs replayed to IOS with our callback in
 *           place of the game's (tag: a snoop slot); the completion learns
 *           the fd and passes the game's callback on.
 *   DELIVER a ready result for an async call that completed without I/O
 *           (tag: a deliver slot), carried by a null IOS round trip (an SD
 *           GETSTATUS through the unhooked async ioctl) so the game's
 *           callback runs from the IPC interrupt after the call returned,
 *           never inside it.
 * Arrivals while the engine is busy: async ones queue (RT_FS_QUEUE deep,
 * started from the completion that frees the engine), sync ones wait on
 * the game's thread with interrupts on, bounded by the time base. Claims
 * of the engine run with interrupts off: the game's own IPC callbacks may
 * issue async calls, so hooks run in both contexts.
 *
 * A rename from outside the directory into it (the SDK's safe write: a
 * file written under /tmp, then moved; Mario Kart Wii's banner.bin and
 * rksys.dat) imports the file: read from NAND through the game's own
 * synchronous functions in RT_FS_IMPORT_BYTES pieces, written into the
 * card, deleted from NAND. That runs on the game's thread: from the sync
 * hook, or from the async hook when it was called with interrupts on
 * (a thread); from an IPC callback it is refused with -102. A rename out
 * of the directory is refused with -102.
 */
#define RT_FS_SNOOPS 2u
#define RT_FS_DELIVERS 4u
#define RT_FS_QUEUE 4u
#define RT_FS_BOUNCE_BYTES 0x8000u    /* one transfer moves up to 64 sectors */
#define RT_FS_IMPORT_BYTES 0x8000u    /* one NAND read of an imported file */
#define RT_FS_OP_FILE 1u
#define RT_FS_OP_SNOOP 2u
#define RT_FS_OP_DELIVER 3u
#define RT_FS_WAIT_TICKS 607500000u    /* 10 s of the time base (60.75 MHz) a sync call waits for the engine */
#define RT_SDIO_GETSTATUS 0x0Bu        /* the null round trip (wiibrew /dev/sdio, libogc wiisd.c) */

struct rt_fs_pend {
    uint32_t in_use;
    uint32_t kind;       /* RT_FS_OP_* */
    uint32_t callback;   /* SNOOP, DELIVER: the game's IPC callback */
    uint32_t user_data;  /* SNOOP, DELIVER: the game's user data */
    int32_t result;      /* DELIVER: the result to hand over */
    uint32_t reserved[3];
    char path[64];       /* SNOOP: the opened path, for the /dev/fs compare */
    uint32_t status[8] __attribute__((aligned(32)));  /* DELIVER: GETSTATUS's out word, its own line */
};

/* An async request that arrived while the engine was busy. */
struct rt_fs_queued {
    uint32_t in_use;
    uint32_t entry_index;
    struct rtfs_ipc ipc;
};

/* Savegame FS interception state. Lives in a loader-owned block (MEM2
 * data area on the console, 32-byte aligned) so struct rt_context stays
 * 2048 bytes; the context holds only a pointer. The IPC blocks and the
 * bounce buffer sit in their own cache lines (IOS DMA). */
struct rt_fs_state {
    struct rtfs_context fs;
    struct rtfs_request req;       /* the one request in flight */
    struct rtfs_request probe;     /* classification scratch (rtfs_probe) */
    /* Loader-filled: the game's synchronous functions (their replay
     * slots when hooked), 0 = unknown. */
    uint32_t complete_fs;          /* rt_complete_fs entry address */
    uint32_t open_sync;            /* IOS_Open: /dev/fs call-through, imports */
    uint32_t ioctlv_sync;          /* IOS_Ioctlv: the sync path's transfers */
    uint32_t close_sync;           /* IOS_Close, IOS_Read, IOS_Ioctl: imports */
    uint32_t read_sync;
    uint32_t ioctl_sync;
    /* State and counters. */
    uint32_t dead;                 /* an engine anomaly left the card image uncertain: everything answers -114 */
    uint32_t queue_head;
    uint32_t queue_count;
    uint32_t transfers;            /* SD requests issued for FS operations */
    uint32_t failures;             /* of those, refused or failed */
    uint32_t deferred;             /* results carried by a null round trip */
    uint32_t inline_deliveries;    /* callbacks called directly (no round trip possible) */
    uint32_t queued;               /* async arrivals queued behind a busy engine */
    uint32_t waits;                /* sync arrivals that waited */
    uint32_t wait_timeouts;        /* of those, answered -114 after RT_FS_WAIT_TICKS */
    uint32_t fs_fd_learned;        /* /dev/fs fds learned (snoop or sync call-through) */
    uint32_t imports;              /* renames into the directory served by importing the file */
    uint32_t import_failures;      /* of those, failed (the destination is then absent) */
    uint32_t import_refused;       /* renames across the boundary refused with -102 */
    uint32_t reserved[3];
    struct rt_fs_pend pend;
    struct rt_fs_pend snoop[RT_FS_SNOOPS];
    struct rt_fs_pend deliver[RT_FS_DELIVERS];
    struct rt_fs_queued queue[RT_FS_QUEUE];
    struct rt_sdio_request request __attribute__((aligned(32)));
    uint32_t pad_request[7];
    uint32_t response[8] __attribute__((aligned(32)));
    struct rt_ioctlv vec[3] __attribute__((aligned(32)));
    uint32_t pad_vec[2];
    uint32_t stats[8] __attribute__((aligned(32)));      /* an import's GetFileStats answer */
    struct rtfs_attr_block attr __attribute__((aligned(32)));  /* an import's CreateFile block */
    uint8_t bounce[RT_FS_BOUNCE_BYTES] __attribute__((aligned(32)));
    uint8_t import[RT_FS_IMPORT_BYTES] __attribute__((aligned(32)));
};

/* 2048 bytes. */
struct rt_context {
    uint32_t magic;               /* RT_CONTEXT_MAGIC (non-zero so the struct lives in .data) */
    uint32_t flags;               /* RT_FLAG_* */
    uint32_t gecko_channel;       /* EXI channel of the USB Gecko (1 = memory card slot B) */
    uint32_t original_ioctl_async; /* address of the hooked IOS_IoctlAsync, for diagnostics */
    /* Counters, updated by the hook. */
    uint32_t ioctl_async_calls;   /* every IOS_IoctlAsync call seen */
    uint32_t di_reads;            /* DVDLowRead (ioctl 0x71) calls */
    uint32_t di_read_bytes_lo;    /* bytes requested by those reads, 64-bit */
    uint32_t di_read_bytes_hi;
    uint32_t di_fd;               /* the fd the game uses for /dev/di, learned from the first read */
    uint32_t last_di_word_offset; /* of the most recent read */
    uint32_t last_di_length;
    uint32_t gecko_failures;      /* Gecko bytes the adapter did not accept; RT_GECKO_MAX_FAILURES clears the flag */
    /* Redirection. */
    uint32_t table;               /* rt_header* of the redirect table, 0 = none (loader-filled) */
    uint32_t complete_entry;      /* absolute address of the completion entry (loader-filled) */
    uint32_t redirected_reads;    /* reads with at least one MEM/ZERO/SD run */
    uint32_t pending_overflow;    /* reads passed through unexamined because no record was free */
    uint32_t run_overflow;        /* reads passed through because they split into > RT_MAX_RUNS */
    uint32_t last_checksum;       /* of the redirected bytes of the last completed read */
    uint32_t completions;         /* completion entry invocations */
    uint32_t virtual_start_words; /* first word offset of the virtual window, 0 = none (loader-filled) */
    uint32_t virtual_reads;       /* reads at or above it, answered without their offset reaching the drive */
    /* SD card (loader-filled). */
    uint32_t sdio_fd;             /* /dev/sdio/slot0 fd, card initialised and selected; 0xFFFFFFFF = none */
    uint32_t sdio_sdhc;           /* 1: CMD18 takes a sector number, 0: a byte offset */
    uint32_t ioctlv_async;        /* the game's IOS_IoctlvAsync, 0 = unknown (SD runs then fail) */
    uint32_t sd_requests;         /* SD requests issued */
    uint32_t sd_failures;         /* SD requests refused or failed; the read then completes with RT_DI_ERROR */
    /* Disc (loader-filled entry). */
    uint32_t di_read_entry;       /* the unhooked IOS_IoctlAsync: the blob's replay slot */
    uint32_t disc_requests;       /* DVDLowReads issued for DISC runs */
    uint32_t disc_failures;       /* of those, refused or failed */
    /* Savegame FS interception (loader-filled). */
    uint32_t fs_state;            /* struct rt_fs_state*, 0 = none */
    uint32_t fs_hijacked;         /* synchronous FS calls answered from the card image */
    uint32_t reserved;
    struct rt_pending pending[RT_MAX_PENDING];
};

/*
 * Called by the trampoline with the eight IOS_IoctlAsync arguments
 * (fd, ioctl, in, in_len, out, out_len, callback, user data) saved on the
 * stack as register images (uintptr_t is the register width: 32 bits on
 * the console, wider on the host where this is unit-tested). May rewrite
 * the callback and user data (the trampoline restores the registers from
 * this array). Returns 0 to let the original function run, non-zero to
 * return `*result` to the caller instead (unused so far).
 */
int rt_on_ioctl_async(struct rt_context* ctx, uintptr_t* args, uint32_t* result);

/* Common entry used by all fourteen wrappers. Async IOS_Ioctl keeps the
 * legacy DI path for disc reads (entry RT_IPC_ASYNC_IOCTL) and otherwise
 * joins the async savegame path; the seven synchronous entries route
 * savegame calls through rtfs when RT_FLAG_FS is set (slice 4B2); every
 * other entry replays its original SDK code. */
int rt_on_ipc(struct rt_context* ctx, uint32_t entry_index, uintptr_t* args, uint32_t* result);

/* Savegame completion entry: IOS invokes it as the callback of every
 * request the runtime issued for the savegame path (see rt_fs_state).
 * Hands back the game's callback and user data for the asm tail call, or
 * a zero callback while more of our own requests are in flight; `result`
 * (in: the IPC result, out: what the game's callback receives) is
 * replaced by the request's result for FILE and DELIVER. */
void rt_on_fs_complete(struct rt_context* ctx, int32_t* result, void* tag,
                       uintptr_t* callback, uintptr_t* user_data);

/* Converts a v3 SDK hook's saved r3..r10 images into the target-width IOS
 * request layout.  Translation only: it neither reads game memory nor
 * invokes rtfs. */
int rt_build_fs_ipc(uint32_t entry_index, const uintptr_t* args, struct rtfs_ipc* out);

/*
 * Called by the completion entry with the IPC result (in/out: what the
 * game's callback will receive) and the pending record it was registered
 * with. Applies the redirect to the buffer, issues SD requests as needed,
 * and hands back the game's callback and user data for the tail call, or
 * a zero callback while an SD request is still in flight.
 */
void rt_on_di_complete(struct rt_context* ctx, int32_t* result, struct rt_pending* record,
                       uintptr_t* callback, uintptr_t* user_data);

/* Exposed for the host tests. */
int rt_is_di_read(uint32_t ioctl, const uint32_t* in, uint32_t in_len);
uint32_t rt_checksum(const uint8_t* bytes, uint32_t length); /* h = h * 31 + byte */

#ifndef RT_TARGET_PPC
/* Host stand-ins for the game's IOS_IoctlvAsync and the unhooked
 * IOS_IoctlAsync (the console calls through rt_context.ioctlv_async and
 * di_read_entry). Tests set them; they return the IPC result. */
extern int32_t (*rt_host_ioctlv_async)(uint32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count,
                                       struct rt_ioctlv* vec, uint32_t callback, struct rt_pending* record);
extern int32_t (*rt_host_ioctl_async)(uint32_t fd, uint32_t ioctl, uint32_t* in, uint32_t in_len, uint32_t out,
                                      uint32_t out_len, uint32_t callback, struct rt_pending* record);
/* Host stand-ins for the console's IOS requests behind the savegame path
 * (the console calls through rt_fs_state.ioctlv_sync / open_sync and the
 * context's ioctlv_async / di_read_entry). A transfer moves io_count
 * 512-byte blocks at lba to/from the 32-bit buffer address (tests point
 * it below 4 GiB). Sync: performed now, 0 on success. Async issue / defer:
 * the test's fake IOS records the request and later calls
 * rt_on_fs_complete with the tag; they return 0 when accepted, negative
 * when refused. Wait: the test's chance to run its fake IOS while a sync
 * arrival waits for the engine. Game callback: observes an inline
 * delivery (the last resort). Null stand-ins refuse. */
extern int32_t (*rt_host_fs_transfer)(uint32_t lba, uint32_t count, uint32_t buffer, uint32_t is_write);
extern int32_t (*rt_host_fs_issue)(uint32_t lba, uint32_t count, uint32_t buffer, uint32_t is_write, void* tag);
extern int32_t (*rt_host_fs_defer)(void* tag);
extern void (*rt_host_fs_wait)(struct rt_context* ctx);
extern int32_t (*rt_host_fs_open_sync)(const char* path, uint32_t mode);
extern int32_t (*rt_host_fs_close_sync)(int32_t fd);
extern int32_t (*rt_host_fs_read_sync)(int32_t fd, uint32_t buffer, uint32_t length);
extern int32_t (*rt_host_fs_ioctl_sync)(int32_t fd, uint32_t request, uint32_t in, uint32_t in_len, uint32_t out,
                                         uint32_t out_len);
extern int rt_host_fs_in_thread; /* 1: hooks run as on a thread (interrupts on); 0: as from an IPC callback */
extern void (*rt_host_game_callback)(uint32_t cb, int32_t result, uint32_t user_data);
#endif

#ifdef __cplusplus
}
#endif

#endif
