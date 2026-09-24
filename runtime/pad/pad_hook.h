/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube adapter's in-game blob: PADRead and PADControlMotor
 * hooked, the WUP-028 driver (runtime/rtgcad.c) behind them. Its own
 * small blob beside the resident runtime, so a game without the adapter
 * option never carries it and the resident's ABI is untouched.
 *
 * Layout (pad_entry.S): the header below, the two trampolines with
 * their loader-filled replay slots, the IPC completion entry, the C
 * code, then the context. Position independent like the resident (the
 * build links it at two bases and compares). The driver's state (with
 * every buffer IOS touches) is in MEM2, at ctx->state. */
#ifndef RIFTWII_PAD_HOOK_H
#define RIFTWII_PAD_HOOK_H

#include <stdint.h>

#define RT_PAD_MAGIC 0x52575044u          /* "RWPD" */
#define RT_PAD_VERSION 2u               /* also in pad_entry.S's header */
#define RT_PAD_CONTEXT_MAGIC 0x52575043u  /* "RWPC" */
#define RT_PAD_CONTEXT_BYTES 128u

/* rt_pad_context.flags */
#define RT_PAD_FLAG_DEMO 1u  /* no adapter needed: the first empty port presses A about once a second (Dolphin tests) */

/* Big-endian words at the start of the blob. */
struct rt_pad_header {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t context_offset;
    uint32_t hook_read;        /* PADRead's trampoline */
    uint32_t replay_read;      /* 16 bytes: the displaced word, then padding */
    uint32_t continue_read;    /* 16 bytes: the jump back (replay + 16) */
    uint32_t hook_motor;       /* PADControlMotor's */
    uint32_t replay_motor;
    uint32_t continue_motor;
    uint32_t complete;         /* the IPC callback */
};

struct rt_pad_context {
    uint32_t magic;
    uint32_t state;            /* struct gcad, in MEM2 */
    uint32_t ioctl_async;      /* the game's IOS_IoctlAsync (unhooked) */
    uint32_t ioctlv_async;     /* the game's IOS_IoctlvAsync (unhooked), 0 on v4 */
    uint32_t complete_entry;   /* this blob's completion entry */
    int32_t fd;                /* /dev/usb/hid, opened by the loader */
    uint32_t version;          /* 4 or 5 */
    uint32_t ticks_per_ms;     /* time base */
    uint32_t inited;           /* the driver state is set up (first PADRead) */
    uint32_t overlay_mask;     /* ports the last PADRead filled from the adapter */
    uint32_t reads;
    uint32_t replies;
    uint32_t flags;            /* RT_PAD_FLAG_* (loader-filled) */
    int32_t known_dev;         /* the adapter's device id from the menu's USB list, -1: none */
};

#ifdef RT_TARGET_PPC
typedef char rt_pad_context_layout[(sizeof(struct rt_pad_context) <= RT_PAD_CONTEXT_BYTES) ? 1 : -1];
#endif

#endif
