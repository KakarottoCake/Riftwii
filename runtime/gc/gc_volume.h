/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Volume boost for quiet GameCube games (a game's Volume boost option).
 *
 * The audio interface's volume register (0xCC006C04) scales only the
 * disc's streamed music; a game's own sound reaches the AI as 16-bit
 * stereo PCM in main memory, mixed by the DSP (or the CPU) and handed over
 * with AIInitDMA. So the boost scales that buffer in AIInitDMA's hook
 * (gc_on_ai_dma), before the AI reads it: each sample times a gain in
 * 1/256ths, clamped to the 16-bit range so a loud sample flattens instead
 * of wrapping round to the other sign.
 *
 * Kept apart from gc_rt.c, with no blob state, so the host tests
 * (tests/gcvolume_tests.cpp) check exactly what the game runs. */
#ifndef RIFTWII_GC_VOLUME_H
#define RIFTWII_GC_VOLUME_H

#include <stdint.h>

/* The gain that leaves the sound as it is. */
#define GC_VOLUME_UNITY 256u
/* +3 dB and +6 dB in 1/256ths: 10^(3/20) = 1.413 and 10^(6/20) = 1.995. */
#define GC_VOLUME_PLUS_3DB 362u
#define GC_VOLUME_PLUS_6DB 511u

/* The gain for a boost in decibels as the option names it (0, 3 or 6);
 * any other value leaves the sound as it is. */
static inline uint32_t gc_volume_gain(uint32_t decibels) {
    if (decibels == 3u) return GC_VOLUME_PLUS_3DB;
    if (decibels == 6u) return GC_VOLUME_PLUS_6DB;
    return GC_VOLUME_UNITY;
}

/* Scales `count` 16-bit samples (the AI's format: both channels
 * interleaved, big-endian as the CPU reads them) by gain / 256, clamped
 * to -32768..32767. */
static inline void gc_volume_scale(int16_t* samples, uint32_t count, uint32_t gain) {
    uint32_t i;
    for (i = 0; i < count; ++i) {
        int32_t scaled = ((int32_t)samples[i] * (int32_t)gain) >> 8;
        if (scaled > 32767) scaled = 32767;
        if (scaled < -32768) scaled = -32768;
        samples[i] = (int16_t)scaled;
    }
}

#endif
