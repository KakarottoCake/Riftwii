/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What a game's SIProbe(chan) hears for a port RiftWii plays a controller
 * on, shared by the GameCube loader (runtime/gc, gc_ports_probe) and the
 * Wii games' pad blob (runtime/pad, #510). Some games ask SIProbe which
 * device is in a port and only read a port's PADStatus when it is a
 * controller: Mario Kart: Double Dash!!'s JUTGamePad::read takes a port
 * only when SIProbe says a controller or a WaveBird. A Wii U has no
 * GameCube ports, and a Wii's empty port has nothing in it either, so
 * SIProbe answers "no response" there and a port the adapter or a Wii
 * Remote fills is skipped although its PADStatus has err 0. Kept free of
 * the hardware so the host tests can check it. */
#ifndef RIFTWII_RTSIPROBE_H
#define RIFTWII_RTSIPROBE_H

#include <stdint.h>

/* SIProbe's answer for a GameCube controller (the SDK's SI_GC_CONTROLLER). */
#define RT_SI_GC_CONTROLLER 0x09000000u

/* The SDK's own answer `probed`, or a controller's when port `chan` is in
 * `played` (bit p: RiftWii filled port p) and nothing answers there: any
 * device the SDK identified (a controller plugged in later, a GBA, a
 * keyboard) keeps its own answer. The low byte of an answer holds
 * SIDecodeType's errors (no response, busy, unknown); a device's type
 * has it 0. */
static inline uint32_t rt_si_probe(uint32_t chan, uint32_t probed, uint32_t played) {
    const int nothing_answers = (probed & 0xFFu) != 0;
    if (chan >= 4u || (played & (1u << chan)) == 0 || !nothing_answers) return probed;
    return RT_SI_GC_CONTROLLER;
}

#endif
