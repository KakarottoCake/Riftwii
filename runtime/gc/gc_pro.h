/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A Wii U Pro Controller as a GameCube controller (gc_bt.c). Kept apart
 * from the Bluetooth driver so the host tests can check the decoding.
 *
 * The Pro speaks the Wii Remote's protocol as one device: it has no Wii
 * Remote buttons of its own and no accelerometer, and reports as an
 * extension whose ID (0xA400FA) ends 01 20. Its buttons and sticks come
 * only in eleven extension bytes, so it is asked for report mode 0x34
 * (the two core button bytes, then 19 extension bytes), the one mode with
 * room for all of them:
 *   bytes 0-1  left stick X    (12 bits, little-endian, about 0x800 at rest)
 *   bytes 2-3  right stick X
 *   bytes 4-5  left stick Y    (larger is up)
 *   bytes 6-7  right stick Y
 *   bytes 8-9  the buttons, active low, in a Classic Controller's own bit
 *              places (its fifth and sixth bytes); L and R are buttons
 *              only, with no analogue trigger
 *   byte 10    the stick clicks and the battery, not used here
 * It is played through the Classic Controller's table of the map
 * (gc_controls.h's GC_MAP_CLASSIC, a controls file's [classic]), since its
 * buttons are a Classic Controller Pro's.
 *
 * Facts from wiibrew's "Wii U Pro Controller" page and RiftWii's own Pro
 * Controller translator for Wii games (runtime/rtpro.c); no code taken. */
#ifndef RIFTWII_GC_PRO_H
#define RIFTWII_GC_PRO_H

#include <stdint.h>

#include "gc_controls.h"

#define GC_PRO_BYTES 11u
#define GC_PRO_MODE 0x34u     /* the report mode a Pro is asked for */
#define GC_PRO_EXT_OFFSET 4u  /* where a 0x34 report's extension bytes start, after 0xA1, the ID and the core bytes */
/* A stick at rest, and how far from it a full push is (they reach a
 * little further, which the clamp below absorbs). */
#define GC_PRO_STICK_CENTRE 2048
#define GC_PRO_STICK_REACH 896

/* The extension ID's last two bytes (0xA400FE, 0xA400FF): 1 for a Wii U
 * Pro Controller's. */
static inline int gc_pro_is_id(uint32_t id_high, uint32_t id_low) { return id_high == 0x01u && id_low == 0x20u; }

/* The bytes a Pro is taken to have before its first data report: both
 * sticks at rest, every button up. Read as nothing held. */
static inline void gc_pro_rest(uint8_t* pro) {
    uint32_t axis;
    for (axis = 0; axis < 4; ++axis) {
        pro[2 * axis] = (uint8_t)(GC_PRO_STICK_CENTRE & 0xFF);
        pro[2 * axis + 1] = (uint8_t)(GC_PRO_STICK_CENTRE >> 8);
    }
    pro[8] = 0xFF;
    pro[9] = 0xFF;
    pro[10] = 0xFF;
}

/* One 12-bit stick axis as a GameCube stick's: 0 at rest, a full push
 * GC_STICK_FULL as the Wii Remotes' other sticks give, at most 127 either
 * way. */
static inline int32_t gc_pro_axis(const uint8_t* low_high) {
    const int32_t raw = (int32_t)low_high[0] | ((int32_t)(low_high[1] & 0x0Fu) << 8);
    const int32_t value = (raw - GC_PRO_STICK_CENTRE) * GC_STICK_FULL / GC_PRO_STICK_REACH;
    if (value < -127) return -127;
    if (value > 127) return 127;
    return value;
}

/* A Pro Controller's state as the GameCube side wants it: the inputs held
 * (gc_controls.h's GC_IN_* bits) and both sticks. */
struct gc_pro_input {
    uint32_t held;
    int32_t stick_x, stick_y;
    int32_t cstick_x, cstick_y;
};

static inline void gc_pro_decode(const uint8_t* pro, struct gc_pro_input* out) {
    out->held = gc_classic_held(pro[8], pro[9]);
    out->stick_x = gc_pro_axis(pro + 0);
    out->cstick_x = gc_pro_axis(pro + 2);
    out->stick_y = gc_pro_axis(pro + 4);
    out->cstick_y = gc_pro_axis(pro + 6);
}

/* 1 while the Pro's HOME is held (its ninth byte's 0x08, active low). */
static inline int gc_pro_home_held(const uint8_t* pro) { return (pro[8] & 0x08u) == 0; }

#endif
