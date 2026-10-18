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
#define GC_IN_SHAKE 19u  /* the Wii Remote shaken (gc_bt.c's accelerometer) */
#define GC_INPUTS 20u

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

/* A Classic Controller's buttons, its fifth and sixth data bytes (active
 * low), as the inputs above. A Wii U Pro Controller sends its buttons in
 * the same bits (gc_pro.h), so it is read through here too. */
static inline uint32_t gc_classic_held(uint32_t byte4, uint32_t byte5) {
    const uint32_t e4 = ~byte4 & 0xFFu;
    const uint32_t e5 = ~byte5 & 0xFFu;
    uint32_t held = 0;
    if (e5 & 0x01u) held |= 1u << GC_IN_UP;
    if (e4 & 0x40u) held |= 1u << GC_IN_DOWN;
    if (e5 & 0x02u) held |= 1u << GC_IN_LEFT;
    if (e4 & 0x80u) held |= 1u << GC_IN_RIGHT;
    if (e5 & 0x10u) held |= 1u << GC_IN_A;
    if (e5 & 0x40u) held |= 1u << GC_IN_B;
    if (e5 & 0x08u) held |= 1u << GC_IN_X;
    if (e5 & 0x20u) held |= 1u << GC_IN_Y;
    if (e5 & 0x80u) held |= 1u << GC_IN_ZL;
    if (e5 & 0x04u) held |= 1u << GC_IN_ZR;
    if (e4 & 0x20u) held |= 1u << GC_IN_L;
    if (e4 & 0x02u) held |= 1u << GC_IN_R;
    if (e4 & 0x04u) held |= 1u << GC_IN_PLUS;
    if (e4 & 0x10u) held |= 1u << GC_IN_MINUS;
    if (e4 & 0x08u) held |= 1u << GC_IN_HOME;
    return held;
}

/* 1 when a Classic Controller's analogue trigger (GC_IN_L or GC_IN_R)
 * still goes to the game: only while its button is still the GameCube's
 * button of the same name (`pad_button`, GC_PAD_L or GC_PAD_R). Mapped
 * away (R = Y), the trigger would half-press the GameCube's R besides,
 * as Super Mario Sunshine's spray does. */
static inline int gc_map_keeps_trigger(const struct gc_map* map, uint32_t input, uint32_t pad_button) {
    return (map->to[GC_MAP_CLASSIC][input] & pad_button) != 0;
}

/* 1 when a kind's shake presses something: its remotes then report their
 * accelerometer (gc_bt.c's report mode). */
static inline int gc_map_wants_shake(const struct gc_map* map, uint32_t kind) {
    return kind < GC_MAP_KINDS && map->to[kind][GC_IN_SHAKE] != 0;
}

/* A shake, from the accelerometer's three axes (eight bits each, about 26
 * a g, as the remote's reports carry them): a jolt is a change of at least
 * GC_SHAKE_CHANGE, the three axes' changes added, from one report to the
 * next. A remote reports about 100 times a second, so that is roughly a
 * change of 1.5 g in a hundredth of a second: a flick of the wrist, not a
 * tilt, which changes the axes slowly. */
#define GC_SHAKE_CHANGE 40u
/* A jolt holds the button for GC_SHAKE_PRESS PADReads (a tenth of a second
 * at 60 a second, so the game sees it), then lets go for at least
 * GC_SHAKE_REST before the next jolt can press it again: shaking on presses
 * the button again and again, as pressing it would. */
#define GC_SHAKE_PRESS 6u
#define GC_SHAKE_REST 6u

static inline int gc_shake_jolt(const uint8_t* before, const uint8_t* now) {
    uint32_t change = 0;
    uint32_t axis;
    for (axis = 0; axis < 3; ++axis)
        change += before[axis] > now[axis] ? (uint32_t)(before[axis] - now[axis]) : (uint32_t)(now[axis] - before[axis]);
    return change >= GC_SHAKE_CHANGE;
}

/* Once a PADRead: `timer` counts the press and the rest down, `jolted` is
 * the jolt seen since the last PADRead (cleared here). Returns 1 while the
 * shake's button is held. */
static inline int gc_shake_step(uint8_t* timer, uint8_t* jolted) {
    if (*timer != 0) --*timer;
    else if (*jolted) *timer = (uint8_t)(GC_SHAKE_PRESS + GC_SHAKE_REST);
    *jolted = 0;
    return *timer > GC_SHAKE_REST;
}

#endif
