/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Delivery, the last of the stand-in's four layers (sources, shapes, slot
 * ownership, delivery; notes in #587): what the game asked for, from a
 * shape's output, in its library's own layout. "Newly pressed" and "let go"
 * belong here, against what the game's KPAD read last got (a raw report has
 * neither), and so do the raw report's bytes (WPADRead, KPAD's raw
 * samples). Packs never reach this layer: a game's own fix here is keyed to
 * what the finders found in its code. Free of the hardware, so the host
 * tests run it. */
#ifndef RIFTWII_RTDELIVER_H
#define RIFTWII_RTDELIVER_H

#include <stdint.h>

#include "rtshape.h"

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

/* WPADStatus, as WPADRead gives it: the buttons (big-endian) at 0, the
 * acceleration at 2, 4 and 6 (signed, RTDELIVER_ACC_G a g, the Wii Remote's
 * own axes: lying flat, +z is up), four camera objects at 8 (x and y signed
 * on 1024 x 768, the size, an id and a pad byte; size 0: none), the device
 * type at 40 and the error at 41; a Wii Remote + Nunchuk's WPADFSStatus goes
 * on with the Nunchuk's acceleration at 42, 44 and 46 and its stick at 48
 * and 49 (signed bytes, RTDELIVER_FS_STICK_FULL full). */
#define RTDELIVER_WPAD_CORE_BYTES 42u
#define RTDELIVER_WPAD_FS_BYTES 50u
#define RTDELIVER_WPAD_ERR 41u  /* the report's error byte: 0 is a real report */
#define RTDELIVER_ACC_G 104     /* Dolphin's calibration: zero 0x200, one g 0x268 */
#define RTDELIVER_FS_STICK_FULL 100
/* The camera's two sensor-bar dots: centred on (512, 384) when the pointer
 * is at the screen's centre, moving against the pointer by up to
 * RTDELIVER_DOT_RANGE_X / Y at its edge, RTDELIVER_DOT_APART apart (Dolphin's
 * emulated Wii Remote at rest: 137 pixels, size 3). */
#define RTDELIVER_DOT_RANGE_X 384
#define RTDELIVER_DOT_RANGE_Y 288
#define RTDELIVER_DOT_APART 137
#define RTDELIVER_DOT_SIZE 3u

/* Data formats WPAD knows: a bare Wii Remote's with the camera, and the
 * range with a Nunchuk's part (the last of them with the camera). */
#define RTDELIVER_FMT_CORE_ACC_DPD 2u
#define RTDELIVER_FMT_FS_FIRST 3u
#define RTDELIVER_FMT_FS_ACC_DPD 5u

/* Whether data format `format` has the camera's dots. */
int rtdeliver_format_has_camera(uint32_t format);

/* A Wii Remote's raw report into `out` from shape output `f`: buttons,
 * acceleration, and with `camera` the pointer as the two dots (none seen
 * while the pointer is hidden). `nunchuk`: a Wii Remote with a Nunchuk,
 * whose device type the report carries and, for a data format with the
 * Nunchuk's part, its acceleration and stick; without one, that part stays
 * zeros with the device type a bare Wii Remote (as WPAD gives a Wii Remote
 * with nothing in its port), so a format it was told worked still gets a
 * report that long (CodeRabbit on #616). Returns the bytes written: the
 * format's report size. */
uint32_t rtdeliver_wpad(const struct rtshape_out* f, uint32_t format, int camera, int nunchuk, uint8_t* out);

/* An acceleration over RTHOME_AXIS_FULL (KPAD's axes) as a raw report's
 * three counts, at `p` (the Wii Remote's at 2, the Nunchuk's at 42). */
void rtdeliver_put_acc(uint8_t* p, const int32_t acc[3]);

#endif
