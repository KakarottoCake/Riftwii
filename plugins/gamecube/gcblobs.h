/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RIFTWII_GCBLOBS_H
#define RIFTWII_GCBLOBS_H

/* The GameCube runtime blobs (Makefile.runtime's build-runtime/), carried
 * in the gamecube plugin's .rodata (gcblobs.S) instead of the DOL: only
 * this plugin copies them into a game. The names and types are bin2s's,
 * which the DOL used for them before. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t riftwii_gc_bin[];       /* runtime/gc, the build for ISOs */
extern const uint32_t riftwii_gc_bin_size;
extern const uint8_t riftwii_gc_rvz_zst[];   /* the RVZ build, zstd-compressed */
extern const uint32_t riftwii_gc_rvz_zst_size;
extern const uint8_t riftwii_gc_rvz_rel[];   /* its relocations (tools/rtreloc.py) */
extern const uint32_t riftwii_gc_rvz_rel_size;

#ifdef __cplusplus
}
#endif

#endif /* RIFTWII_GCBLOBS_H */
