/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The in-game screenshot blob: the combo (runtime/rtshot.h) watched in
 * the game's Bluetooth reads (IOS_IoctlvAsync hooked) and in PADRead,
 * the picture the video interface shows copied into MEM2 when it fires,
 * or, in direct mode, written straight from the game's frame buffer
 * (no copy, so no 0.8 MB of the game's MEM2; a moving picture may shear
 * a little, as the game redraws it while the write runs),
 * and the copy written to the NAND through the game's own asynchronous
 * IOS calls, one request after the other from their completions, so the
 * game never waits for it: in /shared2/riftwii/part, moved to
 * /shared2/riftwii once it is whole and closed (a file cut short by the
 * game ending stays in part/, which the menu empties; a tester's console
 * gave back half its shots unreadable). The name it gets is checked free
 * first: a move replaces a file of that name. The menu turns the files
 * into PNGs at its next start (wii/screenshot.hpp).
 *
 * The Remote that pressed the combo rumbles once, briefly, when the
 * picture is taken, twice when its file is saved, and once for longer
 * when saving failed (rtshot.h has how).
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
#define RT_SHOT_RENAME 9u        /* part/shotNNNN.raw to shotNNNN.raw, once closed */
#define RT_SHOT_DELETE 10u       /* the part file of a job that failed */
#define RT_SHOT_MKDIR_PART 11u   /* /shared2/riftwii/part */
#define RT_SHOT_PROBE 12u        /* GetAttr of the final name: free? */

/* rt_shot_watch.kind */
#define RT_SHOT_WATCH_ACL 0u     /* a Wii Remote's input */
#define RT_SHOT_WATCH_EVENT 1u   /* an HCI event */

#define RT_SHOT_BUZZ_STEPS 4u

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
    uint32_t ioctlv_async;     /* the game's IOS_IoctlvAsync (through the hooks), for the rumble */
};

struct rt_shot_watch {
    uint32_t in_use;
    uint32_t callback;         /* the game's */
    uint32_t user;
    uint32_t data;             /* the transfer's buffer */
    uint32_t length;
    uint32_t kind;             /* RT_SHOT_WATCH_* */
};

/* One rumble report's USBV0 bulk message: IOS reads each part, so each
 * starts a 32-byte line. Two of them, on and off, so the one in flight
 * is never written over (its answer is not waited for). */
struct rt_shot_rumble {
    uint8_t packet[32] __attribute__((aligned(32)));
    uint32_t vectors[6] __attribute__((aligned(32)));  /* endpoint, length, packet: address and size each */
    uint8_t endpoint __attribute__((aligned(32)));
    uint16_t length __attribute__((aligned(32)));
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
    uint32_t created;          /* the part file exists */
    uint32_t renamed;          /* it became the .raw */
    uint32_t deleted;          /* it was removed after a failure */
    int32_t bt_fd;             /* the game's /dev/usb/oh1/57e/305, -1: not seen yet */
    uint32_t buzz_slot;        /* the remote slot that rumbles */
    uint32_t buzz_steps;       /* steps in the pattern, 0: none running */
    uint32_t buzz_next;        /* the step sent next (even: on, odd: off) */
    uint32_t buzz_due;         /* time base when it is due */
    uint32_t buzz_ms[RT_SHOT_BUZZ_STEPS];  /* how long each step lasts */
    char path[64] __attribute__((aligned(32)));
    char names[128] __attribute__((aligned(32)));  /* Rename: the part file, then the final name */
    struct rt_shot_rumble rumble[2];
    char device[32] __attribute__((aligned(32)));
    uint8_t attr[96] __attribute__((aligned(32)));
    uint8_t header[RTSHOT_HEADER_BYTES] __attribute__((aligned(32)));  /* direct mode */
};

#ifdef RT_TARGET_PPC
typedef char rt_shot_context_layout[(sizeof(struct rt_shot_context) <= RT_SHOT_CONTEXT_BYTES) ? 1 : -1];
#endif

#endif
