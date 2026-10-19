/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GameCube controller adapter for Wii U in a GameCube game: the same
 * driver Wii games and the menu run (runtime/rtgcad.c), here through the
 * blob's own polled IPC, as the Wii Remotes' Bluetooth is (gc_bt.c).
 *
 * A GameCube game runs on the menu's IOS (no reload), so /dev/usb/hid is
 * the one the menu reached the adapter through. The loader opens it before
 * the hand-over, as a Wii game's launch does (wii/hooks/padhook.cpp), and
 * passes the handle, the HID version and the adapter's device id from the
 * menu's USB list: v5 hands its list out once per change, to whoever asks
 * first, and the menu had it.
 *
 * The ports: a GameCube controller in the console's own port keeps it;
 * each adapter port's controller plays on the port of its own number when
 * that is free, else on the next free one (gc_ports.h's rule); the Wii
 * Remotes take what is left (gc_bt_fill runs after this). */
#ifndef RIFTWII_GC_GCAD_H
#define RIFTWII_GC_GCAD_H

#include <stdint.h>

#include "gc_ports.h"
#include "rtgcad.h"

/* One request slot per driver tag (rtgcad.h's GCAD_TAG_*). */
struct gc_gcad_rq {
    uint32_t ipc[16];
    uint32_t vec[8];
    uint32_t state;     /* GC_GCAD_RQ_* */
    uint32_t tag;       /* what the driver handed the environment */
    int32_t result;
    uint32_t out;       /* the buffer IOS writes into, 0: none */
    uint32_t out_len;
    uint32_t pad[3];
};
#define GC_GCAD_RQ_IDLE 0u
#define GC_GCAD_RQ_SENT 1u
#define GC_GCAD_RQ_DONE 2u

/* Loader-placed in MEM2 (every /dev/usb buffer must be there), zeroed,
 * with fd, version, known_dev, ticks_per_ms and rumble filled. */
struct gc_gcad {
    gcad g;  /* first: its buffers, 32-byte aligned */
    struct gc_gcad_rq rq[GCAD_TAGS] __attribute__((aligned(32)));
    int32_t fd;
    uint32_t version;       /* 4 or 5 */
    int32_t known_dev;      /* -1: none */
    uint32_t ticks_per_ms;
    uint32_t rumble;        /* 1: the game's rumble reaches the adapter's controllers */
    uint32_t inited;
    uint32_t ctx;           /* struct gc_rt_context, for sending (set at each poll) */
    /* The ports as gc_gcad_fill last left them: each adapter port's game
     * port (GC_PORT_NONE: none), and the game ports filled. */
    uint8_t port_of[GCAD_PORTS];
    uint32_t filled;
    /* For the trace. */
    uint32_t polls;
    uint32_t sends;
    uint32_t send_failures;
    uint32_t replies;
};

#ifdef __cplusplus
extern "C" {
#endif

/* The adapter's controllers into the game's PADStatus[4] (`status`), on
 * the ports gc_gcad.h's rule gives them. Before gc_bt_fill. Built into the
 * host tests too (tests/gcgcad_tests.cpp). */
void gc_gcad_fill(struct gc_gcad* ad, uint8_t* status);
/* PADControlMotor(chan, command): the adapter controller playing on that
 * port rumbles (1) or stops (0, 2), when its rumble is on. */
void gc_gcad_motor(struct gc_gcad* ad, uint32_t chan, uint32_t command);

#ifdef RT_TARGET_PPC
struct gc_rt_context;
/* Once a frame, from PADRead: replies handed to the driver, then its tick. */
void gc_gcad_poll(struct gc_rt_context* ctx, struct gc_gcad* ad);
/* A reply from IOS: 1 when it is one of the adapter's, marked done for the
 * next poll. */
int gc_gcad_on_reply(struct gc_gcad* ad, uint32_t reply);
#else
/* The host tests' clock, in the driver's ticks (the Wii's is the timebase). */
uint32_t gc_gcad_host_now(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
