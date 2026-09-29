/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The game crash blob: __OSUnhandledException hooked, so when the game
 * crashes (an exception it has no handler for, or one its own crash
 * screen handles) the registers, the stack's return addresses and the
 * code around the fault are written to the NAND as
 * /shared2/riftwii/crash.bin (runtime/rtfault.h) before the game goes
 * on as it would have. The menu makes sd:/riftwii/gamecrash.txt of it at
 * its next start and offers to send a report.
 *
 * A crashed game runs its exception handler with interrupts off, so the
 * game's own IOS calls, which wait for an interrupt, cannot be used: the
 * blob talks to IOS through the IPC registers itself and waits for each
 * answer by reading them (the protocol as wiibrew's "Hardware/IPC" and
 * libogc's ipc.c describe it: X1 sends, Y2 is IOS taking the request, Y1
 * its reply, X2 hands the reply back). An answer to a request the game
 * sent before it crashed is acknowledged and passed over. It runs on a
 * stack of its own, since the game's may be what broke, and only once.
 *
 * Layout (fault_entry.S): the header below, the trampoline, the replay
 * slot, the C code, then the context. Position independent like the
 * other blobs (linked at two bases and compared); the state (the IPC
 * request, the record and the stack) is in MEM2 at ctx->state.
 *
 * Hooking: __OSUnhandledException's first instruction is replaced with
 * a `b` to the trampoline; the replay slot runs the displaced
 * instruction and jumps to the second one.
 *
 * The retail BCA. A game read from an image (USB, SD, RVZ, a burned disc)
 * has no drive to answer its BCA read, and d2x answers from the image's
 * bytes, usually zero; a game that checks (New Super Mario Bros. Wii)
 * stops minutes into play. With RT_FAULT_FLAG_BCA the blob also hooks
 * IOS_IoctlAsync: a DVDLowReadDiskBca gets a retail BCA written into the
 * game's buffer and its output pointed at the blob's sink, so IOS's own
 * answer lands there and the game's callback runs from IOS's reply as
 * for any other command. The same answer as the resident runtime's
 * (rt_answer_bca), for launches that need nothing else of it: the
 * runtime takes some 100 KB of the game's memory, and Super Smash Bros.
 * Brawl, a few KB short in MEM1, froze loading every stage. */
#ifndef RIFTWII_FAULT_HOOK_H
#define RIFTWII_FAULT_HOOK_H

#define RT_FAULT_MAGIC 0x52574642u          /* "RWFB" */
#define RT_FAULT_VERSION 2u                 /* also in fault_entry.S's header */
#define RT_FAULT_CONTEXT_MAGIC 0x52574658u  /* "RWFX" */
#define RT_FAULT_CONTEXT_BYTES 128u
#define RT_FAULT_STACK_BYTES 4096u
#define RT_FAULT_FLAG_BCA 1u                /* answer DVDLowReadDiskBca as a retail disc */
#define RT_FAULT_DI_READ_BCA 0xDAu
#define RT_FAULT_BCA_BYTES 0x40u
#define RT_FAULT_BCA_MARK 0x33u             /* the byte that is 1 */
#define RT_FAULT_SINK_BYTES 96u             /* the BCA's 64 bytes, 32-aligned somewhere inside */

/* rt_fault_context byte offsets fault_entry.S uses. */
#define RT_FAULT_CTX_STACK_TOP 8
#define RT_FAULT_CTX_SAVE 64  /* LR, r1, r3, r4, r5, r6 */

#ifndef __ASSEMBLER__
#include <stdint.h>

#include "rtfault.h"
#include "rtshot.h"

/* Big-endian words at the start of the blob. */
struct rt_fault_header {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t context_offset;
    uint32_t hook;          /* the trampoline */
    uint32_t replay;        /* loader-filled: the displaced instruction */
    uint32_t resume;        /* loader-filled: the jump to the second one */
    uint32_t hook_ioctl;    /* IOS_IoctlAsync's trampoline (the BCA) */
    uint32_t replay_ioctl;
    uint32_t resume_ioctl;
    uint32_t sink;          /* RT_FAULT_SINK_BYTES for IOS's BCA answer */
};

/* Inside the blob, 32-aligned; the loader fills the fields up to
 * `version`, the blob the rest. */
struct rt_fault_context {
    uint32_t magic;
    uint32_t state;             /* struct rt_fault_state, MEM2 */
    uint32_t stack_top;         /* the blob's stack: 16 below its top (the trampoline ends the back chain) */
    uint32_t ticks_per_second;
    char version[RTFAULT_VERSION_BYTES];
    uint32_t done;              /* a crash was seen: later ones are left alone */
    uint32_t step;              /* how far the NAND write got (FAULT_STEP_*) */
    int32_t result;             /* the last IOS answer */
    uint32_t flags;             /* RT_FAULT_FLAG_* */
    uint32_t bca_sink;          /* loader-filled: the sink, 32-aligned */
    uint32_t bca_answers;       /* BCA reads answered */
    uint32_t save[6];           /* fault_entry.S: LR, r1, r3-r6 (at RT_FAULT_CTX_SAVE) */
    uint32_t spare2[10];
};

struct rt_fault_state {
    uint32_t request[16];       /* one IPC request, 32-aligned */
    struct rtfault_record record;
    char fs[32];                /* "/dev/fs" */
    char path[64];
    uint8_t attr[96];           /* RTSHOT_FS_ATTR_BYTES, rounded up */
    uint8_t stack[RT_FAULT_STACK_BYTES];
};

#endif
#endif
