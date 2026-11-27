/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GameCube controllers as Classic Controllers (#587): with the game's own
 * setting on (off by default), each GameCube controller in port N stands in
 * on Wii Remote channel N-1 as a Classic Controller for as long as it is
 * plugged in, so a game that takes a Classic Controller plays from it. The
 * HOME Menu's stand-in (rthome.h) does the same for a moment; this one stays.
 *
 * The stand-in is four layers (notes in #587): sources (rtsrc.h: each
 * controller, one sample per report), shapes (rtshape.h: a sample as the
 * Wii controller the game expects), slot ownership (rtslot.h: who holds
 * each slot, what the game was told) and delivery (rtdeliver.h: what the
 * game asked for, in its library's layout). This file joins them: the one
 * state the loader places, and the calls the pad blob's hooks make. Free of
 * the hardware, so the host tests run it. The state lives in MEM2 next to
 * the adapter driver's (the blob's context has no room for four ports), and
 * the context holds its address. */
#ifndef RIFTWII_RTGCWII_H
#define RIFTWII_RTGCWII_H

#include <stdint.h>

#include "rtslot.h"
#include "rtsrc.h"
#include "rtshape.h"
#include "rtdeliver.h"

#define RTGCWII_CHANNELS 4u
#define RTGCWII_PAD_BYTES 12u

struct rtgcwii {
    struct rtslot slot;        /* who holds each Wii Remote slot, and what the game was told (rtslot.h) */
    struct rtsrc src;          /* each player's controller, one sample per report (rtsrc.h) */
    uint32_t hold[RTGCWII_CHANNELS];  /* each channel's Wii buttons now, in its shape */
    struct rtdeliver deliver;  /* what the game's KPAD read last got on each slot (rtdeliver.h) */
    /* Many games that take a Classic Controller never call PADRead (Kamen
     * Rider: Dragon Knight reads only through KPAD), so the adapter would
     * never be read: the blob then calls the game's PADRead itself, once a
     * frame, as that game's pack did. */
    uint32_t game_read;  /* when the game itself last called PADRead (low bit set), 0: never */
    uint32_t own_read;   /* when the blob last called it (low bit set), 0: never */
    uint32_t reading;    /* the blob's own PADRead call is under way */
};

/* At each PADRead, the four ports' statuses (12 bytes each) as the game
 * will see them (the adapter's already in): a port with a controller keeps
 * its status and its Classic buttons move on one sample; a port with none
 * for `grace` ticks loses its channel's stand-in. A short gap (a wireless
 * pad's dropped report) never unplugs the player. */
void rtgcwii_pads(struct rtgcwii* w, const uint8_t* status, uint32_t now);

/* The stand-in's Classic Controller on `channel`, for the game's KPAD read:
 * its hold, trig (newly pressed since the game last got it) and release
 * words, the same for every read until the next sample, and its sticks and
 * triggers as numerators over RTHOME_AXIS_FULL (rthome_classic_axes), in
 * #587's default layout (rtshape_classic_buttons). */
void rtgcwii_classic(struct rtgcwii* w, uint32_t channel, uint32_t* hold, uint32_t* trig, uint32_t* release,
                     int32_t axes[6]);

/* Its buttons alone, for a raw status (KPADGetUnifiedWpadStatus), which
 * moves nothing on; 0 where the stand-in doesn't answer. */
uint32_t rtgcwii_hold(const struct rtgcwii* w, uint32_t channel);

/* At a KPAD read or WPADProbe: whether the blob should call the game's
 * PADRead now (and, if so, notes the time it does). Only when the game
 * hasn't called it itself within 50 ms, and at most every 8 ms (about once
 * a frame). Times are compared unsigned, and a read by the game that has
 * gone stale is forgotten: the time base's low word passes its halfway
 * mark every 35 s, and a signed difference from a read that long ago
 * turned negative and looked recent (Dragon Knight called PADRead once at
 * start-up, and its save screens then took no presses). */
int rtgcwii_should_read(struct rtgcwii* w, uint32_t now, uint32_t ticks_per_ms);

/* PADRead ran at `now`; `own`: the blob's own call (rtgcwii_should_read),
 * else the game's. */
void rtgcwii_read_by(struct rtgcwii* w, uint32_t now, int own);

/* Slot ownership's decisions (rtslot.h) by their old names. */
#define RTGCWII_NO_ERROR RTSLOT_NO_ERROR
#define RTGCWII_EMPTY_READS RTSLOT_EMPTY_READS
#define rtgcwii_probed(w, channel, result) rtslot_probed(&(w)->slot, (channel), (result))
#define rtgcwii_probe_count(w, channel) rtslot_probe_count(&(w)->slot, (channel))
#define rtgcwii_real_check_due(w, now, ticks_per_ms) rtslot_real_check_due(&(w)->slot, (now), (ticks_per_ms))
#define rtgcwii_on(w, channel) rtslot_on(&(w)->slot, (channel))
#define rtgcwii_connect_change(w, leave, join) rtslot_connect_change(&(w)->slot, (leave), (join))
#define rtgcwii_not_told(w, channel) rtslot_not_told(&(w)->slot, (channel))
#define rtgcwii_still_told(w, channel) rtslot_still_told(&(w)->slot, (channel))
#define rtgcwii_next_change(w, skip, keep, channel, reason) rtslot_next_change(&(w)->slot, (skip), (keep), (channel), (reason))
#define rtgcwii_kpad_read(w, channel, result, error, now) rtslot_kpad_read(&(w)->slot, (channel), (result), (error), (now))

#endif
