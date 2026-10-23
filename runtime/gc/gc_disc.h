/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A two-disc GameCube game's disc change (issue #173): the game asks for
 * its other disc, the blob puts it in.
 *
 * What a game expects of the drive (Dolphin's DVDInterface, which can
 * change discs on its own when a game asks, and the reference loaders'
 * behaviour as PR #227 describes it): to change discs, the SDK stops the
 * motor (DVDLowStopMotor, DI command 0xE3), then waits for the cover to
 * close (it sets the cover interrupt's mask in DICVR, 0xCC006004, bit 1).
 * When the player has swapped the disc and closed the cover, the drive
 * sets DICVR's interrupt bit (bit 2) and raises the DI interrupt; the SDK
 * then reads the new disc's ID, compares it with the one it asked for,
 * and reads the new disc's FST. A wrong disc gets another stop of the
 * motor, so asking for the other disc again is how a game says "not this
 * one".
 *
 * So here: a stop of the motor opens the cover; about two seconds later
 * (the reference loaders' wait, long enough for a game's "Please insert
 * Disc 2" to show) the other disc goes in and the cover closes. If the
 * game is waiting on the cover then, its DI interrupt handler runs with
 * the cover interrupt; if not yet, the blob looks again a few times, then
 * leaves the interrupt bit set as the drive would.
 *
 * The state lives in MEM2 (loader-placed, gc_rt_context.disc_change),
 * with each disc's runs and RVZ reader. Kept apart from gc_rt.c, with the
 * time passed in, so the host tests can check the steps
 * (tests/gcdisc_tests.cpp). Freestanding C99, static inline. */
#ifndef RIFTWII_GC_DISC_H
#define RIFTWII_GC_DISC_H

#include <stdint.h>

#define GC_DISC_MAGIC 0x52574443u  /* "RWDC" */
#define GC_DISC_COUNT 2u
/* The alarm's room: an SDK OSAlarm, or libogc's handle (gc_rt.h's
 * GC_ALARM_BYTES). */
#define GC_DISC_ALARM_BYTES 0x40u
/* Looks at a closed cover the game has not started waiting on yet, before
 * the interrupt bit is just left set. */
#define GC_DISC_LOOKS 20u

/* The DI cover register (DICVR, the stand-in's word 1): the cover's
 * state (1: open), the interrupt's mask, the interrupt. */
#define GC_DI_CVR 1u
#define GC_DICVR_OPEN 1u
#define GC_DICVR_MASK 2u
#define GC_DICVR_INT 4u

/* gc_disc_change.state */
#define GC_DISC_IDLE 0u
#define GC_DISC_COVER_OPEN 1u  /* the motor stopped: the other disc goes in at `due` */
#define GC_DISC_CLOSED 2u      /* the other disc is in: the game has not taken the interrupt yet */

/* What gc_disc_cover_alarm asks of the blob: bits. */
#define GC_DISC_SWAPPED 1u  /* the other disc is in: serve reads from its runs */
#define GC_DISC_RAISE 2u    /* set DICVR's interrupt bit and run the game's DI interrupt handler */
#define GC_DISC_LATCH 4u    /* set DICVR's interrupt bit only: nobody waited for it */
#define GC_DISC_AGAIN 8u    /* set the alarm again, `ticks` from now */

/* One disc as the blob reads it: its runs (struct gc_rt_run[run_count],
 * in MEM2) and its RVZ reader (struct gc_rvz, 0 for an ISO). */
struct gc_disc_image {
    uint32_t runs;
    uint32_t run_count;
    uint32_t rvz;
};

struct gc_disc_change {
    uint32_t magic;           /* GC_DISC_MAGIC: set up by the loader */
    /* Loader-filled. */
    struct gc_disc_image discs[GC_DISC_COUNT];  /* disc 1, then disc 2 */
    uint32_t cover_ticks;     /* timebase ticks from the motor's stop to the cover closing */
    uint32_t look_ticks;      /* between looks at a closed cover nobody waits on */
    /* State. */
    uint32_t current;         /* the disc in: 0 disc 1, 1 disc 2 */
    uint32_t state;           /* GC_DISC_* */
    uint32_t due;             /* timebase (low word) when the cover closes */
    uint32_t looks;           /* looks at the closed cover so far */
    uint32_t alarm_queued;    /* 1: `alarm` is in the OS's queue */
    /* Counters, for the trace and a memory dump. */
    uint32_t stops;           /* stops of the motor seen */
    uint32_t changes;         /* discs changed */
    uint32_t raised;          /* cover interrupts the game's handler took */
    uint32_t unseen;          /* changes whose interrupt nobody waited for */
    uint32_t pad[2];
    uint8_t alarm[GC_DISC_ALARM_BYTES] __attribute__((aligned(32)));
};

/* The motor stopped at `now`: the cover opens, and the other disc goes in
 * cover_ticks later. A stop while the cover is open changes nothing (the
 * game asked twice); a stop after the other disc went in but before the
 * game looked starts another change (the game saw the wrong disc another
 * way). 1: the caller sets `alarm`, cover_ticks from now; 0: nothing to
 * set (no second disc, or the alarm is already queued and will find the
 * new `due`). */
static inline uint32_t gc_disc_motor_stopped(struct gc_disc_change* dc, uint32_t now) {
    if (dc->magic != GC_DISC_MAGIC) return 0;
    dc->stops++;
    if (dc->state == GC_DISC_COVER_OPEN) return 0;
    dc->state = GC_DISC_COVER_OPEN;
    dc->due = now + dc->cover_ticks;
    dc->looks = 0;
    if (dc->alarm_queued) return 0;
    dc->alarm_queued = 1;
    return 1;
}

/* The disc-change alarm went off at `now`; `dicvr` is the cover register
 * as the game left it. Returns GC_DISC_* bits, and in `*ticks` when to
 * look again (GC_DISC_AGAIN). */
static inline uint32_t gc_disc_cover_alarm(struct gc_disc_change* dc, uint32_t now, uint32_t dicvr, uint32_t* ticks) {
    uint32_t result = 0;
    const int32_t early = (int32_t)(dc->due - now);
    dc->alarm_queued = 0;
    *ticks = 0;
    if (dc->magic != GC_DISC_MAGIC || dc->state == GC_DISC_IDLE) return 0;
    if (dc->state == GC_DISC_COVER_OPEN) {
        /* Set early (the alarm was queued for an earlier change): the
         * rest of the wait. */
        if (early > 0) {
            dc->alarm_queued = 1;
            *ticks = (uint32_t)early;
            return GC_DISC_AGAIN;
        }
        dc->current = dc->current == 0 ? 1u : 0u;
        dc->changes++;
        dc->state = GC_DISC_CLOSED;
        result |= GC_DISC_SWAPPED;
    }
    if (dicvr & GC_DICVR_MASK) {
        dc->state = GC_DISC_IDLE;
        dc->raised++;
        return result | GC_DISC_RAISE;
    }
    dc->looks++;
    if (dc->looks < GC_DISC_LOOKS) {
        dc->alarm_queued = 1;
        *ticks = dc->look_ticks;
        return result | GC_DISC_AGAIN;
    }
    dc->state = GC_DISC_IDLE;
    dc->unseen++;
    return result | GC_DISC_LATCH;
}

/* The game restarts (Reset, gc_on_hot_reset) with MEM1 as it was at the
 * jump: disc 1's FST and ID, and an OS whose alarm queue no longer holds
 * the disc-change alarm. Disc 1 goes back in. */
static inline void gc_disc_restart(struct gc_disc_change* dc) {
    uint32_t i;
    if (dc->magic != GC_DISC_MAGIC) return;
    dc->current = 0;
    dc->state = GC_DISC_IDLE;
    dc->looks = 0;
    dc->alarm_queued = 0;
    for (i = 0; i < GC_DISC_ALARM_BYTES; ++i) dc->alarm[i] = 0;  /* a libogc handle made before is gone too */
}

#endif
