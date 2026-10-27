/* SPDX-FileCopyrightText: 2026 RiftWii contributors */
/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The HOME Menu from a GameCube controller's decisions (rthome.h). */
#include "rthome.h"

/* PADStatus: buttons (big-endian), stick x/y, C-stick x/y, analog L/R,
 * analog A/B, the error byte. Its button bits (the SDK's PAD_BUTTON_*). */
#define RTHOME_PAD_STATUS_BYTES 12u
#define RTHOME_PAD_ERR 10u
#define RTHOME_GC_LEFT 0x0001u
#define RTHOME_GC_RIGHT 0x0002u
#define RTHOME_GC_DOWN 0x0004u
#define RTHOME_GC_UP 0x0008u
#define RTHOME_GC_Z 0x0010u
#define RTHOME_GC_R 0x0020u
#define RTHOME_GC_L 0x0040u
#define RTHOME_GC_A 0x0100u
#define RTHOME_GC_B 0x0200u
#define RTHOME_GC_X 0x0400u
#define RTHOME_GC_Y 0x0800u
#define RTHOME_GC_START 0x1000u
#define RTHOME_COMBO (RTHOME_GC_START | RTHOME_GC_L | RTHOME_GC_R | RTHOME_GC_Z)

/* A GameCube stick rests a few units off centre, which the HOME Menu's
 * pointer creeps on (the Kamen Rider: Super Climax Heroes pad pack, on
 * hardware, 19 Oct): inside this it reads 0. Then full tilt, and a fully
 * pressed analog trigger. */
#define RTHOME_STICK_DEAD 10
#define RTHOME_STICK_FULL 72
#define RTHOME_CSTICK_FULL 59
#define RTHOME_TRIGGER_FULL 180

static uint32_t pad_buttons(const uint8_t* s) { return ((uint32_t)s[0] << 8) | s[1]; }

/* Ticks since a time noted with its low bit set (so 0 can mean "none"):
 * the bit comes off first, or a time noted at an even tick would be one
 * tick in the future, and the unsigned difference would wrap to the
 * largest there is (the stand-in ended at once in Dolphin). */
static uint32_t elapsed(uint32_t now, uint32_t noted) { return now - (noted & ~1u); }

/* The first port, with no error, holding the combo; RTHOME_CHANNELS: none. */
static uint32_t combo_port(const uint8_t* status) {
    uint32_t i;
    for (i = 0; i < RTHOME_CHANNELS; ++i) {
        const uint8_t* s = status + i * RTHOME_PAD_STATUS_BYTES;
        if ((int8_t)s[RTHOME_PAD_ERR] == 0 && (pad_buttons(s) & RTHOME_COMBO) == RTHOME_COMBO) return i;
    }
    return RTHOME_CHANNELS;
}

/* The lowest empty channel, else the lowest the game never asked about. */
static uint32_t free_channel(const struct rthome* h) {
    uint32_t i;
    for (i = 0; i < RTHOME_CHANNELS; ++i)
        if (((h->probed & h->free) >> i) & 1u) return i;
    for (i = 0; i < RTHOME_CHANNELS; ++i)
        if (!((h->probed >> i) & 1u)) return i;
    return RTHOME_NO_CHANNEL;
}

static void keep_pad(struct rthome* h, const uint8_t* status) {
    const uint8_t* s = status + h->port * RTHOME_PAD_STATUS_BYTES;
    uint32_t i;
    for (i = 0; i < RTHOME_PAD_STATUS_BYTES; ++i) h->pad[i] = s[i];
    h->stale &= pad_buttons(s);  /* a combo button once let go counts again */
    ++h->samples;
}

void rthome_pads(struct rthome* h, const uint8_t* status, uint32_t now) {
    const uint32_t port = combo_port(status);
    if (h->phase == RTHOME_DONE) {
        if (port == RTHOME_CHANNELS) {
            h->phase = RTHOME_IDLE;
            h->since = 0;
        }
        return;
    }
    if (h->phase != RTHOME_IDLE) {
        keep_pad(h, status);
        return;
    }
    if (port == RTHOME_CHANNELS) {
        h->since = 0;
        return;
    }
    if (h->since == 0) {
        h->since = now | 1u;
        return;
    }
    if (elapsed(now, h->since) < h->hold) return;
    h->channel = free_channel(h);
    if (h->channel == RTHOME_NO_CHANNEL) {
        h->phase = RTHOME_DONE;  /* every channel has a controller: its own HOME works */
        return;
    }
    h->port = port;
    h->stale = pad_buttons(status + port * RTHOME_PAD_STATUS_BYTES);
    h->last = 0;
    h->samples = 0;
    h->worked = 0xFFFFFFFFu;  /* none yet */
    h->aside_home = 0;
    h->selected = RTHOME_NO_SELECTION;
    h->menu_seen = 0;
    h->phase = RTHOME_PRESSING;
    h->since = now | 1u;
    keep_pad(h, status);
}

void rthome_probed(struct rthome* h, uint32_t channel, int32_t result) {
    uint32_t bit;
    if (channel >= RTHOME_CHANNELS) return;
    bit = 1u << channel;
    h->probed |= bit;
    if (result == -1)
        h->free |= bit;
    else
        h->free &= ~bit;
}

void rthome_menu_frame(struct rthome* h, uint32_t now) {
    h->menu_seen = now | 1u;
    if (h->phase == RTHOME_ASIDE) h->selected = RTHOME_NO_SELECTION;  /* back in the menu from that screen */
    if (h->phase == RTHOME_WAITING || h->phase == RTHOME_ASIDE) h->phase = RTHOME_OPEN;
    h->aside_home = 0;
}

void rthome_selected(struct rthome* h, int32_t button) { h->selected = (uint32_t)button; }

uint32_t rthome_stand_in(struct rthome* h, uint32_t now) {
    if (h->phase == RTHOME_PRESSING && elapsed(now, h->since) >= h->press) {
        h->phase = h->menu_seen != 0 ? RTHOME_OPEN : RTHOME_WAITING;
        h->since = now | 1u;
    }
    if (h->phase == RTHOME_WAITING && elapsed(now, h->since) >= h->wait) h->phase = RTHOME_DONE;  /* the game refused */
    if (h->phase == RTHOME_OPEN && elapsed(now, h->menu_seen) >= h->linger) {
        /* Closed (HBMGetSelectBtnNum answered 0, for Close and for HOME),
         * or gone to a screen of its own (any other answer: the Operations
         * Guide; Wii Menu and Reset leave the game anyway), which HOME
         * closes in its turn. No answer seen: closed. */
        const int closed = h->selected == RTHOME_NO_SELECTION || h->selected == 0;
        h->phase = closed ? RTHOME_DONE : RTHOME_ASIDE;
        h->aside_home = 0;
    }
    if (h->phase == RTHOME_ASIDE && h->aside_home != 0 && elapsed(now, h->aside_home) >= h->wait)
        h->phase = RTHOME_DONE;  /* HOME closed that screen, and the menu didn't come back */
    if (h->phase == RTHOME_PRESSING || h->phase == RTHOME_WAITING || h->phase == RTHOME_OPEN ||
        h->phase == RTHOME_ASIDE)
        return h->channel;
    return RTHOME_NO_CHANNEL;
}

int rthome_menu_open(const struct rthome* h) { return h->phase == RTHOME_OPEN; }

/* A stick's reading as a fraction of RTHOME_AXIS_FULL, past the dead zone. */
static int32_t stick(int32_t v, int32_t full) {
    int32_t out;
    if (v > -RTHOME_STICK_DEAD && v < RTHOME_STICK_DEAD) return 0;
    v += v > 0 ? -RTHOME_STICK_DEAD : RTHOME_STICK_DEAD;
    out = v * RTHOME_AXIS_FULL / (full - RTHOME_STICK_DEAD);
    if (out > RTHOME_AXIS_FULL) return RTHOME_AXIS_FULL;
    if (out < -RTHOME_AXIS_FULL) return -RTHOME_AXIS_FULL;
    return out;
}

static int32_t trigger(uint32_t v) {
    const int32_t out = (int32_t)v * RTHOME_AXIS_FULL / RTHOME_TRIGGER_FULL;
    return out > RTHOME_AXIS_FULL ? RTHOME_AXIS_FULL : out;
}

static uint32_t mapped(uint32_t b) {
    uint32_t c = 0;
    if (b & RTHOME_GC_A) c |= RTHOME_CL_A;
    if (b & RTHOME_GC_B) c |= RTHOME_CL_B;
    if (b & RTHOME_GC_X) c |= RTHOME_CL_X;
    if (b & RTHOME_GC_Y) c |= RTHOME_CL_Y;
    if (b & RTHOME_GC_START) c |= RTHOME_CL_HOME;
    if (b & RTHOME_GC_Z) c |= RTHOME_CL_ZR;
    if (b & RTHOME_GC_L) c |= RTHOME_CL_L;
    if (b & RTHOME_GC_R) c |= RTHOME_CL_R;
    if (b & RTHOME_GC_UP) c |= RTHOME_CL_UP;
    if (b & RTHOME_GC_DOWN) c |= RTHOME_CL_DOWN;
    if (b & RTHOME_GC_LEFT) c |= RTHOME_CL_LEFT;
    if (b & RTHOME_GC_RIGHT) c |= RTHOME_CL_RIGHT;
    return c;
}

void rthome_classic(struct rthome* h, uint32_t now, uint32_t* hold, uint32_t* trig, uint32_t* release,
                    int32_t axes[6]) {
    uint32_t i, buttons;
    if (h->phase == RTHOME_PRESSING) {
        buttons = RTHOME_CL_HOME;
        for (i = 0; i < 6; ++i) axes[i] = 0;
    } else {
        buttons = mapped(pad_buttons(h->pad) & ~h->stale);
        axes[0] = stick((int8_t)h->pad[2], RTHOME_STICK_FULL);
        axes[1] = stick((int8_t)h->pad[3], RTHOME_STICK_FULL);
        axes[2] = stick((int8_t)h->pad[4], RTHOME_CSTICK_FULL);
        axes[3] = stick((int8_t)h->pad[5], RTHOME_CSTICK_FULL);
        axes[4] = trigger(h->pad[6]);
        axes[5] = trigger(h->pad[7]);
    }
    if (h->worked != h->samples) {
        h->worked = h->samples;
        h->hold_now = buttons;
        h->trig_now = buttons & ~h->last;
        h->release_now = h->last & ~buttons;
        h->last = buttons;
        if (h->phase == RTHOME_ASIDE && (h->trig_now & RTHOME_CL_HOME)) h->aside_home = now | 1u;
    }
    *hold = h->hold_now;
    *trig = h->trig_now;
    *release = h->release_now;
}

uint32_t rthome_float_bits(int32_t numerator) {
    uint32_t sign = 0, magnitude, top = 0;
    if (numerator == 0) return 0;
    if (numerator < 0) {
        sign = 0x80000000u;
        numerator = -numerator;
    }
    magnitude = (uint32_t)(numerator > RTHOME_AXIS_FULL ? RTHOME_AXIS_FULL : numerator);
    while ((magnitude >> (top + 1)) != 0) ++top;  /* its highest bit */
    /* magnitude / 256 = 1.mantissa * 2^(top - 8) */
    return sign | ((127u + top - 8u) << 23) | ((magnitude << (23u - top)) & 0x007FFFFFu);
}
