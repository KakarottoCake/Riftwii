/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Resident runtime: blob header, the IOS_IoctlAsync trampoline, the IPC
 * completion entry and the context. Everything lives in one section so
 * that every address the code needs is a same-section difference, which
 * keeps the blob position independent: the loader may copy it anywhere.
 *
 * The header layout is struct rt_blob_header (rt_hook.h); the context is
 * struct rt_context, defined here so it sits in .text next to the code.
 */

    .section .text.rt_entry, "ax", @progbits
    .globl _rt_blob_start
_rt_blob_start:
    .long 0x5257484B                          /* magic 'RWHK' */
    .long 2                                   /* version */
    .long _rt_blob_end - _rt_blob_start       /* blob_size */
    .long rt_context_data - _rt_blob_start    /* context_offset */
    .long rt_hook_ioctl_async - _rt_blob_start
    .long rt_replay_ioctl_async - _rt_blob_start
    .long rt_continue_ioctl_async - _rt_blob_start
    .long rt_complete_di - _rt_blob_start

/*
 * Runs in place of the first four instructions of IOS_IoctlAsync: r3-r10
 * hold its eight arguments and LR the caller's return address. Calls the C
 * handler with the arguments saved on the stack (the handler may rewrite
 * them), then either replays the displaced instructions and continues in
 * the original function, or returns the handler's result to the caller.
 *
 * Frame (0x40 bytes): 0 back chain, 4 callee LR save, 8..0x27 the eight
 * arguments, 0x28 result slot. CR0 and CTR are free to clobber at a
 * function entry (EABI volatile); r14-r31 are preserved by the C code.
 */
    .globl rt_hook_ioctl_async
rt_hook_ioctl_async:
    stwu    1, -0x40(1)
    mflr    0
    stw     0, 0x44(1)
    stw     3, 0x08(1)
    stw     4, 0x0C(1)
    stw     5, 0x10(1)
    stw     6, 0x14(1)
    stw     7, 0x18(1)
    stw     8, 0x1C(1)
    stw     9, 0x20(1)
    stw     10, 0x24(1)
    bl      1f
1:  mflr    3
    addi    3, 3, rt_context_data - 1b        /* r3 = &context, PC-relative */
    addi    4, 1, 0x08                        /* r4 = saved arguments */
    addi    5, 1, 0x28                        /* r5 = result slot */
    bl      rt_on_ioctl_async
    mr      12, 3                             /* verdict */
    lwz     3, 0x08(1)
    lwz     4, 0x0C(1)
    lwz     5, 0x10(1)
    lwz     6, 0x14(1)
    lwz     7, 0x18(1)
    lwz     8, 0x1C(1)
    lwz     9, 0x20(1)
    lwz     10, 0x24(1)
    lwz     0, 0x44(1)
    mtlr    0
    cmpwi   12, 0
    bne     2f
    addi    1, 1, 0x40
    b       rt_replay_ioctl_async
2:  lwz     3, 0x28(1)                        /* hijacked: hand the caller the handler's result */
    addi    1, 1, 0x40
    blr

/* The loader stores the four displaced instructions here... */
    .globl rt_replay_ioctl_async
rt_replay_ioctl_async:
    nop
    nop
    nop
    nop
/* ...and patches this absolute jump to the hooked function + 16. r12 is a
 * volatile scratch register no prologue keeps live. */
    .globl rt_continue_ioctl_async
rt_continue_ioctl_async:
    lis     12, 0
    ori     12, 12, 0
    mtctr   12
    bctr

/*
 * IPC completion entry, installed as the callback of redirected reads:
 * r3 = IPC result, r4 = the pending record. Applies the redirect, then
 * tail-calls the game's callback with (result, its user data) or simply
 * returns when the game passed none.
 *
 * Frame (0x20 bytes): 0 back chain, 4 callee LR save, 8 result, 0xC the
 * game's callback, 0x10 the game's user data.
 */
    .globl rt_complete_di
rt_complete_di:
    stwu    1, -0x20(1)
    mflr    0
    stw     0, 0x24(1)
    stw     3, 0x08(1)
    mr      5, 4                              /* r5 = record */
    mr      4, 3                              /* r4 = result */
    bl      3f
3:  mflr    3
    addi    3, 3, rt_context_data - 3b        /* r3 = &context */
    addi    6, 1, 0x0C                        /* r6 = &callback */
    addi    7, 1, 0x10                        /* r7 = &user data */
    bl      rt_on_di_complete
    lwz     3, 0x08(1)
    lwz     4, 0x10(1)
    lwz     12, 0x0C(1)
    lwz     0, 0x24(1)
    mtlr    0
    addi    1, 1, 0x20
    cmpwi   12, 0
    beqlr
    mtctr   12
    bctr

    .balign 32
    .globl rt_context_data
rt_context_data:
    .long 0x52574358                          /* magic 'RWCX' */
    .long 0                                   /* flags */
    .long 1                                   /* gecko_channel: slot B */
    .long 0                                   /* original_ioctl_async */
    .fill 52, 4, 0                            /* counters, redirection state, pending records: 224 bytes total */

/* _rt_blob_end is defined by rt.ld after the C code. */
