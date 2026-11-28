/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What the game asked for, in its library's layout (rtdeliver.h). */
#ifndef RT_PAD_STANDIN
#define RT_PAD_STANDIN
#endif
#include "rtdeliver.h"

#include "rthome.h"

void rtdeliver_arrived(struct rtdeliver* d, uint32_t slot, uint32_t hold) {
    if (slot < RTDELIVER_SLOTS) d->delivered[slot] = hold;
}

void rtdeliver_gone(struct rtdeliver* d, uint32_t slot) {
    if (slot < RTDELIVER_SLOTS) d->delivered[slot] = 0;
}

void rtdeliver_take(struct rtdeliver* d, uint32_t slot, uint32_t sample, uint32_t hold, uint32_t* trig,
                    uint32_t* release) {
    if (slot >= RTDELIVER_SLOTS) {
        *trig = *release = 0;
        return;
    }
    if (d->worked[slot] != sample) {
        d->worked[slot] = sample;
        d->trig[slot] = hold & ~d->delivered[slot];
        d->release[slot] = d->delivered[slot] & ~hold;
        d->delivered[slot] = hold;
    }
    *trig = d->trig[slot];
    *release = d->release[slot];
}

uint32_t rtdeliver_float_bits(int32_t numerator) {
    uint32_t sign = 0, magnitude, top = 0;
    if (numerator == 0) return 0;
    /* The size taken unsigned, so the most negative value has one too. */
    magnitude = (uint32_t)numerator;
    if (numerator < 0) {
        sign = 0x80000000u;
        magnitude = 0u - magnitude;
    }
    while (top < 31u && (magnitude >> (top + 1)) != 0) ++top;  /* its highest bit */
    /* magnitude / 256 = 1.mantissa * 2^(top - 8) */
    return sign | ((127u + top - 8u) << 23) |
           ((top <= 23u ? magnitude << (23u - top) : magnitude >> (top - 23u)) & 0x007FFFFFu);
}

static void rtdeliver_put16(uint8_t* p, int32_t v) {
    p[0] = (uint8_t)((uint32_t)v >> 8);
    p[1] = (uint8_t)v;
}

/* KPAD's axes (x left, y down along the remote at rest, z its length) as
 * WPAD's three counts (x, y along its length, z up out of its face): KPAD's
 * x, y and z are WPAD's -x, -z and y. */
void rtdeliver_put_acc(uint8_t* p, const int32_t acc[3]) {
    rtdeliver_put16(p, -acc[0] * RTDELIVER_ACC_G / RTHOME_AXIS_FULL);
    rtdeliver_put16(p + 2, acc[2] * RTDELIVER_ACC_G / RTHOME_AXIS_FULL);
    rtdeliver_put16(p + 4, -acc[1] * RTDELIVER_ACC_G / RTHOME_AXIS_FULL);
}

static void rtdeliver_put_dot(uint8_t* o, int32_t x, int32_t y, uint32_t size, uint32_t id) {
    if (x < 0) x = 0;
    if (x > 1023) x = 1023;
    if (y < 0) y = 0;
    if (y > 767) y = 767;
    rtdeliver_put16(o, x);
    rtdeliver_put16(o + 2, y);
    rtdeliver_put16(o + 4, (int32_t)size);
    o[6] = (uint8_t)id;
    o[7] = 0;
}

int rtdeliver_format_has_camera(uint32_t format) {
    return format == RTDELIVER_FMT_CORE_ACC_DPD || format == RTDELIVER_FMT_FS_ACC_DPD;
}

uint32_t rtdeliver_wpad(const struct rtshape_out* f, uint32_t format, int camera, int nunchuk, uint8_t* out) {
    uint32_t i, bytes = RTDELIVER_WPAD_CORE_BYTES;
    int32_t mid_x, mid_y;
    if (format >= RTDELIVER_FMT_FS_FIRST && format <= RTDELIVER_FMT_FS_ACC_DPD) bytes = RTDELIVER_WPAD_FS_BYTES;
    for (i = 0; i < bytes; ++i) out[i] = 0;
    rtdeliver_put16(out, (int32_t)(f->hold & 0xFFFFu));
    rtdeliver_put_acc(out + 2, f->acc);
    if (camera) {
        if (f->pointer_shown) {
            /* The dots move against the pointer: pointing right or down puts
             * the sensor bar left of or above the camera's centre. */
            int32_t hx = RTHOME_AXIS_FULL, hy = 0;
            mid_x = 512 - f->pointer[0] * RTDELIVER_DOT_RANGE_X / RTHOME_POINTER_FULL;
            mid_y = 384 - f->pointer[1] * RTDELIVER_DOT_RANGE_Y / RTHOME_POINTER_FULL;
#ifdef RT_PAD_FORM_POINTING
            /* The pointing form's roll turns the pair around its midpoint,
             * as KPAD's horizon (the camera sees it mirrored on both axes,
             * as the midpoint; CodeRabbit, fork #23). */
            hx = f->horizon[0];
            hy = f->horizon[1];
#endif
            rtdeliver_put_dot(out + 8, mid_x + (RTDELIVER_DOT_APART / 2 + 1) * hx / RTHOME_AXIS_FULL,
                              mid_y + (RTDELIVER_DOT_APART / 2 + 1) * hy / RTHOME_AXIS_FULL, RTDELIVER_DOT_SIZE, 0);
            rtdeliver_put_dot(out + 16, mid_x - (RTDELIVER_DOT_APART / 2) * hx / RTHOME_AXIS_FULL,
                              mid_y - (RTDELIVER_DOT_APART / 2) * hy / RTHOME_AXIS_FULL, RTDELIVER_DOT_SIZE, 1);
        } else {
            rtdeliver_put_dot(out + 8, 0, 767, 0, 0);
            rtdeliver_put_dot(out + 16, 0, 767, 0, 1);
        }
        /* The other two: nothing seen (size 0), as Dolphin's camera gives them. */
        rtdeliver_put_dot(out + 24, 0, 767, 0, 2);
        rtdeliver_put_dot(out + 32, 0, 767, 0, 3);
    }
    if (nunchuk) {
        out[40] = 1u; /* WPAD_DEV_FREESTYLE */
        if (bytes == RTDELIVER_WPAD_FS_BYTES) {
            rtdeliver_put_acc(out + 42, f->fs_acc);
            out[48] = (uint8_t)(int8_t)(f->stick[0] * RTDELIVER_FS_STICK_FULL / RTHOME_AXIS_FULL);
            out[49] = (uint8_t)(int8_t)(f->stick[1] * RTDELIVER_FS_STICK_FULL / RTHOME_AXIS_FULL);
        }
    }
    return bytes;
}
