/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What the game asked for, in its library's layout (rtdeliver.h). */
#include "rtdeliver.h"

void rtdeliver_arrived(struct rtdeliver* d, uint32_t slot, uint32_t hold) {
    if (slot < RTDELIVER_SLOTS) d->delivered[slot] = hold;
}

void rtdeliver_gone(struct rtdeliver* d, uint32_t slot) {
    if (slot < RTDELIVER_SLOTS) d->delivered[slot] = 0;
}

void rtdeliver_take(struct rtdeliver* d, uint32_t slot, uint32_t sample, uint32_t hold, uint32_t* trig,
                    uint32_t* release) {
    if (slot >= RTDELIVER_SLOTS) {
        *trig = *release = 0;
        return;
    }
    if (d->worked[slot] != sample) {
        d->worked[slot] = sample;
        d->trig[slot] = hold & ~d->delivered[slot];
        d->release[slot] = d->delivered[slot] & ~hold;
        d->delivered[slot] = hold;
    }
    *trig = d->trig[slot];
    *release = d->release[slot];
}

uint32_t rtdeliver_float_bits(int32_t numerator) {
    uint32_t sign = 0, magnitude, top = 0;
    if (numerator == 0) return 0;
    /* The size taken unsigned, so the most negative value has one too. */
    magnitude = (uint32_t)numerator;
    if (numerator < 0) {
        sign = 0x80000000u;
        magnitude = 0u - magnitude;
    }
    while (top < 31u && (magnitude >> (top + 1)) != 0) ++top;  /* its highest bit */
    /* magnitude / 256 = 1.mantissa * 2^(top - 8) */
    return sign | ((127u + top - 8u) << 23) |
           ((top <= 23u ? magnitude << (23u - top) : magnitude >> (top - 23u)) & 0x007FFFFFu);
}
