/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Wii Remote log blob (runtime/rtwlog.h): the game's empty debug
 * print functions branch to wlog_hook_print, which keeps the message;
 * one instruction of __OSLaunchMenu (the HOME Menu's "Wii Menu") branches
 * to wlog_hook_exit, which saves the log to NAND through the game's own
 * IOS functions and goes on.
 *
 * Since version 2 it also hooks the game's IOS_IoctlvAsync, as the
 * screenshot blob does (and chained with it and the resident runtime the
 * same way): in the game's traffic with the Bluetooth dongle it runs the
 * Wii U Pro Controller translator (runtime/rtpro.h) and keeps the
 * Bluetooth trace (rtwlog.h). The dongle's file is the one the game sends
 * HCI commands to (a class request on the control endpoint); its bulk and
 * interrupt transfers are the ones looked at.
 *
 * Layout (wlog_entry.S): the header below, the trampolines, the replay
 * slots, the IPC completion entry, the C code, then the context. Position
 * independent (the build links it at two bases and compares). The log and
 * the translator's state are in MEM2, at ctx->state. */
#ifndef RIFTWII_WLOG_HOOK_H
#define RIFTWII_WLOG_HOOK_H

#define RT_WLOG_MAGIC 0x52575757u          /* "RWWW" */
#define RT_WLOG_VERSION 3u                 /* also in wlog_entry.S's header */
#define RT_WLOG_CONTEXT_MAGIC 0x52575758u  /* "RWWX" */
#define RT_WLOG_CONTEXT_BYTES 128u
#define RT_WLOG_WATCHES 8u                 /* Bluetooth reads watched at once */

/* rt_wlog_context.flags */
#define RT_WLOG_FLAG_TRANSLATE 1u          /* the Pro Controller translator on (else the trace only) */

#ifndef __ASSEMBLER__
#include <stdint.h>

#include "rtpro.h"
#include "rtwlog.h"

/* Big-endian words at the start of the blob. */
struct rt_wlog_header {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t context_offset;
    uint32_t hook_print;           /* where each print stub branches */
    uint32_t hook_exit;            /* where __OSLaunchMenu's instruction branches */
    uint32_t replay_exit;          /* 8 bytes: the displaced instruction, then the branch back */
    uint32_t hook_ioctlv;          /* IOS_IoctlvAsync's trampoline */
    uint32_t replay_ioctlv;        /* 16 bytes: the displaced word (or another blob's branch), then nops */
    uint32_t continue_ioctlv;      /* 16 bytes: the jump to the function's second word */
    uint32_t complete_bt;          /* the IPC callback of a watched Bluetooth read */
};

struct rt_wlog_context {
    uint32_t magic;
    uint32_t state;                /* struct rt_wlog_state, MEM2 */
    uint32_t ios_open;             /* the game's synchronous IOS functions */
    uint32_t ios_close;
    uint32_t ios_write;
    uint32_t ios_ioctl;
    uint32_t saved;                /* the exit hook ran (and what it got: the write's result) */
    int32_t result;
    /* For the log's header, which the blob writes at its first use: the
     * MEM2 it lives in is the loader's own until the jump. */
    uint32_t ticks_per_second;
    uint32_t stubs;
    uint32_t probe;                /* RTWLOG_PROBE_*, for the header */
    uint32_t probe_at;
    /* The Bluetooth side (version 2). */
    uint32_t complete_bt;          /* this blob's completion entry */
    uint32_t flags;                /* RT_WLOG_FLAG_* */
    uint32_t hooked;               /* IOS_IoctlvAsync was hooked */
    uint32_t inited;               /* the state past the log is set up (at its first use) */
    uint32_t tables;               /* the game's lookup tables for the extension's encryption, 0: none */
    uint32_t pro_count;            /* Pro Controller pairings below */
    uint8_t pro_addresses[RTPRO_ADDRESSES * 6u];
};

/* A Bluetooth read the blob's completion stands in for. */
struct rt_wlog_watch {
    uint32_t in_use;
    uint32_t callback;             /* the game's */
    uint32_t user;
    uint32_t data;                 /* the transfer's buffer */
    uint32_t length;
    uint32_t endpoint;             /* 0x81 HCI events, 0x82 ACL */
};

/* Only event bookkeeping is needed when packets are never translated.
 * Keep all seven connections: pairing count does not bound hot-plugging. */
struct rt_wlog_trace_link {
    uint16_t handle;
    uint8_t used;
    uint8_t pad;
};

/* Each image has its own layout. The file stays first and 32-aligned for
 * IOS; translator-only launches reserve neither it nor save scratch. */
struct rt_wlog_full_state {
    struct rtwlog_file file;
    char fs[32];                   /* "/dev/fs" */
    char path[64];
    uint8_t attr[96];              /* RTSHOT_FS_ATTR_BYTES, rounded up */
    struct rtpro_state pro;
    struct rt_wlog_watch watch[RT_WLOG_WATCHES];
    int32_t bt_fd;                 /* the dongle's, once the game sent it a command; -1 before */
    uint32_t data_in, data_out;    /* data reports traced each way */
};

struct rt_wlog_trace_state {
    struct rtwlog_file file;
    char fs[32];
    char path[64];
    uint8_t attr[96];
    struct rt_wlog_trace_link links[RTPRO_LINKS];
    struct rt_wlog_watch watch[RT_WLOG_WATCHES];
    int32_t bt_fd;
    uint32_t data_in, data_out;
};

struct rt_wlog_translate_state {
    struct rtpro_state pro;
    struct rt_wlog_watch watch[RT_WLOG_WATCHES];
    int32_t bt_fd;
};

/* The planner and the host tests use the same launch-time size choice.
 * Watches cannot shrink with connected-pad count: the game owns the
 * outstanding IOS requests, including reads posted before a connection. */
static inline uint32_t rt_wlog_state_bytes(int record, int translate) {
    uint32_t bytes;
    if (!record && !translate) return 0;
    if (!record) bytes = sizeof(struct rt_wlog_translate_state);
    else if (!translate) bytes = sizeof(struct rt_wlog_trace_state);
    else bytes = sizeof(struct rt_wlog_full_state);
    return (bytes + 31u) & ~31u;
}

#if defined(RT_WLOG_TRACE_ONLY)
#define rt_wlog_state rt_wlog_trace_state
#elif defined(RT_WLOG_TRANSLATE_ONLY)
#define rt_wlog_state rt_wlog_translate_state
#else
#define rt_wlog_state rt_wlog_full_state
#endif

/* Entry points shared by the three images and exercised on the host. */
void wlog_on_print(struct rt_wlog_context* c, const uint32_t* words);
void wlog_on_exit(struct rt_wlog_context* c);
uint32_t wlog_on_ioctlv(struct rt_wlog_context* c, uint32_t* args);
int32_t wlog_after_ioctlv(struct rt_wlog_context* c, uint32_t slot, int32_t result);
int32_t wlog_on_bt(struct rt_wlog_context* c, int32_t result, uint32_t tag);

#ifdef RT_TARGET_PPC
typedef char rt_wlog_context_layout[(sizeof(struct rt_wlog_context) <= RT_WLOG_CONTEXT_BYTES) ? 1 : -1];
#endif
#endif

#endif
