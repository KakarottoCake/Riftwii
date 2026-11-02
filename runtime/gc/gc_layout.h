/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube blob's sizes that its assembly (gc_entry.S) shares with the
 * C side (gc_rt.h): defines only, so both can include it. */
#ifndef RIFTWII_GC_LAYOUT_H
#define RIFTWII_GC_LAYOUT_H

/* The room gc_entry.S keeps for struct gc_rt_context: the magic, then
 * zeros. Fixed, with spare bytes at the end, so a field added to the
 * struct changes neither this nor the assembly; gc_rt.h checks that the
 * struct still fits. The native-controller state (#191) and the TGC
 * watch (#192), followed by the asynchronous I/O continuation, then the
 * programs a game loads itself (gc_programs.h), and the ports a probe
 * finds nothing on (gc_ports_idle_update). */
#define GC_RT_CONTEXT_BYTES 0x780

/* gc_entry.S's gc_program_saved: a program the game loaded itself keeps
 * its registers there while gc_on_program_start runs (gc_hook_program).
 * LR, CR, then r1 to r31 (r12's word unused: it holds the area's address),
 * and the top of the MEM2 stack the C side runs on, loader-written. */
#define GC_PROGRAM_SAVED_LR 0x00
#define GC_PROGRAM_SAVED_CR 0x04
#define GC_PROGRAM_SAVED_GPR 0x08
#define GC_PROGRAM_SAVED_STACK 0x84
#define GC_PROGRAM_SAVED_BYTES 0x88

/* A private MEM2 stack lets an IOS wait return from the alarm without
 * keeping the game's interrupt stack or enabling nested OS callbacks. */
#define GC_IO_STACK_BYTES 0x10000

/* A game whose disc code is libogc's (gc_rt.h, "libogc"): each store that
 * starts a DI command jumps to a slot of GC_DI_SLOT_WORDS words, which
 * makes the store, calls gc_di_common and jumps back. GC_DI_SITES slots. */
#define GC_DI_SITES 48
#define GC_DI_SLOT_WORDS 11

#endif
