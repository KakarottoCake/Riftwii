/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_RT_HOOK_H
#define RIFTWII_RT_HOOK_H

/*
 * Layout shared by the resident runtime (freestanding, position-independent
 * PowerPC code copied into reserved MEM2) and the loader that installs it.
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
 */

#include <stdint.h>

#include "rtable.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RT_BLOB_MAGIC 0x5257484Bu    /* 'RWHK' */
#define RT_BLOB_VERSION 2u
#define RT_CONTEXT_MAGIC 0x52574358u /* 'RWCX' */

/* rt_context.flags */
#define RT_FLAG_GECKO 0x1u /* report DI reads over the USB Gecko in EXI channel gecko_channel */

#define RT_MAX_PENDING 4u /* outstanding redirected reads (the DVD driver issues one at a time) */
#define RT_MAX_RUNS 8u    /* pieces one read may split into; more passes through unmodified */
#define RT_GECKO_MAX_FAILURES 32u /* refused bytes after which Gecko reporting turns itself off */
#define RT_BOUNCE_BYTES 0x8000u   /* bytes one SD request fetches (64 sectors), per pending record */

/* rt_pending.phase */
#define RT_PHASE_DISC 0u /* waiting for the disc reply */
#define RT_PHASE_SD 1u   /* an SD request is in flight */

/* DI results as the DVD driver sees them (wiibrew /dev/di). */
#define RT_DI_SUCCESS 1
#define RT_DI_ERROR 2 /* handed to the game when an SD read fails: the driver's error path, not silence */

struct rt_blob_header {
    uint32_t magic;                        /* RT_BLOB_MAGIC */
    uint32_t version;                      /* RT_BLOB_VERSION */
    uint32_t blob_size;                    /* bytes, header included */
    uint32_t context_offset;               /* struct rt_context */
    uint32_t hook_ioctl_async_offset;      /* trampoline: new first instruction of IOS_IoctlAsync */
    uint32_t replay_ioctl_async_offset;    /* 4 words: the displaced instructions, loader-filled */
    uint32_t continue_ioctl_async_offset;  /* 4 words: lis/ori/mtctr/bctr to the original + 16, loader-filled */
    uint32_t complete_di_offset;           /* completion entry the hook installs as the IPC callback */
};

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
 * thread's stack, so no scratch array lives on the stack). 448 bytes.
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
    rt_run runs[RT_MAX_RUNS];        /* offset 0xC0 */
};

/* 1920 bytes. */
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
    uint32_t reserved[6];
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
/* Host stand-in for the game's IOS_IoctlvAsync (the console calls through
 * rt_context.ioctlv_async). Tests set it; it returns the IPC result. */
extern int32_t (*rt_host_ioctlv_async)(uint32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count,
                                       struct rt_ioctlv* vec, uint32_t callback, struct rt_pending* record);
#endif

#ifdef __cplusplus
}
#endif

#endif
