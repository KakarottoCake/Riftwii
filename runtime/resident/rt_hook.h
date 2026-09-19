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
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_BLOB_MAGIC 0x5257484Bu    /* 'RWHK' */
#define RT_BLOB_VERSION 1u
#define RT_CONTEXT_MAGIC 0x52574358u /* 'RWCX' */

/* rt_context.flags */
#define RT_FLAG_GECKO 0x1u /* report every DI read over the USB Gecko in EXI channel gecko_channel */

struct rt_blob_header {
    uint32_t magic;                        /* RT_BLOB_MAGIC */
    uint32_t version;                      /* RT_BLOB_VERSION */
    uint32_t blob_size;                    /* bytes, header included */
    uint32_t context_offset;               /* struct rt_context */
    uint32_t hook_ioctl_async_offset;      /* trampoline: new first instruction of IOS_IoctlAsync */
    uint32_t replay_ioctl_async_offset;    /* 4 words: the displaced instructions, loader-filled */
    uint32_t continue_ioctl_async_offset;  /* 4 words: lis/ori/mtctr/bctr to the original + 16, loader-filled */
    uint32_t reserved;
};

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
    uint32_t reserved[4];
};

/*
 * Called by the trampoline with the eight IOS_IoctlAsync arguments
 * (fd, ioctl, in, in_len, out, out_len, callback, user data) saved on the
 * stack as register images (uintptr_t is the register width: 32 bits on
 * the console, wider on the host where this is unit-tested). Returns 0 to
 * let the original function run, non-zero to return `*result` to the
 * caller instead (unused by E2, needed by E3+).
 */
int rt_on_ioctl_async(struct rt_context* ctx, const uintptr_t* args, uint32_t* result);

/* Exposed for the host tests: how a DI read is recognised. */
int rt_is_di_read(uint32_t ioctl, const uint32_t* in, uint32_t in_len);

#ifdef __cplusplus
}
#endif

#endif
