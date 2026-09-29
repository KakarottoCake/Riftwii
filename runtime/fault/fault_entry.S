/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The game crash blob's header and trampoline (fault_hook.h). */
#include "fault_hook.h"

    .section .text.fault_entry, "ax", @progbits
    .globl _fault_blob_start
_fault_blob_start:
    .long 0x52574642, 1, _fault_blob_end - _fault_blob_start, fault_context_data - _fault_blob_start
    .long fault_hook - _fault_blob_start, fault_replay - _fault_blob_start, fault_continue - _fault_blob_start

/* __OSUnhandledException(exception, context, dsisr, dar): the game's
 * registers are kept in the context, fault_on_exception(ctx, exception,
 * context, dsisr, dar) runs on the blob's own stack, then everything is
 * put back and the function runs as it would have. */
    .globl fault_hook
fault_hook:
    mflr 0
    bl 1f
1:  mflr 12
    addi 12, 12, fault_context_data - 1b
    stw 0, RT_FAULT_CTX_SAVE + 0(12)
    stw 1, RT_FAULT_CTX_SAVE + 4(12)
    stw 3, RT_FAULT_CTX_SAVE + 8(12)
    stw 4, RT_FAULT_CTX_SAVE + 12(12)
    stw 5, RT_FAULT_CTX_SAVE + 16(12)
    stw 6, RT_FAULT_CTX_SAVE + 20(12)
    lwz 1, RT_FAULT_CTX_STACK_TOP(12)
    li 0, 0
    stw 0, 0(1)                         /* the back chain ends here */
    mr 7, 6
    mr 6, 5
    mr 5, 4
    mr 4, 3
    mr 3, 12
    bl fault_on_exception
    bl 2f
2:  mflr 12
    addi 12, 12, fault_context_data - 2b
    lwz 0, RT_FAULT_CTX_SAVE + 0(12)
    mtlr 0
    lwz 1, RT_FAULT_CTX_SAVE + 4(12)
    lwz 3, RT_FAULT_CTX_SAVE + 8(12)
    lwz 4, RT_FAULT_CTX_SAVE + 12(12)
    lwz 5, RT_FAULT_CTX_SAVE + 16(12)
    lwz 6, RT_FAULT_CTX_SAVE + 20(12)
    b fault_replay

/* Loader-filled: the function's first word and nops, then the jump to
 * its second word through r12. */
    .balign 16
    .globl fault_replay
fault_replay:
    nop
    nop
    nop
    nop
    .globl fault_continue
fault_continue:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr

    .balign 32
    .globl fault_context_data
fault_context_data:
    .long 0x52574658
    .fill 31, 4, 0
