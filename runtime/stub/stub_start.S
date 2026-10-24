/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The return stub's entry (include/riftwii/returnstub.h), linked at
 * 0x80001800. libogc's exit() jumps here when it finds "STUBHAXX" at
 * 0x80001804; the loader fills in the titles before it starts the app. */
#include "riftwii/returnstub.h"

    .section .start, "ax", @progbits
    .globl _rtstub_start
_rtstub_start:
    b 1f
    .long RTSTUB_HBC_MAGIC_STUB
    .long RTSTUB_HBC_MAGIC_HAXX
    .long RTSTUB_MAGIC
    .long RTSTUB_VERSION
    .long __rtstub_image_end - _rtstub_start
    .globl rtstub_count
rtstub_count:
    .long 0                     /* written by the loader */
    .globl rtstub_result
rtstub_result:
    .long 0                     /* written here, for a debugger */
    .globl rtstub_titles
rtstub_titles:
    .space RTSTUB_MAX_TITLES * RTSTUB_TITLE_BYTES
1:
    /* Interrupts off: what is left of the app must not run while IOS
     * takes the console over. The app's stack may be anywhere, so the
     * stub has its own. */
    mfmsr 3
    rlwinm 3, 3, 0, 17, 15
    mtmsr 3
    isync
    lis 1, __rtstub_stack_top@h
    ori 1, 1, __rtstub_stack_top@l
    li 0, 0
    stwu 0, -16(1)
    bl rtstub_main
2:
    b 2b
