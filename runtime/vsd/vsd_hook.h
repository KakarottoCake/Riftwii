/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The virtual SD card blob: the game's SD slot answered from a card image
 * (runtime/rtvsd.h) on the SD card or the USB drive.
 *
 * It hooks the game's IOS_Open, IOS_Close, IOS_Ioctl and IOS_Ioctlv, each
 * in both forms. An open of "/dev/sdio/slot0" gets a handle of the blob's
 * own (VSD_FAKE_FD) and never reaches IOS; every call on that handle is
 * answered by the card (rtvsd.c), and its reads and writes of the image
 * become requests on the device holding the image, through the game's
 * own IOS functions (their replay slots): the SD slot the loader opened
 * and selected, d2x's /dev/sdio/sdhc, or d2x's /dev/usb2. Calls on any
 * other handle run the original function untouched.
 *
 * Synchronous calls are served on the caller's thread with the
 * synchronous originals. An asynchronous call returns 0 at once and its
 * callback runs from the IPC interrupt when the answer is ready, as with
 * IOS: a transfer chains its requests with the blob's completion entry
 * (vsd_complete) as their callback and hands the game's callback to the
 * entry's tail call at the end; an answer that needs no transfer rides a
 * null round trip (a status request on the device), so the game's
 * callback never runs inside its own call. The card event (command
 * 0x40) stays unanswered, as IOS keeps it until the card changes, until
 * the game takes it back.
 *
 * Memory: every buffer the game hands over is its own (read and written
 * through the cache, then flushed, since its driver may invalidate them
 * for the DMA it expects). Reads land straight in the game's buffer when
 * the device can take it (32-byte aligned; MEM2 for the USB drive),
 * otherwise in the state's bounce buffer and are copied.
 *
 * After a write to the SD slot the card goes on programming its flash;
 * the blob asks its status (CMD13) until it is done before the next
 * command, as the resident runtime does (rt_hook.c, rt_sd_settles).
 *
 * Layout (vsd_entry.S): the header below, the eight trampolines, the
 * completion entry, the replay slots, the C code, then the context. The
 * state (struct vsd_state) is in MEM2; the loader builds it. Position
 * independent: linked at two bases and compared. */
#ifndef RIFTWII_VSD_HOOK_H
#define RIFTWII_VSD_HOOK_H

#define VSD_MAGIC 0x52575653u          /* "RWVS" */
#define VSD_VERSION 1u                 /* also in vsd_entry.S's header */
#define VSD_CONTEXT_MAGIC 0x52575658u  /* "RWVX" */
#define VSD_CONTEXT_BYTES 64u
#define VSD_STATE_MAGIC 0x52575654u    /* "RWVT" */

/* The hooked functions, in the header's and the state's order. */
#define VSD_OPEN_ASYNC 0u
#define VSD_CLOSE_ASYNC 1u
#define VSD_IOCTL_ASYNC 2u
#define VSD_IOCTLV_ASYNC 3u
#define VSD_OPEN 4u
#define VSD_CLOSE 5u
#define VSD_IOCTL 6u
#define VSD_IOCTLV 7u
#define VSD_ENTRIES 8u

/* The device holding the image. */
#define VSD_BACKEND_SLOT0 0u   /* /dev/sdio/slot0, the card selected (vsd_state.sdhc: block addresses) */
#define VSD_BACKEND_D2X_SD 1u  /* d2x's /dev/sdio/sdhc */
#define VSD_BACKEND_USB 2u     /* d2x's /dev/usb2 */

#define VSD_FAKE_FD 63           /* the handle the game gets: above any IOS gives out */
#define VSD_BOUNCE_BYTES 0x8000u /* 64 sectors */
#define VSD_DELIVERS 8u          /* answers riding a null round trip at once */
#define VSD_TRIES 3u             /* a failed request is sent this often */
#define VSD_SETTLE_POLLS 8192u   /* CMD13s after a write, about half a second */

#ifndef __ASSEMBLER__
#include <stdint.h>

#include "rtvsd.h"

struct vsd_header {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t context_offset;
    uint32_t complete;               /* vsd_complete: the callback of the blob's own requests */
    uint32_t hook[VSD_ENTRIES];      /* the trampolines */
    uint32_t replay[VSD_ENTRIES];    /* loader-filled: the displaced instruction */
    uint32_t resume[VSD_ENTRIES];    /* loader-filled: the jump to the second one */
};

/* Inside the blob, 32-aligned: where the state is. */
struct vsd_context {
    uint32_t magic;
    uint32_t state;  /* struct vsd_state, loader-filled */
    uint32_t spare[14];
};

struct vsd_ioctlv {
    uint32_t data;
    uint32_t len;
};

/* A request of the blob's own in flight; the tag of its callback. */
#define VSD_TAG_TRANSFER 1u
#define VSD_TAG_DELIVER 2u

struct vsd_deliver {
    uint32_t kind;                   /* VSD_TAG_DELIVER */
    uint32_t in_use;
    uint32_t callback;               /* the game's */
    uint32_t user_data;
    int32_t result;                  /* handed to it */
    uint32_t pad[3];
    uint32_t status[8];              /* the null round trip's answer, its own line */
};

/* The one transfer (the game's driver sends one command at a time). */
#define VSD_PHASE_DATA 0u    /* a piece of the image is in flight */
#define VSD_PHASE_SETTLE 1u  /* a CMD13 after a write is in flight */

struct vsd_transfer {
    uint32_t kind;                   /* VSD_TAG_TRANSFER */
    uint32_t in_use;
    uint32_t callback;               /* the game's (async), 0 on a thread */
    uint32_t user_data;
    uint32_t write;
    uint32_t buffer;                 /* the game's data */
    uint32_t sector;                 /* the image's first sector */
    uint32_t count;
    uint32_t done;                   /* sectors moved */
    uint32_t chunk;                  /* sectors of the request in flight */
    uint32_t bounced;                /* the piece goes through the bounce buffer */
    uint32_t phase;
    uint32_t tries;
    uint32_t polls;
    uint32_t piece_device;           /* the piece in flight: where on the device, */
    uint32_t piece_data;             /* and its buffer (kept for a retry) */
    uint32_t reply_out;              /* the game's reply buffer (16 bytes), 0 = none */
    uint32_t reply[4];               /* what goes there */
};

struct vsd_state {
    uint32_t magic;                  /* VSD_STATE_MAGIC */
    /* Loader-filled. */
    uint32_t complete;               /* vsd_complete's address */
    uint32_t original[VSD_ENTRIES];  /* the functions without the hook (replay slots), 0 = not in the game */
    uint32_t backend;                /* VSD_BACKEND_* */
    int32_t fd;                      /* the device's handle */
    uint32_t sdhc;                   /* SLOT0: the real card takes block addresses */
    uint32_t rca;                    /* SLOT0: the real card's address (CMD13 after writes), 0 = none */
    /* Counters. */
    uint32_t opens;
    uint32_t reads;                  /* transfers */
    uint32_t writes;
    uint32_t sectors_read;
    uint32_t sectors_written;
    uint32_t failures;               /* requests on the device that failed (each try) */
    uint32_t gave_up;                /* transfers that failed after every try */
    uint32_t busy;                   /* commands refused: a transfer was in flight */
    uint32_t delivers;
    uint32_t events;                 /* card events held */
    uint32_t last_error;
    /* The card event the game registered, unanswered. */
    uint32_t event_held;
    uint32_t event_callback;
    uint32_t event_user_data;
    uint32_t pad0[2];
    struct vsd_transfer transfer __attribute__((aligned(32)));
    struct vsd_deliver deliver[VSD_DELIVERS] __attribute__((aligned(32)));
    /* The blob's own IPC blocks, each on its own lines. */
    struct rtvsd_request request __attribute__((aligned(32)));
    uint32_t pad1[7];
    uint32_t response[8] __attribute__((aligned(32)));
    struct vsd_ioctlv vec[4] __attribute__((aligned(32)));
    uint32_t words[8] __attribute__((aligned(32)));      /* d2x: the sector and the count */
    struct rtvsd_card card __attribute__((aligned(32)));
    uint8_t bounce[VSD_BOUNCE_BYTES] __attribute__((aligned(32)));
};

/* The blob's C entry points (vsd_entry.S calls them). */
int vsd_on_ipc(struct vsd_context* ctx, uint32_t entry, uint32_t* args, int32_t* result);
void vsd_on_complete(struct vsd_context* ctx, int32_t* result, void* tag, uint32_t* callback, uint32_t* user_data);

#ifndef RT_TARGET_PPC
/* Host stand-ins for the game's IOS functions (the tests' fake IOS). */
extern int32_t (*vsd_host_ioctl)(int32_t fd, uint32_t ioctl, uint32_t in, uint32_t in_len, uint32_t out,
                                 uint32_t out_len);
extern int32_t (*vsd_host_ioctlv)(int32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count, uint32_t vec);
extern int32_t (*vsd_host_ioctl_async)(int32_t fd, uint32_t ioctl, uint32_t in, uint32_t in_len, uint32_t out,
                                       uint32_t out_len, uint32_t callback, uint32_t user_data);
extern int32_t (*vsd_host_ioctlv_async)(int32_t fd, uint32_t ioctl, uint32_t in_count, uint32_t out_count,
                                        uint32_t vec, uint32_t callback, uint32_t user_data);
#endif

#endif
#endif
