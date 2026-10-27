/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-FileCopyrightText: 2017 Alex Chadwick (wup-028-bslug) <https://github.com/Chadderz121/wup-028-bslug> */
/* SPDX-FileCopyrightText: FIX94 and the Nintendont contributors, used with permission <https://github.com/FIX94/Nintendont> */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube adapter's in-game blob: PADRead and PADControlMotor
 * hooked, the WUP-028 driver (runtime/rtgcad.c) behind them. With the
 * way back (#224), L, R, Z and Start held on a GameCube controller leave
 * the game through its own OSReturnToMenu: three more hooks, the SDK's IPC
 * submit routine (save activity), OSSleepThread (where it leaves) and
 * OSSuspendThread (where a thread that suspends itself to wait leaves). Its own
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

#include "rtgcad.h"
#include "rtleave.h"

#define RT_PAD_MAGIC 0x52575044u          /* "RWPD" */
#define RT_PAD_VERSION 4u               /* also in pad_entry.S's header */
#define RT_PAD_CONTEXT_MAGIC 0x52575043u  /* "RWPC" */
#define RT_PAD_CONTEXT_BYTES 192u

/* rt_pad_context.leave_asked: the combo asked for the way back; an
 * OSSleepThread has gone into OSReturnToMenu (rtleave_sleep_leaves: the
 * main thread's, or the pad-reading thread's after the fallback wait). */
#define RT_PAD_LEAVE_ASKED 1u
#define RT_PAD_LEAVE_GONE 2u

/* How long the way back waits for the main thread to sleep before it
 * leaves from the sleep of the thread that read the pads instead. Normal
 * games sleep their main thread every frame, so they never get this far. */
#define RT_PAD_LEAVE_FALLBACK_MS 500u

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
    uint32_t hook_submit;      /* the SDK's IPC submit routine's (save activity) */
    uint32_t replay_submit;
    uint32_t continue_submit;
    uint32_t hook_sleep;       /* OSSleepThread's (the way back leaves from a sleep) */
    uint32_t replay_sleep;
    uint32_t continue_sleep;
    uint32_t hook_suspend;     /* OSSuspendThread's (or from a thread suspending itself) */
    uint32_t replay_suspend;
    uint32_t continue_suspend;
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
    uint32_t idle_reset;       /* the game's screen-dimming reset for its controller ports, 0: not found */
    gcad_pad last[GCAD_PORTS]; /* each port as it was when it last counted as in use (gcad_pad_moved) */
    /* The way back (0 return_to_menu: none). */
    uint32_t return_to_menu;   /* the game's OSReturnToMenu */
    struct rtleave leave;      /* the hold and the saves (runtime/rtleave.h; hold and quiet loader-filled) */
    uint32_t leave_asked;      /* RT_PAD_LEAVE_*: 0, then asked, then gone (once only) */
    /* Saves on the SD card go through the resident runtime's own card
     * driver, which the submit hook doesn't read: its rt_fs_state's
     * last_write (then wrote), 0 without the redirect (loader-filled). */
    uint32_t resident_last_write;
    /* And its savegame engine's busy word and queue count: a request in
     * hand or queued (rtleave_resident). Loader-filled with the above. */
    uint32_t resident_busy;
    uint32_t resident_queued;
    /* The ask: the thread running the PADRead that made it (0x800000E4
     * then) and when (low bit set), both 0 while none is open. */
    uint32_t leave_thread;
    uint32_t leave_asked_at;
};

#ifdef RT_TARGET_PPC
typedef char rt_pad_context_layout[(sizeof(struct rt_pad_context) <= RT_PAD_CONTEXT_BYTES) ? 1 : -1];
#endif

#endif
