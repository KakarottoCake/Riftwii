/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_RT_HOOK_H
#define RIFTWII_RT_HOOK_H

/*
 * Layout shared by the resident runtime (freestanding, position-independent
 * PowerPC code copied into reserved MEM2) and the loader that installs it.
 * Every field is a 32-bit unsigned so the layout is identical on the host,
 * where the C part is unit-tested, and on the console.
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
 */

#include <stdint.h>

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

/* One redirected read in flight. 32 bytes. */
struct rt_pending {
    uint32_t in_use;
    uint32_t callback;     /* the game's IPC callback (may be 0) */
    uint32_t user_data;    /* the game's user data */
    uint32_t out;          /* the game's destination buffer */
    uint32_t length;       /* bytes requested */
    uint32_t word_offset;  /* disc offset in 4-byte words */
    uint32_t reserved[2];
};

/* 224 bytes. */
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
    uint32_t gecko_failures;      /* Gecko bytes the adapter did not accept */
    /* Redirection. */
    uint32_t table;               /* rt_header* of the redirect table, 0 = none (loader-filled) */
    uint32_t complete_entry;      /* absolute address of the completion entry (loader-filled) */
    uint32_t redirected_reads;    /* reads with at least one MEM/ZERO run */
    uint32_t pending_overflow;    /* redirected reads passed through because no record was free */
    uint32_t run_overflow;        /* reads passed through because they split into > RT_MAX_RUNS */
    uint32_t last_checksum;       /* of the redirected bytes of the last completed read */
    uint32_t completions;         /* completion entry invocations */
    uint32_t reserved[5];
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
 * Called by the completion entry with the IPC result and the pending
 * record it was registered with. Applies the redirect to the buffer and
 * hands back the game's callback and user data for the tail call.
 */
void rt_on_di_complete(struct rt_context* ctx, int32_t result, struct rt_pending* record,
                       uintptr_t* callback, uintptr_t* user_data);

/* Exposed for the host tests. */
int rt_is_di_read(uint32_t ioctl, const uint32_t* in, uint32_t in_len);
uint32_t rt_checksum(const uint8_t* bytes, uint32_t length); /* h = h * 31 + byte */

#ifdef __cplusplus
}
#endif

#endif
