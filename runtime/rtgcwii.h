/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GameCube controllers as Wii controllers (#587): with the game's own
 * setting on (off by default), each GameCube controller in port N stands in
 * on Wii Remote channel N-1, as a Classic Controller, a Wii Remote + Nunchuk
 * or a sideways Wii Remote (the build's form), for as long as it is plugged
 * in. The HOME Menu's stand-in (rthome.h) does the same for a moment; this
 * one stays.
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

/* rtgcwii.info_answers: what the hooked WPADGetInfo(Async) gets on the
 * stand-in's channel. */
#define RTGCWII_INFO_BUSY 0u
#define RTGCWII_INFO_ANSWER 1u
#define RTGCWII_INFO_BLOCKING 2u

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
    struct rtshape shape;  /* each slot's shape's motion: shake, pointer, camera (rtshape.h) */
    /* Where KPAD keeps each channel's camera switch (KPADEnableDPD's
     * code): the byte at kpad_dpd + channel * kpad_dpd_stride +
     * kpad_dpd_offset, 0: not found (the pointer shows only after the
     * C-stick moves). Loader-filled. */
    uint32_t kpad_dpd, kpad_dpd_stride, kpad_dpd_offset;
    /* A 2006 KPAD game's WPADGetInfoAsync callback, answered for the
     * stand-in and told at the game's next KPAD read on that channel (as a
     * real Wii Remote's answer comes later), 0: none waiting. */
    uint32_t info_callback[RTGCWII_CHANNELS];
    /* RTGCWII_INFO_ANSWER: a 2006 KPAD game, whose WPADGetInfoAsync on the
     * stand-in's channel gets the stand-in's info (pad_on_info);
     * RTGCWII_INFO_BUSY: a later game, where it is busy (-2), so a blocking
     * WPADGetInfo (Mario Strikers Charged's, after its title) returns at once
     * instead of sleeping for a reply only a real Wii Remote sends; the game
     * skips its battery check. RTGCWII_INFO_BLOCKING: the hooked function is
     * a blocking WPADGetInfo with the request built in (Scarface: The World
     * Is Yours), which sleeps even with no Wii Remote there: busy before the
     * original runs (pad_info_first). Loader-filled. */
    uint32_t info_answers;
    /* The 2006 WPAD's connect callbacks (its games have no KPAD-level
     * one): the table of the channels' control blocks and the callback's
     * place in one, 0: none. Loader-filled. */
    uint32_t wpad_connect_table, wpad_connect_offset;
    /* The stand-in below KPAD (the Wii Remote forms), for a game that reads
     * WPAD itself (Punch-Out!!: WPADRead into its own pad manager): the data
     * format the game last asked for on each channel (WPADSetDataFormat,
     * answered for the stand-in), 0 until it asks. */
    uint32_t format[RTGCWII_CHANNELS];
    /* wpad_connect_table is the later WPAD's (find_wpad_connect_later): the
     * game's sleeps read the pads for the stand-in only while the game
     * itself is quiet (rtslot_quiet_due, pad_hook.c's pad_reads_at_sleep).
     * Loader-filled. */
    uint32_t wpad_connect_later;
    /* WPAD's extension callbacks (the game's, WPADSetExtensionCallback):
     * the table of the channels' control blocks and the callback's place in
     * one (just before the connect callback's), 0: none. Told the stand-in's
     * attachment after its "connected" (pad_tell_extension). Loader-filled. */
    uint32_t wpad_ext_table, wpad_ext_offset;
};

/* At each PADRead, the four ports' statuses (12 bytes each) as the game
 * will see them (the adapter's already in): a port with a controller keeps
 * its status and its buttons move on one sample; a port with none for
 * `grace` ticks loses its channel's stand-in. A short gap (a wireless pad's
 * dropped report) never unplugs the player. */
void rtgcwii_pads(struct rtgcwii* w, const uint8_t* status, uint32_t now);

/* The Wii Remote + Nunchuk and sideways forms' buttons for a PADStatus
 * (rtshape_nunchuk_buttons, rtshape_sideways_buttons): in every build;
 * the forms' builds take theirs. */
uint32_t rtgcwii_map_nunchuk(const uint8_t* pad);
uint32_t rtgcwii_map_sideways(const uint8_t* pad);
uint32_t rtgcwii_map_pointing(const uint8_t* pad);

/* The game wants `channel`'s Wii Remote camera on (`mode` not 0) or off,
 * as KPAD's switch says (KPADEnableDPD, KPADDisableDPD): a game turns it on
 * for its pointer menus (Batman: The Brave and the Bold from its title
 * screen on), so the stand-in's pointer is shown while it is on, as well as
 * after the C-stick moves. */
void rtgcwii_camera(struct rtgcwii* w, uint32_t channel, uint32_t mode);

/* The stand-in's Wii Remote on `channel` in the build's shape (with a
 * Nunchuk, or sideways) at `now`, into `out`. The game's own KPAD read
 * takes the words (RTGCWII_TAKE_WORDS: newly pressed and let go against
 * what it last got) and the shake's step (RTGCWII_TAKE_STEP: its jump in
 * speed); a look takes neither and leaves them for that read: a raw report
 * (it has neither) takes 0, and the HOME Menu's stand-in's status in this
 * shape, whose buttons are its own, takes the step alone. Where the
 * stand-in isn't on, the buttons, stick and pointer are 0 (pointer hidden)
 * and both accelerometers read a still controller: 1 g along -Y. */
#define RTGCWII_TAKE_WORDS 1u
#define RTGCWII_TAKE_STEP 2u
void rtgcwii_wii_remote(struct rtgcwii* w, uint32_t channel, uint32_t now, struct rtshape_out* out, uint32_t takes);

/* The same as the game's KPAD read takes them, in the Nunchuk shape
 * (rtshape_nunchuk) or the sideways one (rtshape_sideways), whatever the
 * build's form: the host tests take each. */
void rtgcwii_nunchuk(struct rtgcwii* w, uint32_t channel, uint32_t now, struct rtshape_out* out);
void rtgcwii_sideways(struct rtgcwii* w, uint32_t channel, struct rtshape_out* out);
#ifdef RT_PAD_FORM_POINTING
/* The pointing Wii Remote on `channel` for the game's KPAD read (the words
 * taken, the shake's step too): rtshape_pointing's state, all 0 where the
 * stand-in isn't on. Built only with RT_PAD_FORM_POINTING. */
void rtgcwii_pointing(struct rtgcwii* w, uint32_t channel, struct rtshape_out* out);
#endif

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

/* The stand-in's raw status on `channel` (the Wii Remote forms) into `out`,
 * for a game that reads WPAD itself: its form's buttons and acceleration
 * and, while the camera is on (KPAD's switch, `kpad_camera`, or a data
 * format with the camera that the game asked for), the pointer as the two
 * dots; exactly as long as the data format the game asked for (WPADRead
 * copies that format's size). Returns the bytes written, 0 (none) where the
 * stand-in isn't on. In the Classic Controller form it writes nothing. */
uint32_t rtgcwii_wpad_status(struct rtgcwii* w, uint32_t channel, uint8_t* out, uint32_t now, int kpad_camera);

/* KPADGetUnifiedWpadStatus's raw samples for the stand-in's channel, newest
 * first: `count` entries of `entry_bytes` (the raw report, then its data
 * format in the entry's last-but-one byte), each `sample_ticks` older than
 * the one before, so a shake swings through them as a real Wii Remote's
 * samples do (Donkey Kong Country Returns' "shake to start" reads these,
 * not KPAD's statuses). In the Nunchuk form they always carry the
 * Nunchuk's part, as a real one's do. 0 when the channel isn't the
 * stand-in's (nothing written). */
uint32_t rtgcwii_unified(struct rtgcwii* w, uint32_t channel, uint8_t* out, uint32_t entry_bytes, uint32_t count,
                         uint32_t now, uint32_t sample_ticks, int kpad_camera);

/* --- The layers' names as the hooks and tests knew them before the
 * restructure. */
#define RTGCWII_NO_ERROR RTSLOT_NO_ERROR
#define RTGCWII_EMPTY_READS RTSLOT_EMPTY_READS
#define RTGCWII_RETELLS RTSLOT_RETELLS
#define rtgcwii_probed(w, channel, result) rtslot_probed(&(w)->slot, (channel), (result))
#define rtgcwii_probe_count(w, channel) rtslot_probe_count(&(w)->slot, (channel))
#define rtgcwii_real_check_due(w, now, ticks_per_ms) rtslot_real_check_due(&(w)->slot, (now), (ticks_per_ms))
#define rtgcwii_on(w, channel) rtslot_on(&(w)->slot, (channel))
#define rtgcwii_shaped(w) rtslot_shaped(&(w)->slot)
#define rtgcwii_home_took(w, channel) rtslot_home_took(&(w)->slot, (channel))
#define rtgcwii_home_keeps_shape(w, channel) rtslot_home_keeps_shape(&(w)->slot, (channel))
#define rtgcwii_connect_change(w, leave, join) rtslot_connect_change(&(w)->slot, (leave), (join))
#define rtgcwii_not_told(w, channel) rtslot_not_told(&(w)->slot, (channel))
#define rtgcwii_still_told(w, channel) rtslot_still_told(&(w)->slot, (channel))
#define rtgcwii_next_change(w, skip, keep, channel, reason) rtslot_next_change(&(w)->slot, (skip), (keep), (channel), (reason))
#define rtgcwii_kpad_read(w, channel, result, error, now) rtslot_kpad_read(&(w)->slot, (channel), (result), (error), (now))
#define rtgcwii_heard(w, channel) rtslot_heard(&(w)->slot, (channel))
#define rtgcwii_wii_read(w, now) rtslot_wii_read(&(w)->slot, (now))
#define rtgcwii_quiet_due(w, now, ticks_per_ms) rtslot_quiet_due(&(w)->slot, (now), (ticks_per_ms))
#define rtgcwii_retell(w, now) rtslot_retell(&(w)->slot, (now))
#define RTGCWII_SHAKE_G RTSHAPE_SHAKE_G
#define RTGCWII_SHAKE_VALUE RTSHAPE_SHAKE_VALUE
#define RTGCWII_SHAKE_FLIP RTSHAPE_SHAKE_FLIP
#define RTGCWII_SHAKE_PERIOD RTSHAPE_SHAKE_PERIOD
#define RTGCWII_SHAKE_SPEED RTSHAPE_SHAKE_SPEED
#define RTGCWII_SHAKE_TOP RTSHAPE_SHAKE_TOP
#define RTGCWII_POINTER_STEP RTSHAPE_POINTER_STEP
#define RTGCWII_POINTER_EDGE RTSHAPE_POINTER_EDGE
#define RTGCWII_TILT_SIN RTSHAPE_TILT_SIN
#define RTGCWII_TICKS_PER_FRAME RTSHAPE_TICKS_PER_FRAME
#define RTGCWII_POINTER_GAP_FRAMES RTSHAPE_POINTER_GAP_FRAMES
#define rtgcwii_float_bits rtdeliver_float_bits
#define RTGCWII_WPAD_CORE_BYTES RTDELIVER_WPAD_CORE_BYTES
#define RTGCWII_WPAD_FS_BYTES RTDELIVER_WPAD_FS_BYTES
#define RTGCWII_WPAD_ERR RTDELIVER_WPAD_ERR
#define RTGCWII_ACC_G RTDELIVER_ACC_G
#define RTGCWII_FS_STICK_FULL RTDELIVER_FS_STICK_FULL
#define RTGCWII_DOT_RANGE_X RTDELIVER_DOT_RANGE_X
#define RTGCWII_DOT_RANGE_Y RTDELIVER_DOT_RANGE_Y
#define RTGCWII_DOT_APART RTDELIVER_DOT_APART
#define RTGCWII_DOT_SIZE RTDELIVER_DOT_SIZE

#endif
