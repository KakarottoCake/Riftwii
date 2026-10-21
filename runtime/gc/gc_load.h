/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_GC_LOAD_H
#define RIFTWII_GC_LOAD_H

#include <stdint.h>

#define GC_LOAD_ROOT_HEAPS 4u

/* A callback's allocations may outlive its read buffer. Keep the game's
 * choice on its main heaps; only interrupt a temporary heap with the
 * queued heap. Zero means no change, including an untracked request. */
static inline uint32_t gc_load_callback_heap(uint32_t current, uint32_t queued,
                                             const uint32_t roots[GC_LOAD_ROOT_HEAPS]) {
    uint32_t i;
    if (current == 0 || queued == 0 || current == queued) return 0;
    for (i = 0; i < GC_LOAD_ROOT_HEAPS; ++i) {
        if (current == roots[i]) return 0;
    }
    return queued;
}

#endif
