/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The screenshot blob's header and trampolines (shot_hook.h). */
    .section .text.shot_entry, "ax", @progbits
    .globl _shot_blob_start
_shot_blob_start:
    .long 0x52575353, 1, _shot_blob_end - _shot_blob_start, shot_context_data - _shot_blob_start
    .long shot_hook_ioctlv - _shot_blob_start, shot_replay_ioctlv - _shot_blob_start, shot_continue_ioctlv - _shot_blob_start
    .long shot_hook_pad - _shot_blob_start, shot_replay_pad - _shot_blob_start, shot_continue_pad - _shot_blob_start
    .long shot_complete_bt - _shot_blob_start, shot_complete_nand - _shot_blob_start

/* s32 IOS_IoctlvAsync(fd, ioctl, in count, io count, vectors, callback,
 * user data): shot_on_ioctlv(ctx, args) may put this blob's callback in
 * place of the game's (a watched Bluetooth read) and returns its slot;
 * the original runs with the arguments as they are then, and
 * shot_after_ioctlv(ctx, slot, result) frees the slot when IOS refused
 * the request. Frame: 8-32 the seven arguments, 36 the slot. */
    .globl shot_hook_ioctlv
shot_hook_ioctlv:
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
    addi 4, 1, 0x08
    bl 1f
1:  mflr 3
    addi 3, 3, shot_context_data - 1b
    bl shot_on_ioctlv
    stw 3, 0x24(1)
    lwz 3, 0x08(1)
    lwz 4, 0x0C(1)
    lwz 5, 0x10(1)
    lwz 6, 0x14(1)
    lwz 7, 0x18(1)
    lwz 8, 0x1C(1)
    lwz 9, 0x20(1)
    bl shot_replay_ioctlv
    mr 5, 3
    lwz 4, 0x24(1)
    bl 2f
2:  mflr 3
    addi 3, 3, shot_context_data - 2b
    bl shot_after_ioctlv
    lwz 0, 0x44(1)
    mtlr 0
    addi 1, 1, 0x40
    blr

/* u32 PADRead(PADStatus status[4]): the original, then
 * shot_on_pad(ctx, status); its result is returned. Frame: 8 status,
 * 12 result. */
    .globl shot_hook_pad
shot_hook_pad:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    bl shot_replay_pad
    stw 3, 0x0C(1)
    lwz 4, 0x08(1)
    bl 3f
3:  mflr 3
    addi 3, 3, shot_context_data - 3b
    bl shot_on_pad
    lwz 3, 0x0C(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

/* The SDK's IPC callbacks: r3 result, r4 user data. */
    .globl shot_complete_bt
shot_complete_bt:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    mr 5, 4
    mr 4, 3
    bl 4f
4:  mflr 3
    addi 3, 3, shot_context_data - 4b
    bl shot_on_bt
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

    .globl shot_complete_nand
shot_complete_nand:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    mr 4, 3
    bl 5f
5:  mflr 3
    addi 3, 3, shot_context_data - 5b
    bl shot_on_nand
    li 3, 0
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

/* Loader-filled: the hooked function's first word (or another blob's
 * branch, retargeted) and nops, then the jump to its second word through
 * r12. */
    .macro SHOT_REPLAY name
    .balign 16
    .globl shot_replay_\name
shot_replay_\name:
    nop
    nop
    nop
    nop
    .globl shot_continue_\name
shot_continue_\name:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr
    .endm
    SHOT_REPLAY ioctlv
    SHOT_REPLAY pad

    .balign 32
    .globl shot_context_data
shot_context_data:
    .long 0x52575358
    .fill 31, 4, 0
