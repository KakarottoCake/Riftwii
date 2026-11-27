/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* GameCube controllers as Classic Controllers' decisions (rtgcwii.h). */
/* The Wii controller stand-ins' own file: linked only into the pad builds
 * with them (RT_PAD_STANDIN, Makefile.runtime), and compiled, unused, with
 * the DOL's and the host library's runtime sources; it takes rthome.h's
 * stand-in declarations in each. */
#ifndef RT_PAD_STANDIN
#define RT_PAD_STANDIN
#endif
#include "rtgcwii.h"
#include "rtshape.h"

#include "rthome.h"

/* Ticks since a time noted with its low bit set (so 0 can mean "none"). */
static uint32_t rtgcwii_elapsed(uint32_t now, uint32_t noted) { return now - (noted & ~1u); }

uint32_t rtgcwii_map_nunchuk(const uint8_t* pad) {
    struct rtsrc_sample p;
    rtsrc_decode(pad, &p);
    return rtshape_nunchuk_buttons(&p);
}

uint32_t rtgcwii_map_sideways(const uint8_t* pad) {
    struct rtsrc_sample p;
    rtsrc_decode(pad, &p);
    return rtshape_sideways_buttons(&p);
}

uint32_t rtgcwii_map_pointing(const uint8_t* pad) {
    struct rtsrc_sample p;
    rtsrc_decode(pad, &p);
    return rtshape_pointing_buttons(&p);
}

void rtgcwii_pads(struct rtgcwii* w, const uint8_t* status, uint32_t now) {
    /* Connected only (rtslot.inputless): a connected controller's sample is
     * all at rest (buttons up, sticks centred, no motion); it still comes
     * and goes as usual. */
    uint8_t at_rest[RTGCWII_PAD_BYTES];  /* (on the stack: the blob has no .rodata) */
    uint32_t i;
    for (i = 0; i < RTGCWII_PAD_BYTES; ++i) at_rest[i] = 0;
    for (i = 0; i < RTGCWII_CHANNELS; ++i) {
        const uint32_t bit = 1u << i;
        const uint8_t* s = status + i * RTGCWII_PAD_BYTES;
        const enum rtsrc_event event = rtsrc_port(
            &w->src, i, w->slot.inputless && (int8_t)s[RTSRC_PAD_ERR] == 0 ? at_rest : s, w->slot.grace, now);
        if (event == RTSRC_ARRIVED || event == RTSRC_REPORT) {
            struct rtsrc_sample p;
            rtsrc_latest(&w->src, i, &p);
#if defined(RT_PAD_FORM_NUNCHUK)
            w->hold[i] = rtshape_nunchuk_buttons(&p);
#elif defined(RT_PAD_FORM_SIDEWAYS)
            w->hold[i] = rtshape_sideways_buttons(&p);
#elif defined(RT_PAD_FORM_POINTING)
            w->hold[i] = rtshape_pointing_buttons(&p);
#else
            w->hold[i] = rtshape_classic_buttons(&p);
#endif
            rtshape_step(&w->shape, i, &p, now);
            /* Newly plugged in: what it holds already isn't newly pressed. */
            if (event == RTSRC_ARRIVED) rtdeliver_arrived(&w->deliver, i, w->hold[i]);
            w->slot.present |= bit;
        } else if (event == RTSRC_GONE) {
            w->slot.present &= ~bit;
            w->hold[i] = 0;
            rtdeliver_gone(&w->deliver, i);
            rtshape_reset(&w->shape, i);
        }
    }
}

int rtgcwii_should_read(struct rtgcwii* w, uint32_t now, uint32_t ticks_per_ms) {
    if (w->reading) return 0;
    if (w->game_read != 0) {
        if (rtgcwii_elapsed(now, w->game_read) <= 50u * ticks_per_ms) return 0;
        w->game_read = 0;
    }
    if (w->own_read != 0 && rtgcwii_elapsed(now, w->own_read) < 8u * ticks_per_ms) return 0;
    w->own_read = now | 1u;
    return 1;
}

void rtgcwii_read_by(struct rtgcwii* w, uint32_t now, int own) {
    if (!own) w->game_read = now | 1u;
}

void rtgcwii_classic(struct rtgcwii* w, uint32_t channel, uint32_t* hold, uint32_t* trig, uint32_t* release,
                     int32_t axes[6]) {
    uint32_t i;
    if (!rtgcwii_on(w, channel)) {
        *hold = *trig = *release = 0;
        for (i = 0; i < 6; ++i) axes[i] = 0;
        return;
    }
    *hold = w->hold[channel];
    rtdeliver_take(&w->deliver, channel, w->src.samples[channel], *hold, trig, release);
    {
        struct rtsrc_sample p;
        rtsrc_latest(&w->src, channel, &p);
        for (i = 0; i < 6; ++i) axes[i] = p.axes[i];
    }
}

/* The shape (sideways or with a Nunchuk) whatever the build's: the host
 * tests take each. Inlined into each caller with `sideways` fixed, so a pad
 * build links only its own form's shape (the other is dropped). */
static inline __attribute__((always_inline)) void rtgcwii_shape(struct rtgcwii* w, uint32_t channel, uint32_t now, struct rtshape_out* out, uint32_t takes,
                          int sideways) {
    struct rtsrc_sample p;
    rtshape_rest(out);
    out->hold = out->trig = out->release = 0;
    if (!rtgcwii_on(w, channel)) return;
    out->hold = w->hold[channel];
    if (takes & RTGCWII_TAKE_WORDS)
        rtdeliver_take(&w->deliver, channel, w->src.samples[channel], out->hold, &out->trig, &out->release);
    rtsrc_latest(&w->src, channel, &p);
#ifdef RT_PAD_FORM_POINTING
    (void)sideways;
    (void)now;
    rtshape_pointing(&w->shape, channel, &p, out);
#else
    if (sideways) {
        rtshape_sideways(&w->shape, channel, &p, out);
    } else {
        rtshape_nunchuk(&w->shape, channel, &p, now, out);
    }
#endif
    if (takes & RTGCWII_TAKE_STEP) rtshape_take_step(&w->shape, channel);
}

void rtgcwii_nunchuk(struct rtgcwii* w, uint32_t channel, uint32_t now, struct rtshape_out* out) {
    rtgcwii_shape(w, channel, now, out, RTGCWII_TAKE_WORDS | RTGCWII_TAKE_STEP, 0);
}

void rtgcwii_sideways(struct rtgcwii* w, uint32_t channel, struct rtshape_out* out) {
    rtgcwii_shape(w, channel, 0, out, RTGCWII_TAKE_WORDS | RTGCWII_TAKE_STEP, 1);
}

#ifdef RT_PAD_FORM_POINTING
void rtgcwii_pointing(struct rtgcwii* w, uint32_t channel, struct rtshape_out* out) {
    rtgcwii_shape(w, channel, 0, out, RTGCWII_TAKE_WORDS | RTGCWII_TAKE_STEP, 0);
}
#endif

void rtgcwii_wii_remote(struct rtgcwii* w, uint32_t channel, uint32_t now, struct rtshape_out* out, uint32_t takes) {
#if defined(RT_PAD_FORM_SIDEWAYS) || defined(RT_PAD_FORM_POINTING)
    rtgcwii_shape(w, channel, now, out, takes, 1);
#else
    rtgcwii_shape(w, channel, now, out, takes, 0);
#endif
}

void rtgcwii_camera(struct rtgcwii* w, uint32_t channel, uint32_t mode) { rtshape_camera(&w->shape, channel, mode); }

uint32_t rtgcwii_hold(const struct rtgcwii* w, uint32_t channel) {
    return rtgcwii_on(w, channel) ? w->hold[channel] : 0u;
}

#if defined(RT_PAD_FORM_NUNCHUK) || defined(RT_PAD_FORM_SIDEWAYS) || defined(RT_PAD_FORM_POINTING)
/* The raw report on `channel` in data format `report_format` (its length),
 * the camera as the game set it (KPAD's switch, `kpad_camera`, or the format
 * the game asked for). */
static uint32_t rtgcwii_report(struct rtgcwii* w, uint32_t channel, uint8_t* out, uint32_t now, int kpad_camera,
                               uint32_t report_format) {
    struct rtshape_out f;
    if (channel >= RTGCWII_CHANNELS || !rtgcwii_on(w, channel)) return 0;
    rtgcwii_camera(w, channel, kpad_camera || rtdeliver_format_has_camera(w->format[channel]));
    /* A look: the report has only the buttons held, so what the game's KPAD
     * reads were last given (and what is new at its next one) and the
     * shake's step stay for that read, as for the HOME Menu's stand-in. */
    rtgcwii_wii_remote(w, channel, now, &f, 0);
#ifdef RT_PAD_FORM_NUNCHUK
    return rtdeliver_wpad(&f, report_format, (int)((w->shape.camera >> channel) & 1u), 1, out);
#else
    return rtdeliver_wpad(&f, report_format, (int)((w->shape.camera >> channel) & 1u), 0, out);
#endif
}

uint32_t rtgcwii_wpad_status(struct rtgcwii* w, uint32_t channel, uint8_t* out, uint32_t now, int kpad_camera) {
    /* WPADRead copies the format the game asked for: exactly that long. */
    return rtgcwii_report(w, channel, out, now, kpad_camera, channel < RTGCWII_CHANNELS ? w->format[channel] : 0u);
}

uint32_t rtgcwii_unified(struct rtgcwii* w, uint32_t channel, uint8_t* out, uint32_t entry_bytes, uint32_t count,
                         uint32_t now, uint32_t sample_ticks, int kpad_camera) {
    uint8_t first[RTGCWII_WPAD_FS_BYTES];
    uint32_t i, k, n, report_format;
    if (channel >= RTGCWII_CHANNELS || entry_bytes < RTGCWII_WPAD_FS_BYTES + 2u || count == 0) return 0;
    {
        /* KPAD's raw samples: a real Wii Remote + Nunchuk's always carry the
         * Nunchuk's part (KPAD sets a Nunchuk data format itself once one is
         * plugged in), and the entries say so, so the Nunchuk form gives it
         * whatever format the stand-in saw the game ask for. Donkey Kong
         * Country Returns never asks, and read the Nunchuk's part as zeros
         * (a hard pull, never a shake): its Nunchuk play never started. */
        report_format = w->format[channel];
#ifdef RT_PAD_FORM_NUNCHUK
        if (report_format < RTDELIVER_FMT_FS_FIRST || report_format > RTDELIVER_FMT_FS_ACC_DPD)
            report_format = RTDELIVER_FMT_FS_ACC_DPD;
#endif
        n = rtgcwii_report(w, channel, first, now, kpad_camera, report_format);
    }
    if (n == 0) return 0;
    for (i = 0; i < count; ++i) {
        uint8_t* e = out + i * entry_bytes;
        for (k = 0; k < entry_bytes; ++k) e[k] = 0;
        for (k = 0; k < n; ++k) e[k] = first[k];
        e[entry_bytes - 2u] = (uint8_t)report_format;  /* the format the entry is in (CodeRabbit CLI) */
        /* The shake at each sample's own time: a step every shake_flip
         * ticks from when it began; a sample from before it, at rest. */
        {
            int32_t acc[3];
            const int shaking = rtshape_shake_at(&w->shape, channel, now, i * sample_ticks, acc);
            if (shaking != 0) rtdeliver_put_acc(e + 2, acc);
            /* The Nunchuk's part, where the sample has it: zeros, except
             * while the shake swings it with the Wii Remote (rtshape_nunchuk).
             * The stand-in's slot never had a real Nunchuk, so the game has
             * no calibration for one: any other steady value reads as the
             * Nunchuk moving. Batman: The Brave and the Bold's heavy attack
             * (a Wii Remote shake) fails against a steady 1 g there, and
             * Donkey Kong Country Returns' Nunchuk play still needs the
             * swing (Dolphin, 25 Nov). */
#ifdef RT_PAD_FORM_NUNCHUK
            if (n == RTGCWII_WPAD_FS_BYTES) {
                for (k = 42; k < 48; ++k) e[k] = 0;
                if (shaking == 2) rtdeliver_put_acc(e + 42, acc);
            }
#endif
        }
    }
    return count;
}
#else
uint32_t rtgcwii_unified(struct rtgcwii* w, uint32_t channel, uint8_t* out, uint32_t entry_bytes, uint32_t count,
                         uint32_t now, uint32_t sample_ticks, int kpad_camera) {
    (void)w;
    (void)channel;
    (void)out;
    (void)entry_bytes;
    (void)count;
    (void)now;
    (void)sample_ticks;
    (void)kpad_camera;
    return 0;
}
uint32_t rtgcwii_wpad_status(struct rtgcwii* w, uint32_t channel, uint8_t* out, uint32_t now, int kpad_camera) {
    (void)w;
    (void)channel;
    (void)out;
    (void)now;
    (void)kpad_camera;
    return 0;
}
#endif
