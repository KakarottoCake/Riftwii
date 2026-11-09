/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A GameCube controller's calibration as the in-game code applies it
 * (Settings > Controllers > Calibrate a GameCube controller): each stick
 * stretched per direction so a worn stick that stops short of its gate
 * still reaches a healthy one's full tilt, a dead zone for a stick that
 * drifts, and each trigger stretched to a healthy one's full travel.
 *
 * The menu works the stretch out once (riftwii/padcal.hpp: the division
 * is done there), so a game pays a compare, a multiply and a shift per
 * axis: no division, no table, no constant data. One header for the pad
 * hook (runtime/pad, runtime/rtgcad.c), RiftWii's GameCube loader
 * (runtime/gc) and the host tests. Every value works on a PADStatus's six
 * analog bytes as the game reads them: stick x, y, C stick x, y (signed,
 * from the origin), then L and R (from the origin, 0 at rest). */
#ifndef RIFTWII_RTPADCAL_H
#define RIFTWII_RTPADCAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1.0 in a direction's scale (8 bits of fraction). */
#define RTPADCAL_ONE 256u
/* The calibrations a launch carries for USB pads, told apart by their IDs. */
#define RTPADCAL_USB_PADS 4u
#define RTPADCAL_PORTS 4u

/* One controller. `used` 0 is a controller with no calibration: its
 * values pass as they are. Directions, in this order for each stick:
 * right, left, up, down; `scale` and `limit` hold the main stick's four,
 * then the C stick's. */
typedef struct rtpadcal_pad {
    uint16_t scale[8];          /* times RTPADCAL_ONE: what a value past the dead zone is multiplied by */
    uint16_t trigger_scale[2];  /* L, R */
    uint8_t limit[8];           /* the most the direction may give (127 right and up, 128 left and down unstretched) */
    uint8_t dead[2];            /* main stick, C stick: this far from the centre or less reads as the centre */
    uint8_t trigger_limit[2];   /* L, R: the most a trigger may give */
    uint8_t used;
    uint8_t reserved[3];
} rtpadcal_pad;

/* Every calibration a launch carries, by where the controller plays from:
 * the Wii's own GameCube ports, the adapter's ports, and USB pads by their
 * VID << 16 | PID (0: none). Loader-placed; a game reads it only. */
typedef struct rtpadcal_table {
    rtpadcal_pad console[RTPADCAL_PORTS];
    rtpadcal_pad adapter[RTPADCAL_PORTS];
    uint32_t usb_id[RTPADCAL_USB_PADS];
    rtpadcal_pad usb[RTPADCAL_USB_PADS];
} rtpadcal_table;

/* One stick axis: its byte as a signed value, its dead zone, then the
 * positive direction's scale and limit and the negative's. */
static inline uint8_t rtpadcal_axis(uint8_t byte, uint32_t dead, const uint16_t* scale, const uint8_t* limit) {
    const int32_t value = (int8_t)byte;
    const uint32_t negative = value < 0 ? 1u : 0u;
    uint32_t size = negative ? (uint32_t)-value : (uint32_t)value;
    if (size <= dead) return 0;
    size = ((size - dead) * scale[negative] + RTPADCAL_ONE / 2u) >> 8;
    if (size > limit[negative]) size = limit[negative];
    return (uint8_t)(negative ? -(int32_t)size : (int32_t)size);
}

static inline uint8_t rtpadcal_trigger(uint8_t value, uint32_t scale, uint32_t limit) {
    uint32_t size = ((uint32_t)value * scale + RTPADCAL_ONE / 2u) >> 8;
    if (size > limit) size = limit;
    return (uint8_t)size;
}

/* A PADStatus's analog bytes (`axes`: stick x, y, C x, y, L, R), as the
 * calibration `c` wants them. Nothing changes when it has none. */
static inline void rtpadcal_apply(const rtpadcal_pad* c, uint8_t* axes) {
    uint32_t axis;
    if (c == 0 || !c->used) return;
    for (axis = 0; axis < 4u; ++axis)
        axes[axis] = rtpadcal_axis(axes[axis], c->dead[axis >> 1], &c->scale[axis * 2u], &c->limit[axis * 2u]);
    axes[4] = rtpadcal_trigger(axes[4], c->trigger_scale[0], c->trigger_limit[0]);
    axes[5] = rtpadcal_trigger(axes[5], c->trigger_scale[1], c->trigger_limit[1]);
}

/* The calibration of a USB pad by its VID << 16 | PID, or none. */
static inline const rtpadcal_pad* rtpadcal_usb(const rtpadcal_table* t, uint32_t vid_pid) {
    uint32_t i;
    for (i = 0; i < RTPADCAL_USB_PADS; ++i)
        if (t->usb_id[i] != 0 && t->usb_id[i] == vid_pid) return &t->usb[i];
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif
