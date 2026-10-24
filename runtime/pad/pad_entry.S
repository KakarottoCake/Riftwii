/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-FileCopyrightText: 2017 Alex Chadwick (wup-028-bslug) <https://github.com/Chadderz121/wup-028-bslug> */
/* SPDX-FileCopyrightText: FIX94 and the Nintendont contributors, used with permission <https://github.com/FIX94/Nintendont> */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube adapter blob's header and trampolines (pad_hook.h). */
    .section .text.pad_entry, "ax", @progbits
    .globl _pad_blob_start
_pad_blob_start:
    .long 0x52575044, 3, _pad_blob_end - _pad_blob_start, pad_context_data - _pad_blob_start
    .long pad_hook_read - _pad_blob_start, pad_replay_read - _pad_blob_start, pad_continue_read - _pad_blob_start
    .long pad_hook_motor - _pad_blob_start, pad_replay_motor - _pad_blob_start, pad_continue_motor - _pad_blob_start
    .long pad_complete - _pad_blob_start
    .long pad_hook_submit - _pad_blob_start, pad_replay_submit - _pad_blob_start, pad_continue_submit - _pad_blob_start
    .long pad_hook_sleep - _pad_blob_start, pad_replay_sleep - _pad_blob_start, pad_continue_sleep - _pad_blob_start

/* u32 PADRead(PADStatus status[4]): the original first, through the
 * replay slot (which returns here), then pad_on_read(ctx, status, its
 * result) fills the ports the game has no pad on and returns the motor
 * bits. Frame: 0 back chain, 8 status. */
    .globl pad_hook_read
pad_hook_read:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    bl pad_replay_read
    mr 5, 3
    lwz 4, 0x08(1)
    bl 1f
1:  mflr 3
    addi 3, 3, pad_context_data - 1b
    bl pad_on_read
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

/* void PADControlMotor(s32 chan, u32 command): the original, then
 * pad_on_motor(ctx, chan, command). Frame: 8 chan, 12 command. */
    .globl pad_hook_motor
pad_hook_motor:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    stw 4, 0x0C(1)
    bl pad_replay_motor
    lwz 4, 0x08(1)
    lwz 5, 0x0C(1)
    bl 2f
2:  mflr 3
    addi 3, 3, pad_context_data - 2b
    bl pad_on_motor
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

/* The SDK's IPC callback: r3 result, r4 user data (the driver's tag).
 * pad_on_complete(ctx, result, tag); returns 0. */
    .globl pad_complete
pad_complete:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    mr 5, 4
    mr 4, 3
    bl 3f
3:  mflr 3
    addi 3, 3, pad_context_data - 3b
    bl pad_on_complete
    li 3, 0
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

/* The SDK's IPC submit routine (request block, callback): every IOS
 * request the game makes passes here. pad_on_submit(ctx, block) notes
 * save activity, then the routine itself runs from its first word, as
 * if called directly. Frame: 8 block, 12 callback. */
    .globl pad_hook_submit
pad_hook_submit:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    stw 4, 0x0C(1)
    mr 4, 3
    bl 4f
4:  mflr 3
    addi 3, 3, pad_context_data - 4b
    bl pad_on_submit
    lwz 3, 0x08(1)
    lwz 4, 0x0C(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    b pad_replay_submit

/* OSSleepThread(queue): pad_on_sleep(ctx) returns the game's
 * OSReturnToMenu when the way back was asked for and this is the main
 * thread, else 0. With an address, interrupts go on (as the HOME Menu's
 * Wii Menu button has them) and OSReturnToMenu takes over, never to
 * return; otherwise OSSleepThread runs from its first word, as if called
 * directly. Frame: 8 queue. */
    .globl pad_hook_sleep
pad_hook_sleep:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    bl 5f
5:  mflr 3
    addi 3, 3, pad_context_data - 5b
    bl pad_on_sleep
    mr 12, 3
    lwz 3, 0x08(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    cmpwi 12, 0
    beq pad_replay_sleep
    mfmsr 11
    ori 11, 11, 0x8000
    mtmsr 11
    mtctr 12
    bctr

/* Loader-filled: the hooked function's first word (and nops), then the
 * jump to its second word through r12. */
    .macro PAD_REPLAY name
    .balign 16
    .globl pad_replay_\name
pad_replay_\name:
    nop
    nop
    nop
    nop
    .globl pad_continue_\name
pad_continue_\name:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr
    .endm
    PAD_REPLAY read
    PAD_REPLAY motor
    PAD_REPLAY submit
    PAD_REPLAY sleep

    .balign 32
    .globl pad_context_data
pad_context_data:
    .long 0x52575043
    .fill 47, 4, 0
