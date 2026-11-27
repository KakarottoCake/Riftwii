/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Delivery, the last of the stand-in's four layers (sources, shapes, slot
 * ownership, delivery; notes in #587): what the game asked for, in its
 * library's own layout. "Newly pressed" and "let go" belong here, against
 * what the game's KPAD read last got. Packs never reach this layer. Free of
 * the hardware, so the host tests run it. */
#ifndef RIFTWII_RTDELIVER_H
#define RIFTWII_RTDELIVER_H

#include <stdint.h>

#define RTDELIVER_SLOTS 4u

/* What the game's KPAD read last got on each slot. Newly pressed and let go
 * are against that, not the sample before: a sample taken at a WPADProbe
 * the game never reads in between still counts its press. Worked out once
 * per sample, so a second read in a frame sees the same words. */
struct rtdeliver {
    uint32_t worked[RTDELIVER_SLOTS];     /* the sample the words below are from */
    uint32_t delivered[RTDELIVER_SLOTS];  /* the buttons the game last got */
    uint32_t trig[RTDELIVER_SLOTS], release[RTDELIVER_SLOTS];
};

/* A controller arrived on `slot` holding `hold`: what it holds already
 * isn't newly pressed. */
void rtdeliver_arrived(struct rtdeliver* d, uint32_t slot, uint32_t hold);

/* `slot`'s controller left: a later one starts from nothing held. */
void rtdeliver_gone(struct rtdeliver* d, uint32_t slot);

/* The game's KPAD read takes `slot`'s words for sample number `sample`,
 * whose buttons are `hold`: newly pressed and let go since it last got
 * them, the same for every read until the next sample. */
void rtdeliver_take(struct rtdeliver* d, uint32_t slot, uint32_t sample, uint32_t hold, uint32_t* trig,
                    uint32_t* release);

/* A numerator over RTHOME_AXIS_FULL as a float's bits, as rthome_float_bits
 * but with no cap at 1: the Wii Remote's shake swings past 3 g, and the
 * pointer's distance at rest is about 1.95 (the form's KPAD status). */
uint32_t rtdeliver_float_bits(int32_t numerator);

#endif
