/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Wii Remote log blob's header and trampolines (wlog_hook.h). */
#include "wlog_hook.h"

    .section .text.wlog_entry, "ax", @progbits
    .globl _wlog_blob_start
_wlog_blob_start:
    .long 0x52575757, 1, _wlog_blob_end - _wlog_blob_start, wlog_context_data - _wlog_blob_start
    .long wlog_hook_print - _wlog_blob_start, wlog_hook_exit - _wlog_blob_start
    .long wlog_replay_exit - _wlog_blob_start

/* A debug print, (format, arguments...): it takes the empty function's
 * place, so the caller comes here and goes straight back.
 * wlog_on_print(ctx, words) with words = the format, then r4-r10.
 * Frame: 0 back chain, 8-0x24 r3-r10, 0x44 the caller's LR. */
    .globl wlog_hook_print
wlog_hook_print:
    stwu 1, -0x40(1)
    mflr 0
    stw 0, 0x44(1)
    stw 3, 0x08(1)
    stw 4, 0x0C(1)
    stw 5, 0x10(1)
    stw 6, 0x14(1)
    stw 7, 0x18(1)
    stw 8, 0x1C(1)
    stw 9, 0x20(1)
    stw 10, 0x24(1)
    addi 4, 1, 0x08
    bl 1f
1:  mflr 3
    addi 3, 3, wlog_context_data - 1b
    bl wlog_on_print
    lwz 0, 0x44(1)
    mtlr 0
    addi 1, 1, 0x40
    blr

/* One instruction in the middle of __OSLaunchMenu branches here: every
 * register it may still use is kept (r0, r3-r12, LR, CR, CTR, XER),
 * wlog_on_exit(ctx) saves the log, then the displaced instruction runs
 * and a branch (both loader-filled) goes back after it. */
    .globl wlog_hook_exit
wlog_hook_exit:
    stwu 1, -0x60(1)
    stw 0, 0x08(1)
    stw 3, 0x0C(1)
    stw 4, 0x10(1)
    stw 5, 0x14(1)
    stw 6, 0x18(1)
    stw 7, 0x1C(1)
    stw 8, 0x20(1)
    stw 9, 0x24(1)
    stw 10, 0x28(1)
    stw 11, 0x2C(1)
    stw 12, 0x30(1)
    mflr 0
    stw 0, 0x34(1)
    mfcr 0
    stw 0, 0x38(1)
    mfctr 0
    stw 0, 0x3C(1)
    mfxer 0
    stw 0, 0x40(1)
    bl 2f
2:  mflr 3
    addi 3, 3, wlog_context_data - 2b
    bl wlog_on_exit
    lwz 0, 0x40(1)
    mtxer 0
    lwz 0, 0x3C(1)
    mtctr 0
    lwz 0, 0x38(1)
    mtcr 0
    lwz 0, 0x34(1)
    mtlr 0
    lwz 3, 0x0C(1)
    lwz 4, 0x10(1)
    lwz 5, 0x14(1)
    lwz 6, 0x18(1)
    lwz 7, 0x1C(1)
    lwz 8, 0x20(1)
    lwz 9, 0x24(1)
    lwz 10, 0x28(1)
    lwz 11, 0x2C(1)
    lwz 12, 0x30(1)
    lwz 0, 0x08(1)
    addi 1, 1, 0x60
/* Loader-filled: the displaced instruction, then a relative branch to the
 * instruction after it (the blob is within reach: just below the MEM1
 * arena's top). */
    .globl wlog_replay_exit
wlog_replay_exit:
    nop
    nop

    .balign 32
    .globl wlog_context_data
wlog_context_data:
    .long 0x52575758
    .fill 15, 4, 0
