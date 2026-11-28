/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GameCube controllers as Wii controllers: the layers joined (rtgcwii.h). */
/* The Wii controller stand-ins' own file: linked only into the pad builds
 * with them (RT_PAD_STANDIN, Makefile.runtime), and compiled, unused, with
 * the DOL's and the host library's runtime sources; it takes rthome.h's
 * stand-in declarations in each. */
#ifndef RT_PAD_STANDIN
#define RT_PAD_STANDIN
#endif
#include "rtgcwii.h"

#include "rthome.h"

/* Ticks since a time noted with its low bit set (so 0 can mean "none"). */
static uint32_t rtgcwii_elapsed(uint32_t now, uint32_t noted) { return now - (noted & ~1u); }

void rtgcwii_pads(struct rtgcwii* w, const uint8_t* status, uint32_t now) {
    uint32_t i;
    for (i = 0; i < RTGCWII_CHANNELS; ++i) {
        const uint32_t bit = 1u << i;
        const enum rtsrc_event event = rtsrc_port(&w->src, i, status + i * RTGCWII_PAD_BYTES, w->slot.grace, now);
        if (event == RTSRC_ARRIVED || event == RTSRC_REPORT) {
            struct rtsrc_sample p;
            rtsrc_latest(&w->src, i, &p);
            w->hold[i] = rtshape_classic_buttons(&p);
            /* Newly plugged in: what it holds already isn't newly pressed. */
            if (event == RTSRC_ARRIVED) rtdeliver_arrived(&w->deliver, i, w->hold[i]);
            w->slot.present |= bit;
        } else if (event == RTSRC_GONE) {
            w->slot.present &= ~bit;
            w->hold[i] = 0;
            rtdeliver_gone(&w->deliver, i);
        }
    }
}

int rtgcwii_should_read(struct rtgcwii* w, uint32_t now, uint32_t ticks_per_ms) {
    if (w->reading) return 0;
    if (w->game_read != 0) {
        if (rtgcwii_elapsed(now, w->game_read) <= 50u * ticks_per_ms) return 0;
        w->game_read = 0;
    }
    if (w->own_read != 0 && rtgcwii_elapsed(now, w->own_read) < 8u * ticks_per_ms) return 0;
    w->own_read = now | 1u;
    return 1;
}

void rtgcwii_read_by(struct rtgcwii* w, uint32_t now, int own) {
    if (!own) w->game_read = now | 1u;
}

void rtgcwii_classic(struct rtgcwii* w, uint32_t channel, uint32_t* hold, uint32_t* trig, uint32_t* release,
                     int32_t axes[6]) {
    uint32_t i;
    if (!rtgcwii_on(w, channel)) {
        *hold = *trig = *release = 0;
        for (i = 0; i < 6; ++i) axes[i] = 0;
        return;
    }
    *hold = w->hold[channel];
    rtdeliver_take(&w->deliver, channel, w->src.samples[channel], *hold, trig, release);
    {
        struct rtsrc_sample p;
        rtsrc_latest(&w->src, channel, &p);
        for (i = 0; i < 6; ++i) axes[i] = p.axes[i];
    }
}

uint32_t rtgcwii_hold(const struct rtgcwii* w, uint32_t channel) {
    return rtgcwii_on(w, channel) ? w->hold[channel] : 0u;
}
