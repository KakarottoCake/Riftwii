/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ABI v5: async commands 1..7 occupy entries 0..6; sync 1..7 are 7..13. */
    .section .text.rt_entry, "ax", @progbits
    .globl _rt_blob_start
_rt_blob_start:
    .long 0x5257484B, 5, _rt_blob_end - _rt_blob_start, rt_context_data - _rt_blob_start
    .long rt_hook_0 - _rt_blob_start, rt_hook_1 - _rt_blob_start, rt_hook_2 - _rt_blob_start, rt_hook_3 - _rt_blob_start
    .long rt_hook_4 - _rt_blob_start, rt_hook_5 - _rt_blob_start, rt_hook_6 - _rt_blob_start, rt_hook_7 - _rt_blob_start
    .long rt_hook_8 - _rt_blob_start, rt_hook_9 - _rt_blob_start, rt_hook_10 - _rt_blob_start, rt_hook_11 - _rt_blob_start
    .long rt_hook_12 - _rt_blob_start, rt_hook_13 - _rt_blob_start
    .long rt_replay_0 - _rt_blob_start, rt_replay_1 - _rt_blob_start, rt_replay_2 - _rt_blob_start, rt_replay_3 - _rt_blob_start
    .long rt_replay_4 - _rt_blob_start, rt_replay_5 - _rt_blob_start, rt_replay_6 - _rt_blob_start, rt_replay_7 - _rt_blob_start
    .long rt_replay_8 - _rt_blob_start, rt_replay_9 - _rt_blob_start, rt_replay_10 - _rt_blob_start, rt_replay_11 - _rt_blob_start
    .long rt_replay_12 - _rt_blob_start, rt_replay_13 - _rt_blob_start
    .long rt_continue_0 - _rt_blob_start, rt_continue_1 - _rt_blob_start, rt_continue_2 - _rt_blob_start, rt_continue_3 - _rt_blob_start
    .long rt_continue_4 - _rt_blob_start, rt_continue_5 - _rt_blob_start, rt_continue_6 - _rt_blob_start, rt_continue_7 - _rt_blob_start
    .long rt_continue_8 - _rt_blob_start, rt_continue_9 - _rt_blob_start, rt_continue_10 - _rt_blob_start, rt_continue_11 - _rt_blob_start
    .long rt_continue_12 - _rt_blob_start, rt_continue_13 - _rt_blob_start
    .long rt_complete_di - _rt_blob_start
    .long rt_complete_fs - _rt_blob_start
    .long rt_open_undo_0 - _rt_blob_start, rt_open_undo_1 - _rt_blob_start

/* Frame: 0 back chain, 4 LR, 8..27 r3..r10, 28 result. */
    .macro RT_HOOK n
    .globl rt_hook_\n
rt_hook_\n:
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
    bl 1f
1:  mflr 3
    addi 3, 3, rt_context_data - 1b
    li 4, \n
    addi 5, 1, 0x08
    addi 6, 1, 0x28
    bl rt_on_ipc
    mr 12, 3
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
    cmpwi 12, 0
    bne 2f
    addi 1, 1, 0x40
    b rt_replay_\n
2:  lwz 3, 0x28(1)
    addi 1, 1, 0x40
    blr
    .endm

/* Synchronous IOS_Open (entry 7): RT_HOOK with a loader-filled word after
 * each frame pop (rt_blob_header.open_undo_offset). */
    .globl rt_hook_7
rt_hook_7:
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
    bl 1f
1:  mflr 3
    addi 3, 3, rt_context_data - 1b
    li 4, 7
    addi 5, 1, 0x08
    addi 6, 1, 0x28
    bl rt_on_ipc
    mr 12, 3
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
    cmpwi 12, 0
    bne 2f
    addi 1, 1, 0x40
    .globl rt_open_undo_0
rt_open_undo_0:
    nop
    b rt_replay_7
2:  lwz 3, 0x28(1)
    addi 1, 1, 0x40
    .globl rt_open_undo_1
rt_open_undo_1:
    nop
    blr

    RT_HOOK 0
    RT_HOOK 1
    RT_HOOK 2
    RT_HOOK 3
    RT_HOOK 4
    RT_HOOK 5
    RT_HOOK 6
    RT_HOOK 8
    RT_HOOK 9
    RT_HOOK 10
    RT_HOOK 11
    RT_HOOK 12
    RT_HOOK 13

/* Each loader-filled replay is 16 bytes and its continuation follows it. */
    .macro RT_REPLAY n
    .balign 16
    .globl rt_replay_\n
rt_replay_\n:
    nop
    nop
    nop
    nop
    .globl rt_continue_\n
rt_continue_\n:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr
    .endm
    RT_REPLAY 0
    RT_REPLAY 1
    RT_REPLAY 2
    RT_REPLAY 3
    RT_REPLAY 4
    RT_REPLAY 5
    RT_REPLAY 6
    RT_REPLAY 7
    RT_REPLAY 8
    RT_REPLAY 9
    RT_REPLAY 10
    RT_REPLAY 11
    RT_REPLAY 12
    RT_REPLAY 13

    .globl rt_complete_di
rt_complete_di:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    mr 5, 4
    addi 4, 1, 0x08
    bl 3f
3:  mflr 3
    addi 3, 3, rt_context_data - 3b
    addi 6, 1, 0x0C
    addi 7, 1, 0x10
    bl rt_on_di_complete
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

/* Savegame completion entry (slice 4B3): same shape as rt_complete_di,
 * but the record tag selects an FS pend or snoop slot in rt_on_fs_complete.
 * Frame: 0 back chain, 4 LR, 8 result image, 12 callback, 16 user data. */
    .globl rt_complete_fs
rt_complete_fs:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    mr 5, 4
    addi 4, 1, 0x08
    bl 4f
4:  mflr 3
    addi 3, 3, rt_context_data - 4b
    addi 6, 1, 0x0C
    addi 7, 1, 0x10
    bl rt_on_fs_complete
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

    .balign 32
    .globl rt_context_data
rt_context_data:
    .long 0x52574358, 0, 1, 0
    .fill 508, 4, 0
