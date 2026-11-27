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
