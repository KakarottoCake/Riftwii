/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube blob's sizes that its assembly (gc_entry.S) shares with the
 * C side (gc_rt.h): defines only, so both can include it. */
#ifndef RIFTWII_GC_LAYOUT_H
#define RIFTWII_GC_LAYOUT_H

/* The room gc_entry.S keeps for struct gc_rt_context: the magic, then
 * zeros. Fixed, with spare bytes at the end, so a field added to the
 * struct changes neither this nor the assembly; gc_rt.h checks that the
 * struct still fits. Raise it (a multiple of 32) only when it does not. */
#define GC_RT_CONTEXT_BYTES 0x600

#endif
