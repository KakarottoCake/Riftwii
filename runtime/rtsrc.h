/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Sources, the first of the stand-in's four layers (sources, shapes, slot
 * ownership, delivery; notes in #587): what each player's controller is
 * doing, one sample per report, whatever the controller is. Today the
 * GameCube controllers' PADStatus (the adapter's and the Wii's own ports,
 * merged before the game's PADRead returns); a USB pad or a real Classic
 * Controller later fills the same sample. The shapes (rtshape) read only
 * samples, never a controller's own report. Free of the hardware, so the
 * host tests run it. */
#ifndef RIFTWII_RTSRC_H
#define RIFTWII_RTSRC_H

#include <stdint.h>

#define RTSRC_PORTS 4u
#define RTSRC_PAD_BYTES 12u  /* a PADStatus */

/* A sample's buttons, in the GameCube controller's own bits (PADStatus'
 * word); a source with other buttons maps them onto these. */
#define RTSRC_LEFT 0x0001u
#define RTSRC_RIGHT 0x0002u
#define RTSRC_DOWN 0x0004u
#define RTSRC_UP 0x0008u
#define RTSRC_Z 0x0010u
#define RTSRC_R 0x0020u
#define RTSRC_L 0x0040u
#define RTSRC_A 0x0100u
#define RTSRC_B 0x0200u
#define RTSRC_X 0x0400u
#define RTSRC_Y 0x0800u
#define RTSRC_START 0x1000u

/* One controller at one moment. */
struct rtsrc_sample {
    uint32_t buttons;    /* RTSRC_* bits */
    /* The control stick (0, 1), the C-stick (2, 3) and the analog L and R
     * (4, 5), numerators over RTHOME_AXIS_FULL, centred and dead-zoned as
     * rthome_classic_axes has them. */
    int32_t axes[6];
    uint8_t trigger[2];  /* the analog L and R as the controller gave them, 0..255 */
};

/* Each port's controller as the last report had it. */
struct rtsrc {
    uint32_t present;                             /* ports with a controller, one bit each */
    uint32_t seen[RTSRC_PORTS];                   /* when each last reported (low bit set), 0: gone */
    uint32_t samples[RTSRC_PORTS];                /* samples taken (a report, or a dropped one within grace) */
    uint8_t pad[RTSRC_PORTS][RTSRC_PAD_BYTES];    /* each port's PADStatus as its last report had it */
};

/* What a port's report meant. */
enum rtsrc_event {
    RTSRC_NOTHING,  /* no controller, as before */
    RTSRC_ARRIVED,  /* a report from a port that had no controller: plugged in */
    RTSRC_REPORT,   /* a report from a controller that was there */
    RTSRC_REPEAT,   /* no report, within `grace` of the last: the same sample again (a wireless pad's dropped report) */
    RTSRC_GONE      /* no report for `grace` ticks: unplugged */
};

/* The game's PADRead gave `status` (one PADStatus) for `port` at `now`. A
 * short gap (a wireless pad's dropped report) never unplugs the player. */
enum rtsrc_event rtsrc_port(struct rtsrc* s, uint32_t port, const uint8_t* status, uint32_t grace, uint32_t now);

/* A PADStatus as a sample. */
void rtsrc_decode(const uint8_t* status, struct rtsrc_sample* out);

/* `port`'s latest sample: its last report while a controller is there (a
 * dropped report within grace included); all zero for a port with nothing. */
void rtsrc_latest(const struct rtsrc* s, uint32_t port, struct rtsrc_sample* out);

#endif
