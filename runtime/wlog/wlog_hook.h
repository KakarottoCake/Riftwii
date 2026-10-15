/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Wii Remote log blob (runtime/rtwlog.h): the game's empty debug
 * print functions branch to wlog_hook_print, which keeps the message;
 * one instruction of __OSLaunchMenu (the HOME Menu's "Wii Menu") branches
 * to wlog_hook_exit, which saves the log to NAND through the game's own
 * IOS functions and goes on.
 *
 * Layout (wlog_entry.S): the header below, the two trampolines, the exit
 * replay slot, the C code, then the context. Position independent (the
 * build links it at two bases and compares). The log itself is in MEM2,
 * at ctx->state. */
#ifndef RIFTWII_WLOG_HOOK_H
#define RIFTWII_WLOG_HOOK_H

#define RT_WLOG_MAGIC 0x52575757u          /* "RWWW" */
#define RT_WLOG_VERSION 1u                 /* also in wlog_entry.S's header */
#define RT_WLOG_CONTEXT_MAGIC 0x52575758u  /* "RWWX" */
#define RT_WLOG_CONTEXT_BYTES 64u

#ifndef __ASSEMBLER__
#include <stdint.h>

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
};

/* The blob's memory in MEM2: the file first (32-aligned for IOS). */
struct rt_wlog_state {
    struct rtwlog_file file;
    char fs[32];                   /* "/dev/fs" */
    char path[64];
    uint8_t attr[96];              /* RTSHOT_FS_ATTR_BYTES, rounded up */
};

#ifdef RT_TARGET_PPC
typedef char rt_wlog_context_layout[(sizeof(struct rt_wlog_context) <= RT_WLOG_CONTEXT_BYTES) ? 1 : -1];
#endif
#endif

#endif
