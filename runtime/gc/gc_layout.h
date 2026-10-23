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
 * watch (#192), followed by the asynchronous I/O continuation. */
#define GC_RT_CONTEXT_BYTES 0x740

/* A private MEM2 stack lets an IOS wait return from the alarm without
 * keeping the game's interrupt stack or enabling nested OS callbacks. */
#define GC_IO_STACK_BYTES 0x10000

/* A game whose disc code is libogc's (gc_rt.h, "libogc"): each store that
 * starts a DI command jumps to a slot of GC_DI_SLOT_WORDS words, which
 * makes the store, calls gc_di_common and jumps back. GC_DI_SITES slots. */
#define GC_DI_SITES 48
#define GC_DI_SLOT_WORDS 11

#endif
