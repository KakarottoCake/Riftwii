/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Each player's controller, one sample per report (rtsrc.h). */
#ifndef RT_PAD_STANDIN
#define RT_PAD_STANDIN
#endif
#include "rtsrc.h"

#include "rthome.h"


/* Ticks since a time noted with its low bit set (so 0 can mean "none"). */
static uint32_t rtsrc_elapsed(uint32_t now, uint32_t noted) { return now - (noted & ~1u); }

enum rtsrc_event rtsrc_port(struct rtsrc* s, uint32_t port, const uint8_t* status, uint32_t grace, uint32_t now) {
    uint32_t bit, k;
    if (port >= RTSRC_PORTS) return RTSRC_NOTHING;
    bit = 1u << port;
    if ((int8_t)status[RTSRC_PAD_ERR] == 0) {
        const int arrived = !(s->present & bit);
        for (k = 0; k < RTSRC_PAD_BYTES; ++k) s->pad[port][k] = status[k];
        s->seen[port] = now | 1u;
        ++s->samples[port];
        s->present |= bit;
        return arrived ? RTSRC_ARRIVED : RTSRC_REPORT;
    }
    if (!(s->present & bit)) return RTSRC_NOTHING;
    if (rtsrc_elapsed(now, s->seen[port]) >= grace) {
        s->present &= ~bit;
        s->seen[port] = 0;
        return RTSRC_GONE;
    }
    /* Within the grace period: the buttons stay as held, but it is a new
     * sample, so a press already given to the game isn't given again at
     * each read until a report comes. */
    ++s->samples[port];
    return RTSRC_REPEAT;
}

void rtsrc_decode(const uint8_t* status, struct rtsrc_sample* out) {
    out->buttons = ((uint32_t)status[0] << 8) | status[1];
    rthome_classic_axes(status, out->axes);
    out->trigger[0] = status[6];
    out->trigger[1] = status[7];
}

void rtsrc_latest(const struct rtsrc* s, uint32_t port, struct rtsrc_sample* out) {
    uint32_t k;
    if (port < RTSRC_PORTS && ((s->present >> port) & 1u)) {
        rtsrc_decode(s->pad[port], out);
        return;
    }
    /* No such port, or nothing in it (unplugged past the grace period):
     * nothing pressed, everything centred (no table: the blob has no data
     * section of its own to keep one in; CodeRabbit CLI). */
    out->buttons = 0;
    for (k = 0; k < 6; ++k) out->axes[k] = 0;
    out->trigger[0] = out->trigger[1] = 0;
}
