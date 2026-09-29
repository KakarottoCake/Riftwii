/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The in-game screenshot blob: the combo (runtime/rtshot.h) watched in
 * the game's Bluetooth reads (IOS_IoctlvAsync hooked) and in PADRead,
 * the picture the video interface shows copied into MEM2 when it fires,
 * or, in direct mode, written straight from the game's frame buffer
 * (no copy, so no 0.8 MB of the game's MEM2; a moving picture may shear
 * a little, as the game redraws it while the write runs),
 * and the copy written to the NAND (/shared2/riftwii/shotNNNN.raw)
 * through the game's own asynchronous IOS calls, one request after the
 * other from their completions, so the game never waits for it. The
 * menu turns the files into PNGs at its next start (wii/screenshot.hpp).
 *
 * Layout (shot_entry.S): the header below, the trampolines with their
 * loader-filled replay slots, the IPC completion entries, the C code,
 * then the context. Position independent like the other blobs (linked
 * at two bases and compared). The state and the picture are in MEM2,
 * at ctx->state and ctx->frame.
 *
 * Hooking: each hooked function's first instruction is replaced with a
 * `b` to its trampoline. The replay slot runs the displaced instruction
 * and jumps to the function's second one; when the first instruction is
 * already another blob's `b` (the resident runtime's IOS_IoctlvAsync
 * hook, the adapter's PADRead hook), the replay is that same branch, so
 * both hooks run, this one first. */
#ifndef RIFTWII_SHOT_HOOK_H
#define RIFTWII_SHOT_HOOK_H

#include <stdint.h>

#include "rtshot.h"

#define RT_SHOT_MAGIC 0x52575353u          /* "RWSS" */
#define RT_SHOT_VERSION 1u                 /* also in shot_entry.S's header */
#define RT_SHOT_CONTEXT_MAGIC 0x52575358u  /* "RWSX" */
#define RT_SHOT_CONTEXT_BYTES 128u

/* rt_shot_context.flags */
#define RT_SHOT_FLAG_DEMO 1u    /* also 20 and 40 seconds after the first Wii Remote report (Dolphin tests) */
#define RT_SHOT_FLAG_DIRECT 2u  /* no frame copy: the NAND write reads the frame buffer on screen */

#define RT_SHOT_MAX 32u       /* screenshots a game session keeps at most (about 26 MB of NAND) */
#define RT_SHOT_WATCHES 8u    /* Bluetooth reads watched at once */

/* The NAND job: the request in flight. */
#define RT_SHOT_IDLE 0u
#define RT_SHOT_OPEN_FS 1u
#define RT_SHOT_MKDIR 2u
#define RT_SHOT_CREATE 3u
#define RT_SHOT_OPEN_FILE 4u
#define RT_SHOT_WRITE 5u
#define RT_SHOT_CLOSE_FILE 6u
#define RT_SHOT_CLOSE_FS 7u
#define RT_SHOT_WRITE_PIXELS 8u  /* direct mode: the picture, after the header */

/* Big-endian words at the start of the blob. */
struct rt_shot_header {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t context_offset;
    uint32_t hook_ioctlv;      /* IOS_IoctlvAsync's trampoline */
    uint32_t replay_ioctlv;    /* 16 bytes: the displaced word, then padding */
    uint32_t continue_ioctlv;  /* 16 bytes: the jump to the function's second word */
    uint32_t hook_pad;         /* PADRead's */
    uint32_t replay_pad;
    uint32_t continue_pad;
    uint32_t complete_bt;      /* the IPC callback of a watched Bluetooth read */
    uint32_t complete_nand;    /* the IPC callback of the NAND job */
};

struct rt_shot_context {
    uint32_t magic;
    uint32_t state;            /* struct rt_shot_state, MEM2 */
    uint32_t frame;            /* RTSHOT_FRAME_BYTES, MEM2, 32-byte aligned; 0 in direct mode */
    uint32_t open_async;       /* the game's IOS_OpenAsync (not through another blob) */
    uint32_t close_async;
    uint32_t write_async;
    uint32_t ioctl_async;
    uint32_t complete_bt;      /* this blob's completion entries */
    uint32_t complete_nand;
    uint32_t flags;            /* RT_SHOT_FLAG_* */
    uint32_t ticks_per_second; /* time base */
    uint32_t shots;            /* written */
    uint32_t failures;         /* pictures that were not */
    uint32_t number;           /* the file number tried next */
    int32_t last_error;
    uint32_t demo_start;       /* time base at the first Wii Remote report, 0: none yet */
    uint32_t inited;           /* the state is set up (at the first hooked call) */
};

struct rt_shot_watch {
    uint32_t in_use;
    uint32_t callback;         /* the game's */
    uint32_t user;
    uint32_t data;             /* the transfer's buffer */
    uint32_t length;
};

/* In MEM2. The buffers IOS reads each start a 32-byte line. */
struct rt_shot_state {
    struct rtshot_input input;
    struct rt_shot_watch watch[RT_SHOT_WATCHES];
    uint32_t phase;            /* RT_SHOT_* */
    int32_t fs_fd;
    int32_t file_fd;
    uint32_t bytes;            /* the file's size */
    uint32_t tries;            /* CreateFile names tried */
    int32_t error;             /* the job's first failure */
    uint32_t pixels;           /* direct mode: the frame buffer on screen (cached address) */
    uint32_t pixel_bytes;
    char path[64] __attribute__((aligned(32)));
    char device[32] __attribute__((aligned(32)));
    uint8_t attr[96] __attribute__((aligned(32)));
    uint8_t header[RTSHOT_HEADER_BYTES] __attribute__((aligned(32)));  /* direct mode */
};

#ifdef RT_TARGET_PPC
typedef char rt_shot_context_layout[(sizeof(struct rt_shot_context) <= RT_SHOT_CONTEXT_BYTES) ? 1 : -1];
#endif

#endif
