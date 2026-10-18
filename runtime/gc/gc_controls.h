/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Which GameCube buttons a Wii Remote's buttons press in a GameCube game
 * (gc_bt.c): one table per way of holding it, filled by the loader from
 * the built-in map and the player's files (src/data/gccontrols.cpp,
 * sd:/riftwii/gc/controls/), and read by the blob at every PADRead. Kept
 * apart from the Bluetooth driver so the host tests can check it.
 *
 * The analogue parts are not in the table: the Nunchuk's stick stays the
 * control stick, and the Classic Controller's sticks and triggers stay the
 * GameCube's sticks and triggers. A button can also push a stick all the
 * way (the D-pad of a Wii Remote held sideways is the control stick). */
#ifndef RIFTWII_GC_CONTROLS_H
#define RIFTWII_GC_CONTROLS_H

#include <stdint.h>

/* The ways of holding it, each with its own table. */
#define GC_MAP_REMOTE 0u   /* a Wii Remote alone, held sideways (D-pad on the left) */
#define GC_MAP_NUNCHUK 1u  /* a Wii Remote upright with a Nunchuk */
#define GC_MAP_CLASSIC 2u  /* a Classic Controller or Classic Controller Pro */
#define GC_MAP_KINDS 3u

/* The inputs, a bit each in the word gc_map_targets takes. The D-pad's
 * directions are as the player holds the controller: for a Wii Remote held
 * sideways, Up is the arrow that points up then (the remote's own Right).
 * Which inputs each kind has is the parser's business: the blob only ever
 * sets the ones a controller of that kind has. */
#define GC_IN_UP 0u
#define GC_IN_DOWN 1u
#define GC_IN_LEFT 2u
#define GC_IN_RIGHT 3u
#define GC_IN_A 4u
#define GC_IN_B 5u
#define GC_IN_1 6u
#define GC_IN_2 7u
#define GC_IN_MINUS 8u
#define GC_IN_PLUS 9u
#define GC_IN_HOME 10u
#define GC_IN_C 11u      /* the Nunchuk's */
#define GC_IN_Z 12u
#define GC_IN_X 13u      /* the Classic Controller's */
#define GC_IN_Y 14u
#define GC_IN_L 15u
#define GC_IN_R 16u
#define GC_IN_ZL 17u
#define GC_IN_ZR 18u
#define GC_INPUTS 19u

/* What an input presses: the GameCube buttons by their own bits in
 * PADStatus's first half-word, and the sticks pushed all the way. */
#define GC_PAD_LEFT 0x0001u
#define GC_PAD_RIGHT 0x0002u
#define GC_PAD_DOWN 0x0004u
#define GC_PAD_UP 0x0008u
#define GC_PAD_Z 0x0010u
#define GC_PAD_R 0x0020u
#define GC_PAD_L 0x0040u
#define GC_PAD_A 0x0100u
#define GC_PAD_B 0x0200u
#define GC_PAD_X 0x0400u
#define GC_PAD_Y 0x0800u
#define GC_PAD_START 0x1000u
#define GC_PAD_BUTTONS 0x1F7Fu
#define GC_OUT_STICK_LEFT 0x00010000u
#define GC_OUT_STICK_RIGHT 0x00020000u
#define GC_OUT_STICK_UP 0x00040000u
#define GC_OUT_STICK_DOWN 0x00080000u
#define GC_OUT_CSTICK_LEFT 0x00100000u
#define GC_OUT_CSTICK_RIGHT 0x00200000u
#define GC_OUT_CSTICK_UP 0x00400000u
#define GC_OUT_CSTICK_DOWN 0x00800000u
#define GC_STICK_FULL 100

struct gc_map {
    uint32_t to[GC_MAP_KINDS][GC_INPUTS];  /* each input's buttons and stick pushes, 0: nothing */
};

/* Everything the held inputs (`held`: bit GC_IN_* set for each) press. */
static inline uint32_t gc_map_targets(const struct gc_map* map, uint32_t kind, uint32_t held) {
    uint32_t targets = 0;
    uint32_t input;
    if (kind >= GC_MAP_KINDS) return 0;
    for (input = 0; input < GC_INPUTS; ++input)
        if (held & (1u << input)) targets |= map->to[kind][input];
    return targets;
}

/* The stick pushes in `targets` over the sticks' own positions: an axis a
 * button pushes is all the way that way, the others are left. With both
 * ways of one axis held, right and down win, as they always have. */
static inline void gc_map_sticks(uint32_t targets, int32_t* sx, int32_t* sy, int32_t* cx, int32_t* cy) {
    if (targets & GC_OUT_STICK_LEFT) *sx = -GC_STICK_FULL;
    if (targets & GC_OUT_STICK_RIGHT) *sx = GC_STICK_FULL;
    if (targets & GC_OUT_STICK_UP) *sy = GC_STICK_FULL;
    if (targets & GC_OUT_STICK_DOWN) *sy = -GC_STICK_FULL;
    if (targets & GC_OUT_CSTICK_LEFT) *cx = -GC_STICK_FULL;
    if (targets & GC_OUT_CSTICK_RIGHT) *cx = GC_STICK_FULL;
    if (targets & GC_OUT_CSTICK_UP) *cy = GC_STICK_FULL;
    if (targets & GC_OUT_CSTICK_DOWN) *cy = -GC_STICK_FULL;
}

#endif
