/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube disc blob's header and trampolines (gc_rt.h). Each hook
 * replaces a DVDLow* function at its first word: the game calls it with
 * its own arguments and gets 1 (the command was taken) back. */
#include "gc_layout.h"

    .section .text.gc_entry, "ax", @progbits
    .globl _gc_blob_start
_gc_blob_start:
    .long 0x52574744, 6, _gc_blob_end - _gc_blob_start, gc_context_data - _gc_blob_start
    .long gc_hook_read - _gc_blob_start, gc_hook_seek - _gc_blob_start
    .long gc_hook_disk_id - _gc_blob_start, gc_hook_inquiry - _gc_blob_start
    .long gc_hook_cb_r3 - _gc_blob_start, gc_hook_audio_status - _gc_blob_start
    .long gc_hook_cb_r5 - _gc_blob_start, gc_hook_audio_stream - _gc_blob_start
    .long gc_alarm - _gc_blob_start
    .long gc_hook_ai_dma - _gc_blob_start, gc_replay_ai_dma - _gc_blob_start, gc_continue_ai_dma - _gc_blob_start
    .long gc_hook_pad_read - _gc_blob_start, gc_replay_pad_read - _gc_blob_start, gc_continue_pad_read - _gc_blob_start
    .long gc_hook_ar_dma - _gc_blob_start, gc_replay_ar_dma - _gc_blob_start, gc_continue_ar_dma - _gc_blob_start
    .long gc_trace_now - _gc_blob_start
    .long gc_hook_fatal - _gc_blob_start
    .long gc_dsi_entry - _gc_blob_start
    .long gc_exi_table - _gc_blob_start
    .long gc_hook_vi_configure - _gc_blob_start, gc_replay_vi_configure - _gc_blob_start
    .long gc_continue_vi_configure - _gc_blob_start
    .long gc_hook_projection - _gc_blob_start, gc_replay_projection - _gc_blob_start
    .long gc_continue_projection - _gc_blob_start
    .long gc_stray_entry - _gc_blob_start
    .long gc_hook_load_queued - _gc_blob_start
    .long gc_hook_load_alloc - _gc_blob_start
    .long gc_hook_load_callbacks - _gc_blob_start, gc_hook_load_callbacks_done - _gc_blob_start
    .long gc_hook_pad_motor - _gc_blob_start, gc_replay_pad_motor - _gc_blob_start
    .long gc_continue_pad_motor - _gc_blob_start
    .long gc_hook_reset_system - _gc_blob_start, gc_replay_reset_system - _gc_blob_start
    .long gc_continue_reset_system - _gc_blob_start
    .long gc_hook_hot_reset - _gc_blob_start

/* Calls `fn` with the context in r3 and the game's r3..r6 moved up one
 * register (r4..r7). */
    .macro GC_HOOK name, fn
    .globl gc_hook_\name
gc_hook_\name:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    mr 7, 6
    mr 6, 5
    mr 5, 4
    mr 4, 3
    bl 1f
1:  mflr 3
    addi 3, 3, gc_context_data - 1b
    bl \fn
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr
    .endm

    GC_HOOK read, gc_on_read
    GC_HOOK disk_id, gc_on_disk_id
    GC_HOOK inquiry, gc_on_inquiry
    GC_HOOK audio_status, gc_on_audio_status
    GC_HOOK audio_stream, gc_on_audio_stream
    GC_HOOK fatal, gc_on_fatal
    GC_HOOK load_queued, gc_on_load_queued
    GC_HOOK load_callbacks_done, gc_on_load_callbacks_done

/* Luigi's Mansion's loading thread: calls of `fn` that also get its
 * request, which the thread keeps in r30 (the loader checked), after the
 * game's r3 and r4: fn(ctx, r3, r4, request). */
    .macro GC_LOAD_HOOK name, fn
    .globl gc_hook_\name
gc_hook_\name:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    mr 6, 30
    mr 5, 4
    mr 4, 3
    bl 8f
8:  mflr 3
    addi 3, 3, gc_context_data - 8b
    bl \fn
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr
    .endm

    GC_LOAD_HOOK load_alloc, gc_on_load_alloc
    GC_LOAD_HOOK load_callbacks, gc_on_load_callbacks

/* AIInitDMA(start, length): gc_on_ai_dma(ctx, start, length) mixes the
 * disc stream into the buffer, then the original runs from the replay
 * slot with the game's arguments. Frame: 8 start, 12 length. */
    .globl gc_hook_ai_dma
gc_hook_ai_dma:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    stw 4, 0x0C(1)
    mr 5, 4
    mr 4, 3
    bl 4f
4:  mflr 3
    addi 3, 3, gc_context_data - 4b
    bl gc_on_ai_dma
    lwz 3, 0x08(1)
    lwz 4, 0x0C(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    b gc_replay_ai_dma

/* u32 PADRead(PADStatus status[4]): the original through its replay
 * slot, then gc_on_pad_read(ctx, status, its result). */
    .globl gc_hook_pad_read
gc_hook_pad_read:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    bl gc_replay_pad_read
    mr 5, 3
    lwz 4, 0x08(1)
    bl 5f
5:  mflr 3
    addi 3, 3, gc_context_data - 5b
    bl gc_on_pad_read
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

    .balign 16
    .globl gc_replay_pad_read
gc_replay_pad_read:
    nop
    nop
    nop
    nop
    .globl gc_continue_pad_read
gc_continue_pad_read:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr

/* void PADControlMotor(s32 chan, u32 command): the original through its
 * replay slot (the pad of a GameCube controller there takes it), then
 * gc_on_pad_motor(ctx, chan, command) for a Wii Remote on that port.
 * Frame: 8 chan, 12 command. */
    .globl gc_hook_pad_motor
gc_hook_pad_motor:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    stw 4, 0x0C(1)
    bl gc_replay_pad_motor
    lwz 4, 0x08(1)
    lwz 5, 0x0C(1)
    bl 8f
8:  mflr 3
    addi 3, 3, gc_context_data - 8b
    bl gc_on_pad_motor
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

    .balign 16
    .globl gc_replay_pad_motor
gc_replay_pad_motor:
    nop
    nop
    nop
    nop
    .globl gc_continue_pad_motor
gc_continue_pad_motor:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr

/* ARStartDMA(type, main, aram, length): gc_on_ar_dma does the transfer
 * itself and returns a 32-byte scratch buffer's physical address; the
 * original then runs from its replay slot on that buffer for 32 bytes,
 * with the game's own ARAM address, so the hardware still raises its
 * interrupt but writes nothing of the game's (gc_on_ar_dma says why the
 * buffer holds what it does). The original is called rather than jumped
 * to, so gc_after_ar_dma runs once it has started the hardware: it waits
 * for the transfer's interrupt to be pending before the game goes on.
 * Frame: 8 r3, 12 r4, 16 r5, 20 r6. */
    .globl gc_hook_ar_dma
gc_hook_ar_dma:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    stw 4, 0x0C(1)
    stw 5, 0x10(1)
    stw 6, 0x14(1)
    mr 7, 6
    mr 6, 5
    mr 5, 4
    mr 4, 3
    bl 6f
6:  mflr 3
    addi 3, 3, gc_context_data - 6b
    bl gc_on_ar_dma
    mr 4, 3
    lwz 3, 0x08(1)
    lwz 5, 0x10(1)
    li 6, 32
    bl gc_replay_ar_dma
    bl 7f
7:  mflr 3
    addi 3, 3, gc_context_data - 7b
    bl gc_after_ar_dma
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

    .balign 16
    .globl gc_replay_ar_dma
gc_replay_ar_dma:
    nop
    nop
    nop
    nop
    .globl gc_continue_ar_dma
gc_continue_ar_dma:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr

/* Loader-filled: AIInitDMA's first word (and nops), then the jump to its
 * second word through r12. */
    .balign 16
    .globl gc_replay_ai_dma
gc_replay_ai_dma:
    nop
    nop
    nop
    nop
    .globl gc_continue_ai_dma
gc_continue_ai_dma:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr

/* VIConfigure(rm), for 480p and the picture width: gc_on_vi_configure(ctx,
 * rm) returns the mode to use (a changed copy, or the game's own), and the
 * original runs on it from its replay slot. */
    .globl gc_hook_vi_configure
gc_hook_vi_configure:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    mr 4, 3
    bl 1f
1:  mflr 3
    addi 3, 3, gc_context_data - 1b
    bl gc_on_vi_configure
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    b gc_replay_vi_configure

    .balign 16
    .globl gc_replay_vi_configure
gc_replay_vi_configure:
    nop
    nop
    nop
    nop
    .globl gc_continue_vi_configure
gc_continue_vi_configure:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr

/* GXSetProjection(mtx, type), for 16:9: gc_on_projection(ctx, mtx, type,
 * caller) returns the matrix to draw from (a widened copy of a
 * perspective one, or the game's own), and the original runs on it from
 * its replay slot with the game's type. No floating point here or in
 * the C: the game's FPU registers stay as the game left them.
 * Frame: 12 type. */
    .globl gc_hook_projection
gc_hook_projection:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 4, 0x0C(1)
    mr 6, 0
    mr 5, 4
    mr 4, 3
    bl 1f
1:  mflr 3
    addi 3, 3, gc_context_data - 1b
    bl gc_on_projection
    lwz 4, 0x0C(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    b gc_replay_projection

    .balign 16
    .globl gc_replay_projection
gc_replay_projection:
    nop
    nop
    nop
    nop
    .globl gc_continue_projection
gc_continue_projection:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr

/* OSResetSystem(reset, code, menu), for the restart: gc_on_reset_system(ctx,
 * args) may change the three (args: the frame's words at 8, 12 and 16),
 * and the original runs on them from its replay slot. */
    .globl gc_hook_reset_system
gc_hook_reset_system:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    stw 3, 0x08(1)
    stw 4, 0x0C(1)
    stw 5, 0x10(1)
    addi 4, 1, 0x08
    bl 1f
1:  mflr 3
    addi 3, 3, gc_context_data - 1b
    bl gc_on_reset_system
    lwz 3, 0x08(1)
    lwz 4, 0x0C(1)
    lwz 5, 0x10(1)
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    b gc_replay_reset_system

    .balign 16
    .globl gc_replay_reset_system
gc_replay_reset_system:
    nop
    nop
    nop
    nop
    .globl gc_continue_reset_system
gc_continue_reset_system:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr

/* The SDK's reset routine(code << 3), in place of the write to PI's reset
 * register: gc_on_hot_reset(ctx, code) restarts the game and never
 * returns. */
    .globl gc_hook_hot_reset
gc_hook_hot_reset:
    stwu 1, -0x20(1)
    mr 4, 3
    bl 1f
1:  mflr 3
    addi 3, 3, gc_context_data - 1b
    bl gc_on_hot_reset
2:  b 2b

/* Commands with nothing to do but their callback, in r3..r6: moved to
 * r4 for gc_on_done(ctx, cb). */
    .macro GC_DONE name, reg
    .globl gc_hook_\name
gc_hook_\name:
    mr 4, \reg
    b gc_done_common
    .endm

    GC_DONE seek, 4
    GC_DONE cb_r3, 3
    GC_DONE cb_r5, 5

gc_done_common:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    bl 2f
2:  mflr 3
    addi 3, 3, gc_context_data - 2b
    bl gc_on_done
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

/* trace_now(where): one trace record, for the loader. */
    .globl gc_trace_now
gc_trace_now:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    mr 4, 3
    bl 7f
7:  mflr 3
    addi 3, 3, gc_context_data - 7b
    bl gc_on_trace_now
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

/* The OSAlarm handler (alarm, context): runs the pending callback, or
 * takes the heartbeat's sample of the interrupted context. */
    .globl gc_alarm
gc_alarm:
    stwu 1, -0x20(1)
    mflr 0
    stw 0, 0x24(1)
    mr 5, 4
    mr 4, 3
    bl 3f
3:  mflr 3
    addi 3, 3, gc_context_data - 3b
    bl gc_on_alarm
    lwz 0, 0x24(1)
    mtlr 0
    addi 1, 1, 0x20
    blr

/* An exception before the game's OS has its handlers. The loader's stub
 * at the vector has put r3, SRR0, SRR1 and the vector number in SPRG0-3
 * and come here with translation back on. The registers go into an OSContext-shaped block on the stack
 * (GPRs at 0, CR, LR, CTR and XER at 0x80, SRR0/SRR1 at 0x198) for gc_on_fatal, which
 * never returns. */
    .globl gc_dsi_entry
gc_dsi_entry:
    stwu 1, -0x1C0(1)
    stw 0, 8(1)
    stmw 2, 16(1)
    mfcr 0
    stw 0, 8 + 0x80(1)
    mfctr 0
    stw 0, 8 + 0x88(1)
    mfxer 0
    stw 0, 8 + 0x8C(1)
    mfsprg 0, 0
    stw 0, 20(1)
    addi 0, 1, 0x1C0
    stw 0, 12(1)
    mflr 0
    stw 0, 8 + 0x84(1)
    mfsprg 0, 1
    stw 0, 8 + 0x198(1)
    mfsprg 0, 2
    stw 0, 8 + 0x19C(1)
    mfsprg 4, 3
    addi 4, 4, 0x10
    addi 5, 1, 8
    mfdsisr 6
    mfdar 7
    bl 4f
4:  mflr 3
    addi 3, 3, gc_context_data - 4b
    bl gc_on_fatal
5:  b 5b

/* A jump into RiftWii's own data (gc_rt.h's GC_EXCEPTION_STRAY): the
 * landing words at the start of a block of it (gc_card.h) are "mflr r0",
 * then "bl" here, so r0 holds the LR the jump left and LR points 8 bytes
 * past the landing. The registers go into an OSContext-shaped block on the
 * stack as in gc_dsi_entry, for gc_on_stray(ctx, context, CTR), which never
 * returns. */
    .globl gc_stray_entry
gc_stray_entry:
    stwu 1, -0x1C0(1)
    stmw 2, 16(1)
    stw 0, 8 + 0x84(1)
    mfcr 0
    stw 0, 8 + 0x80(1)
    mfctr 0
    stw 0, 8 + 0x88(1)
    mfxer 0
    stw 0, 8 + 0x8C(1)
    li 0, 0
    stw 0, 8(1)
    addi 0, 1, 0x1C0
    stw 0, 12(1)
    mflr 3
    subi 3, 3, 8
    stw 3, 8 + 0x198(1)
    mfmsr 0
    stw 0, 8 + 0x19C(1)
    mfctr 5
    addi 4, 1, 8
    bl 6f
6:  mflr 3
    addi 3, 3, gc_context_data - 6b
    bl gc_on_stray
7:  b 7b

/* The EXI functions (gc_card.h): gc_on_exi(ctx, which, the game's r3..r7,
 * &pass) answers for the emulated card; when it sets pass, the original
 * runs from its replay slot with the game's arguments.
 * Frame: 8..0x18 r3..r7, 0x1C pass. */
    .macro GC_EXI_HOOK idx
gc_exi_hook_\idx:
    stwu 1, -0x30(1)
    mflr 0
    stw 0, 0x34(1)
    stw 3, 0x08(1)
    stw 4, 0x0C(1)
    stw 5, 0x10(1)
    stw 6, 0x14(1)
    stw 7, 0x18(1)
    li 0, 0
    stw 0, 0x1C(1)
    addi 10, 1, 0x1C
    mr 9, 7
    mr 8, 6
    mr 7, 5
    mr 6, 4
    mr 5, 3
    li 4, \idx
    bl 1f
1:  mflr 3
    addi 3, 3, gc_context_data - 1b
    bl gc_on_exi
    lwz 0, 0x1C(1)
    cmpwi 0, 0
    bne 2f
    lwz 0, 0x34(1)
    mtlr 0
    addi 1, 1, 0x30
    blr
2:  lwz 3, 0x08(1)
    lwz 4, 0x0C(1)
    lwz 5, 0x10(1)
    lwz 6, 0x14(1)
    lwz 7, 0x18(1)
    lwz 0, 0x34(1)
    mtlr 0
    addi 1, 1, 0x30
    b gc_exi_replay_\idx
    .balign 16
gc_exi_replay_\idx:
    nop
    nop
    nop
    nop
gc_exi_continue_\idx:
    lis 12, 0
    ori 12, 12, 0
    mtctr 12
    bctr
    .endm

    GC_EXI_HOOK 0
    GC_EXI_HOOK 1
    GC_EXI_HOOK 2
    GC_EXI_HOOK 3
    GC_EXI_HOOK 4
    GC_EXI_HOOK 5
    GC_EXI_HOOK 6
    GC_EXI_HOOK 7
    GC_EXI_HOOK 8
    GC_EXI_HOOK 9
    GC_EXI_HOOK 10

    .macro GC_EXI_ENTRY idx
    .long gc_exi_hook_\idx - _gc_blob_start, gc_exi_replay_\idx - _gc_blob_start, gc_exi_continue_\idx - _gc_blob_start
    .endm
    .balign 4
gc_exi_table:
    GC_EXI_ENTRY 0
    GC_EXI_ENTRY 1
    GC_EXI_ENTRY 2
    GC_EXI_ENTRY 3
    GC_EXI_ENTRY 4
    GC_EXI_ENTRY 5
    GC_EXI_ENTRY 6
    GC_EXI_ENTRY 7
    GC_EXI_ENTRY 8
    GC_EXI_ENTRY 9
    GC_EXI_ENTRY 10

    .balign 32
    .globl gc_context_data
gc_context_data:
    /* struct gc_rt_context, in GC_RT_CONTEXT_BYTES (gc_layout.h): the magic, then zeros */
    .long 0x52574743
    .fill GC_RT_CONTEXT_BYTES / 4 - 1, 4, 0
