/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The virtual SD card blob's header and trampolines (vsd_hook.h). */
#include "vsd_hook.h"

    .section .text.vsd_entry, "ax", @progbits
    .globl _vsd_blob_start
_vsd_blob_start:
    .long 0x52575653, 3, _vsd_blob_end - _vsd_blob_start, vsd_context_data - _vsd_blob_start
    .long vsd_complete - _vsd_blob_start
    .long vsd_hook_0 - _vsd_blob_start, vsd_hook_1 - _vsd_blob_start, vsd_hook_2 - _vsd_blob_start
    .long vsd_hook_3 - _vsd_blob_start, vsd_hook_4 - _vsd_blob_start, vsd_hook_5 - _vsd_blob_start
    .long vsd_hook_6 - _vsd_blob_start, vsd_hook_7 - _vsd_blob_start
    .long vsd_replay_0 - _vsd_blob_start, vsd_replay_1 - _vsd_blob_start, vsd_replay_2 - _vsd_blob_start
    .long vsd_replay_3 - _vsd_blob_start, vsd_replay_4 - _vsd_blob_start, vsd_replay_5 - _vsd_blob_start
    .long vsd_replay_6 - _vsd_blob_start, vsd_replay_7 - _vsd_blob_start
    .long vsd_continue_0 - _vsd_blob_start, vsd_continue_1 - _vsd_blob_start, vsd_continue_2 - _vsd_blob_start
    .long vsd_continue_3 - _vsd_blob_start, vsd_continue_4 - _vsd_blob_start, vsd_continue_5 - _vsd_blob_start
    .long vsd_continue_6 - _vsd_blob_start, vsd_continue_7 - _vsd_blob_start

/* The entries. A call on a handle other than the card's (VSD_FAKE_FD)
 * goes straight to its replay slot, the original function, with no frame
 * and no C call: every IOS call the game makes passes here, and only the
 * card's and the opens need the C code. The opens (0 and 4) always go to
 * C, which looks at the path. Uses only r11 (the entry's number), cr0 and,
 * in the shared body, r0, r12 and ctr, all free at a function's entry; the
 * C code would change them anyway. */
    .macro VSD_HOOK_OPEN n
    .globl vsd_hook_\n
vsd_hook_\n:
    li 11, \n
    b vsd_hook_common
    .endm

    .macro VSD_HOOK_FD n
    .globl vsd_hook_\n
vsd_hook_\n:
    cmpwi 3, VSD_FAKE_FD
    bne vsd_replay_\n
    li 11, \n
    b vsd_hook_common
    .endm

    VSD_HOOK_OPEN 0
    VSD_HOOK_FD 1
    VSD_HOOK_FD 2
    VSD_HOOK_FD 3
    VSD_HOOK_OPEN 4
    VSD_HOOK_FD 5
    VSD_HOOK_FD 6
    VSD_HOOK_FD 7

/* vsd_on_ipc(ctx, entry, args, &result): non-zero when it answered, and
 * the function returns the result; zero runs the original (replay slot
 * r11, at vsd_replay_0 + r11 * 32) with the arguments as they were.
 * Frame: 0 back chain, 4 LR, 8..39 r3..r10, 40 result, 44 the entry,
 * 48 the context's address. */
    .globl vsd_hook_common
vsd_hook_common:
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
    stw 11, 0x2C(1)
    bl 1f
1:  mflr 3
    addi 3, 3, vsd_context_data - 1b
    stw 3, 0x30(1)
    mr 4, 11
    addi 5, 1, 0x08
    addi 6, 1, 0x28
    bl vsd_on_ipc
    cmpwi 3, 0
    lwz 12, 0x30(1)
    lwz 11, 0x2C(1)
    addi 12, 12, vsd_replay_0 - vsd_context_data
    slwi 11, 11, 5
    add 12, 12, 11
    mtctr 12
    lwz 3, 0x08(1)
    lwz 4, 0x0C(1)
    lwz 5, 0x10(1)
    lwz 6, 0x14(1)
    lwz 7, 0x18(1)
    lwz 8, 0x1C(1)
    lwz 9, 0x20(1)
    lwz 10, 0x24(1)
    lwz 0, 0x44(1)
    mtlr 0
    bne 2f
    addi 1, 1, 0x40
    bctr
2:  lwz 3, 0x28(1)
    addi 1, 1, 0x40
    blr

/* The callback of the blob's own requests: (result, tag). vsd_on_complete
 * (ctx, &result, tag, &callback, &user data) hands back the game's
 * callback, which is tail-called with the result, or 0 while more of the
 * blob's requests are in flight. Frame: 8 result, 12 callback, 16 user
 * data. */
    .globl vsd_complete
vsd_complete:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    mr 5, 4
    addi 4, 1, 0x08
    bl 3f
3:  mflr 3
    addi 3, 3, vsd_context_data - 3b
    addi 6, 1, 0x0C
    addi 7, 1, 0x10
    bl vsd_on_complete
    lwz 3, 0x08(1)
    lwz 4, 0x10(1)
    lwz 12, 0x0C(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    cmpwi 12, 0
    beqlr
    mtctr 12
    bctr

/* Loader-filled: the function's first word (or another blob's branch,
 * retargeted) and nops, then the jump to its second word through r12.
 * 32 bytes each, one after the other from a 16-byte boundary. */
    .macro VSD_REPLAY n
    .globl vsd_replay_\n
vsd_replay_\n:
    nop
    nop
    nop
    nop
    .globl vsd_continue_\n
vsd_continue_\n:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr
    .endm
    .balign 16
    VSD_REPLAY 0
    VSD_REPLAY 1
    VSD_REPLAY 2
    VSD_REPLAY 3
    VSD_REPLAY 4
    VSD_REPLAY 5
    VSD_REPLAY 6
    VSD_REPLAY 7
/* vsd_hook_common finds slot n at vsd_replay_0 + n * 32. */
    .if (vsd_replay_7 - vsd_replay_0) != 7 * 32
    .error "the replay slots are not 32 bytes apart"
    .endif

    .balign 32
    .globl vsd_context_data
vsd_context_data:
    .long 0x52575658
    .fill 7, 4, 0
