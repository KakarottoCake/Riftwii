/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shapes, the second of the stand-in's four layers (sources, shapes, slot
 * ownership, delivery; notes in #587): a controller's samples (rtsrc) as
 * the Wii controller the game expects. Each shape is a button layout plus
 * whatever motion the Wii controller has that the controller doesn't: the
 * shake (a button held), the pointer (the C-stick) and the sideways Wii
 * Remote's tilt. Only the shapes know a layout; ownership and delivery
 * never look at a controller's buttons. The build picks the shape today
 * (RT_PAD_FORM_NUNCHUK, RT_PAD_FORM_SIDEWAYS, else the Classic Controller);
 * the layouts are tables a pack's own layout file can fill later. Free of
 * the hardware, so the host tests run it. */
#ifndef RIFTWII_RTSHAPE_H
#define RIFTWII_RTSHAPE_H

#include <stdint.h>

#include "rtsrc.h"

#define RTSHAPE_PORTS RTSRC_PORTS

/* The shake: all three axes swing between +RTSHAPE_SHAKE_G and
 * -RTSHAPE_SHAKE_G as a trapezoid, moving every count: a ramp in odd sixths
 * of the peak (+-5, +-3, +-1), crossing zero between counts (so it comes
 * back below 3 g, as Donkey Kong Country Returns' "shake to start" wants,
 * but is never exactly at rest mid-shake: an all-zero raw sample reads as a
 * Nunchuk with no calibration), and a flat top of RTSHAPE_SHAKE_TOP counts
 * at each end (so
 * a frame's read always sees the peak: 3.4 g is past the 3.2 g Batman: The
 * Brave and the Bold checks for its heavy attacks, 2.0 g for the Nunchuk).
 * Moving every count, not held still between flips, is what Wario Land:
 * Shake It!'s earthquake punch checks for: acceleration and speed that
 * change frame after frame, as a hand's (and Dolphin's emulated) shake
 * does. The count advances every shake_flip / RTSHAPE_SHAKE_FLIP ticks (the
 * loader's 33-40 ms a flip, so a swing of RTSHAPE_SHAKE_PERIOD counts is
 * about 6-8 Hz), or every sample with no shake_flip. acc_speed is a
 * steady RTSHAPE_SHAKE_SPEED while shaking: the change from one count to
 * the next on the ramp. Numerators over RTHOME_AXIS_FULL (256: 1 g). */
#define RTSHAPE_SHAKE_G 870
#define RTSHAPE_SHAKE_VALUE 1508  /* |(3.4, 3.4, 3.4)| g */
#define RTSHAPE_SHAKE_FLIP 4u     /* counts a shake_flip */
#define RTSHAPE_SHAKE_PERIOD 18u  /* counts a whole swing: two tops and two ramps */
#define RTSHAPE_SHAKE_RAMP 6      /* the peak in ramp sixths */
#define RTSHAPE_SHAKE_TOP 3u      /* counts held at each peak */
#define RTSHAPE_SHAKE_SPEED 502   /* |(1.13, 1.13, 1.13)| g: one ramp step */
/* Full C-stick moves the pointer this much a sample (the screen is two
 * RTHOME_POINTER_FULL wide, so about a second across), and it stays inside
 * +-RTSHAPE_POINTER_EDGE, so the cursor never leaves the screen. */
#define RTSHAPE_POINTER_STEP 143
#define RTSHAPE_POINTER_EDGE 3891
/* The sideways form's tilt: the C-stick held right turns the Wii Remote as
 * a wheel steering right, gravity from -Y towards -Z (Dolphin's emulated
 * sideways Wii Remote, Excite Truck's tutorial, 22 Nov): full C-stick is
 * sin 55 degrees of it, over RTHOME_AXIS_FULL. */
#define RTSHAPE_TILT_SIN 210

/* The motion each slot's shape keeps between samples. */
struct rtshape {
    uint32_t pointer_show;                  /* ticks the pointer stays shown after the C-stick (loader-filled) */
    int32_t pointer[RTSHAPE_PORTS][2];      /* each slot's pointer, x and y over RTHOME_POINTER_FULL */
    uint32_t pointer_moved[RTSHAPE_PORTS];  /* when the C-stick last moved it (low bit set), 0: never */
    uint32_t shake[RTSHAPE_PORTS];          /* the shake's count while its button is held, 0: not held */
    /* The shake's rhythm by time, not by samples (a game that reads more
     * often would shake faster): when its button went down (low bit set),
     * which slots stepped since a read last took it, and the ticks between
     * steps (loader-filled; 0: every RTSHAPE_SHAKE_FLIP samples). */
    uint32_t shake_from[RTSHAPE_PORTS];
    uint32_t shake_jump;
    uint32_t shake_flip;
    uint32_t camera;  /* slots whose Wii Remote camera the game wants on (KPAD's switch), one bit each */
    /* The pointing form moves its pointer by time: when its motion was last
     * counted up to (low bit set, 0: not since the camera came on or the
     * controller was plugged in), and the part of a step not moved yet on
     * each axis, so neither close samples nor a slight push lose movement.
     * In every build, so the state's layout is the same for all of them. */
    uint32_t pointer_at[RTSHAPE_PORTS];
    int32_t pointer_rest[RTSHAPE_PORTS][2];
};

/* A Wii controller at one moment, in the Wii's own terms: the buttons,
 * the Nunchuk's stick, both accelerometers and the pointer, all
 * numerators over RTHOME_AXIS_FULL but the pointer's (over
 * RTHOME_POINTER_FULL). The words (hold, trig, release) are delivery's:
 * newly pressed is against what the game last got. */
struct rtshape_out {
    uint32_t hold, trig, release;
    int32_t stick[2];
    int32_t acc[3], acc_value, acc_speed;           /* the Wii Remote's */
    int32_t fs_acc[3], fs_acc_value, fs_acc_speed;  /* the Nunchuk's */
    int32_t pointer[2];
    int pointer_shown;  /* the game has the camera on, or the C-stick moved it within pointer_show ticks */
#ifdef RT_PAD_FORM_POINTING
    /* The pointing form's roll as the camera sees it (KPAD's horizon: the
     * line between the sensor bar's lights), cos and sin over
     * RTHOME_AXIS_FULL: a game with the pointer may read its twist there. */
    int32_t horizon[2];
#endif
};

/* The buttons in each shape's default layout (#587), as KPADStatus' hold
 * word has them. Classic: the control stick and C-stick as the two
 * sticks, A B X Y as a b x y, L and R as themselves, Z as ZR, Start as +,
 * the D-pad as the D-pad. Wii Remote + Nunchuk: A and B as A and B, Y as
 * 2, Z as -, Start as +, the D-pad as the D-pad, L as the Nunchuk's Z and
 * R as its C (each also from its analog trigger, pushed past a third of
 * the way, as a Nunchuk's buttons need no hard press); X is the shake.
 * Sideways: A as 2, B as 1, X as A, Y as B, Z as -, Start as +, and the
 * D-pad and the control stick (past half way) as the Wii Remote's D-pad
 * turned as the screen shows it held sideways (D-pad on the left: screen
 * left is the remote's Up, right its Down, up its Right, down its Left);
 * R is the shake, L nothing. */
uint32_t rtshape_classic_buttons(const struct rtsrc_sample* p);
uint32_t rtshape_nunchuk_buttons(const struct rtsrc_sample* p);
uint32_t rtshape_sideways_buttons(const struct rtsrc_sample* p);
/* Pointing (#587, 22 Nov: a Wii Remote held upright, pointed at the
 * screen, no Nunchuk): A as A, B as B, X as 1, Y as 2, Z as -, Start as +,
 * the D-pad as the D-pad (not turned); the control stick is the pointer,
 * the C-stick tilts it, R shakes it, L nothing (rtshape_pointing). */
uint32_t rtshape_pointing_buttons(const struct rtsrc_sample* p);

/* The pointing form's pointer moves by time: a frame (1/60 s) of the Wii's
 * time base (60.75 MHz), and the longest gap between two samples that
 * still counts in full. */
#define RTSHAPE_TICKS_PER_FRAME 1012500u
#define RTSHAPE_POINTER_GAP_FRAMES 4u

/* Once a sample, the build's shape's motion on `port`: the pointer follows
 * the C-stick (in the sideways shape only while the game has the camera
 * on: else the C-stick tilts), and the shake counts while its button is
 * held. Nothing in the Classic shape. */
void rtshape_step(struct rtshape* sh, uint32_t port, const struct rtsrc_sample* p, uint32_t now);

/* `port`'s controller unplugged: its shake and pointer's last move are
 * forgotten (an old mark never reads as recent after the time base wraps). */
void rtshape_reset(struct rtshape* sh, uint32_t port);

/* The game wants `port`'s Wii Remote camera on (`mode` not 0) or off, as
 * KPAD's switch says (KPADEnableDPD, KPADDisableDPD): a game turns it on
 * for its pointer menus (Batman: The Brave and the Bold from its title
 * screen on), so the pointer is shown while it is on, as well as after the
 * C-stick moves. */
void rtshape_camera(struct rtshape* sh, uint32_t port, uint32_t mode);

/* A controller held still with nothing pressed: both accelerometers 1 g
 * along -Y (as KPAD reports a real one at rest), stick and pointer centred,
 * the pointer hidden. The words are left alone. */
void rtshape_rest(struct rtshape_out* out);

/* The Wii Remote + Nunchuk's motion on `port` from sample `p` at `now`
 * into `out` (the words are left alone): the Nunchuk's stick, the pointer,
 * and the Wii Remote's accelerometer shaking while the shake's button is
 * held, a new step of it a jump in speed until a read takes it
 * (rtshape_take_step). */
void rtshape_nunchuk(const struct rtshape* sh, uint32_t port, const struct rtsrc_sample* p, uint32_t now,
                     struct rtshape_out* out);

/* The sideways Wii Remote's on `port`: the pointer while the game has the
 * camera on, else the tilt the C-stick gives; the shake while its button is
 * held. The Nunchuk's fields stay at rest. */
void rtshape_sideways(const struct rtshape* sh, uint32_t port, const struct rtsrc_sample* p, struct rtshape_out* out);

#ifdef RT_PAD_FORM_POINTING
/* The pointing Wii Remote on `port`: at rest, tilted any way by the C-stick
 * (up dips its nose, right rolls it right; a diagonal no further than a
 * full push one way, the acceleration staying 1 g), the horizon turning
 * with the roll, shaking while R is held, and with the camera on the
 * pointer the control stick moves (rtshape_step, by time). Built only with
 * RT_PAD_FORM_POINTING. */
void rtshape_pointing(const struct rtshape* sh, uint32_t port, const struct rtsrc_sample* p, struct rtshape_out* out);
#endif

/* The game's KPAD read got `port`'s shake step (its jump in speed): the
 * next read's has none until the shake steps again. A raw report, which has
 * no speed, leaves it (CodeRabbit CLI, #610). */
void rtshape_take_step(struct rtshape* sh, uint32_t port);

#endif
