#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes Wave, a RiftWii menu theme: a deep blue wall with a slow ribbon of
fine light lines flowing across it and sparkles twinkling along it, behind
frosted, see-through panels; whatever is lit glows white.

Every picture, sound and the music are worked out here from arithmetic
alone (no photo, texture, screenshot, recording or picture from anywhere
else):

- backdrop_00.png ... : the looping wall, 320x180 frames that RiftWii scales
  to cover the screen. Every motion is a whole number of turns per loop, so
  the last frame runs into the first without a seam;
- background.png / background_wide.png: the same wall, still, at full size,
  for RiftWii versions without looping backdrops;
- bar.png / bar_wide.png: Home's bottom bar, a dark see-through strip with a
  fine light edge, dipping under the clock (the curve RiftWii paints);
- tiles, cover cards, the game page's and Settings' tiles, the wide buttons,
  the HOME Menu's buttons, Home's round buttons, option chips, the lit list
  row, the option and scroll arrows, switches, panels, the popup, the
  banner's stripes, Home's page arrows, the shelf and the blank cover: dark
  frosted glass with a fine light rim; lit, a brighter blue with a white
  glow around it;
- music.ogg: a slow ambient pad (soft chords, a quiet shimmer), synthesised
  here and encoded with ffmpeg's Vorbis encoder;
- hover.wav, click.wav, home_open.wav, home_close.wav: soft synthesised
  ticks and chimes;
- theme.ini and LICENSE.

    py -3 tools/make_wave_theme.py [out_dir [picture ...]]   (needs numpy, Pillow, ffmpeg)

The default out_dir is build-themes/Wave in the repository; the folder goes
to the community themes repository (RiftWii/themes, themes/Wave) as it is.
A whole build needs a new or empty folder, so nothing from an older
build is left in the theme; naming pictures (or "music", "sounds",
"loop") repaints only those in place, and an unknown name stops the run.
"""
import math
import pathlib
import subprocess
import sys
import tempfile

import numpy as np

import themepaint as tp

HERE = pathlib.Path(__file__).resolve().parent

# The loop: 36 frames of 320x180 at 8 a second, 4.5 seconds, 4.0 MiB.
LOOP_W, LOOP_H, LOOP_FRAMES, LOOP_FPS = 320, 180, 36, 8

# The palette.
WALL_TOP = (34, 78, 168)
WALL_MID = (14, 44, 120)
WALL_BOTTOM = (5, 16, 54)
LINE = (232, 242, 255)
GLASS_TOP = (30, 58, 118)
GLASS_BOTTOM = (10, 26, 66)
LIT_TOP = (78, 132, 220)
LIT_BOTTOM = (34, 76, 168)
GLOW = (190, 222, 255)
RIM = (214, 230, 255)

INI = """[theme]
name = Wave
author = RiftWii contributors

[shape]
# Softly rounded frosted glass, flat, lit by a white glow.
corners = 0.8
gloss = no
bar = dip
shelf_grain = no
home_menu = wii

[backdrop]
# backdrop_00.png to backdrop_35.png: the ribbon of light flows and the
# sparkles twinkle, 4.5 seconds a loop. RiftWii versions without looping
# backdrops show background.png.
stripes = no
frames = 36
fps = 8

[colors]
# Text: white, and pale blues for the quieter levels
ink = #FFFFFF
ink_soft = #D5E3F8
ink_dim = #A8BEE2
clock = #EEF4FF
# Light blue marks what is lit; its text is dark
accent = #8CC8FF
accent_ink = #BFE1FF
text_on_accent = #061633
warn = #FF8A80

# Frosted glass tiles, buttons and panels
card = #12295C
card_edge = #6E8FC8
card_edge_strong = #A9C3EC
shadow = #00000070
glow = #CFE6FF70
glyph = #F2F7FF

# Option chips and switches
chip_on = #2D5AA8
chip_off = #13295A
chip_off_edge = #4D6CA6
switch_off = #2A3F6E

# Background and bars
bar = #0B1D48
backdrop = #0E2C76
backdrop_stripe = #0E2C76
banner_stripe = #FFFFFF10
# The games' own colours, a little toward the wall's blue
banner_tint = #0A246040

# Lists and badges
divider = #2C4880
scroll_track = #0C1F4A
scroll_thumb = #8AA8DA
badge = #23407C
shelf = #3A64B4
shelf_edge = #1C3A7A

# Player pointers
pointer1 = #3B8FD6
pointer2 = #E05252
pointer3 = #48B858
pointer4 = #E5AC24
"""

LICENSE = """License: CC0-1.0
Author: RiftWii contributors

Wave, a theme for RiftWii: a deep blue wall with a slow ribbon of fine
light lines and twinkling sparkles flowing across it, behind frosted glass
panels that glow white when lit.

To the extent possible under law, the author has waived all copyright and
related or neighboring rights to this theme (theme.ini, every PNG picture,
music.ogg and the WAV sounds in its folder), under Creative Commons CC0 1.0
Universal: https://creativecommons.org/publicdomain/zero/1.0/

The pictures, including the looping backdrop's 36 frames, the music and the
sounds were made by RiftWii's tools/make_wave_theme.py
(https://github.com/KakarottoCake/Riftwii) from arithmetic alone: no source
images, textures, fonts, recordings or samples.

Third-party parts: none.
"""

TAU = 2 * math.pi


# --- the wall ----------------------------------------------------------------

def wall(W, H, t, u0=0.0, u1=1.0, scale=None):
    """The wall at loop time t (0..1) as an opaque float canvas. u0..u1 is
    the part of the 16:9 picture shown across W (a 4:3 picture is its
    middle). `scale` is pixels per loop pixel (line widths and sparkles)."""
    if scale is None:
        scale = H / LOOP_H
    x, y = tp.grid(W, H)
    u = u0 + (u1 - u0) * x / W
    v = y / H
    c = tp.canvas(W, H)
    # The gradient: lighter high up, deep at the foot, a broad brighter
    # haze up and to the left.
    base = np.where((v < 0.45)[..., None], tp.mix(WALL_TOP, WALL_MID, tp.smooth(v / 0.45)),
                    tp.mix(WALL_MID, WALL_BOTTOM, tp.smooth((v - 0.45) / 0.55)))
    haze = np.exp(-(((u - 0.22) / 0.55) ** 2 + ((v - 0.05) / 0.55) ** 2))
    base = tp.mix(base, (70, 120, 210), 0.35 * haze)
    # A darker corner at the bottom right, as a vignette.
    vig = tp.smooth(((u - 0.5) / 0.75) ** 2 + ((v - 0.42) / 0.75) ** 2)
    base = tp.mix(base, (2, 8, 30), 0.45 * vig)
    c[..., :3] = base
    c[..., 3] = 1.0

    ph = TAU * t
    # The ribbon's middle line and its width, each a sum of slow waves:
    # the long one only breathes, the shorter ones travel (a whole number of
    # wavelengths a loop, so the loop has no seam).
    def middle(uu):
        return (0.60 + 0.075 * np.sin(TAU * 0.55 * uu + 0.9) * (1 + 0.16 * np.sin(ph))
                + 0.026 * np.sin(TAU * 1.45 * uu - ph + 0.3) + 0.010 * np.sin(TAU * 2.9 * uu + 2 * ph))

    def spread(uu):
        return (0.105 * np.sin(TAU * 0.72 * uu + 2.0 + 0.55 * np.sin(ph)) + 0.018 * np.sin(TAU * 1.9 * uu - ph))

    m = middle(u)
    s = spread(u)
    # The sheet: a soft veil between the ribbon's edges, and a wide glow.
    top = m - np.abs(s)
    bot = m + np.abs(s)
    inside = tp.smooth((v - top) * H / (3 * scale) + 0.5) * tp.smooth((bot - v) * H / (3 * scale) + 0.5)
    tp.add_light(c, (150, 200, 255), 0.075 * inside)
    tp.add_light(c, (120, 170, 245), 0.16 * np.exp(-((v - m) / 0.12) ** 2))
    # The lines: each one's place in the ribbon, its own small wobble, a
    # fine bright core and a faint halo.
    n = 44
    for i in range(n):
        f = i / (n - 1) - 0.5
        # Not a comb: the lines fan out unevenly (a bow across the ribbon),
        # and each wobbles at its own pace.
        bow = 0.035 * (4 * f * f - 1) * np.sin(TAU * 0.9 * u + 1.3 + ph)
        yi = (m + 2 * f * s + bow * np.abs(s) / 0.1
              + 0.007 * np.sin(TAU * (1.8 + 0.9 * (i % 5) / 4) * u + i * 0.61 + ph * (1 + i % 2)))
        d = (v - yi) * H / scale  # in loop pixels
        bright = 0.07 + 0.19 * (1 - abs(2 * f)) ** 1.5
        # Brighter where the ribbon is wide open, fading where it twists.
        bright = bright * (0.55 + 0.45 * np.clip(np.abs(s) / 0.10, 0, 1))
        core = np.exp(-(d / 0.42) ** 2)
        halo = np.exp(-(d / 1.6) ** 2) * 0.25
        tp.add_light(c, LINE, bright * (core + halo))
    # A second, fainter ribbon a little above, its own slow course.
    m2 = (0.50 + 0.06 * np.sin(TAU * 0.62 * u + 2.6) * (1 + 0.2 * np.sin(ph + 1.0))
          + 0.02 * np.sin(TAU * 1.3 * u + ph + 1.7))
    s2 = 0.045 * np.sin(TAU * 0.8 * u + 0.4 - 0.5 * np.sin(ph))
    for i in range(14):
        f = i / 13 - 0.5
        yi = m2 + 2 * f * s2 + 0.004 * np.sin(TAU * 2.6 * u + i + ph)
        d = (v - yi) * H / scale
        tp.add_light(c, LINE, (0.05 + 0.07 * (1 - abs(2 * f))) * np.exp(-(d / 0.45) ** 2))
    # Sparkles along the ribbon: each twinkles a whole number of times a
    # loop and drifts a little back and forth.
    rng = np.random.default_rng(2611)
    count = 110
    su = rng.random(count) * 1.1 - 0.05
    sv = rng.normal(0, 0.55, count)
    kk = rng.integers(1, 3, count)
    phase = rng.random(count) * TAU
    size = 0.55 + rng.random(count) * 0.75
    for j in range(count):
        uj = su[j] + 0.012 * math.sin(ph + phase[j])
        if not (u0 - 0.02 <= uj <= u1 + 0.02):
            continue
        mj = float(middle(np.float32(uj)))
        vj = mj + sv[j] * abs(float(spread(np.float32(uj)))) * 1.2 + 0.008 * math.cos(ph + phase[j])
        tw = (0.5 + 0.5 * math.sin(kk[j] * ph + phase[j])) ** 3
        if tw < 0.02:
            continue
        px = (uj - u0) / (u1 - u0) * W
        py = vj * H
        # Only the few pixels round the sparkle.
        reach = int(math.ceil(4 * size[j] * scale)) + 1
        xa, xb = max(0, int(px) - reach), min(W, int(px) + reach + 1)
        ya, yb = max(0, int(py) - reach), min(H, int(py) + reach + 1)
        if xa >= xb or ya >= yb:
            continue
        win = c[ya:yb, xa:xb]
        r2 = ((x[ya:yb, xa:xb] - px) ** 2 + (y[ya:yb, xa:xb] - py) ** 2) / (scale * scale)
        tp.add_light(win, (255, 255, 255), 0.85 * tw * np.exp(-r2 / (size[j] ** 2)))
    return c


def wall_smooth(W, H, t, u0=0.0, u1=1.0, ss=3):
    """The wall drawn at ss times the size and averaged down, so the fine
    lines stay smooth at the loop's small size."""
    big = wall(W * ss, H * ss, t, u0, u1, scale=H * ss / LOOP_H)
    small = big[..., :3].reshape(H, ss, W, ss, 3).mean(axis=(1, 3))
    out = tp.canvas(W, H)
    out[..., :3] = small
    out[..., 3] = 1.0
    return out


def loop_frames(out):
    paths = []
    for k in range(LOOP_FRAMES):
        img = tp.to_image(wall_smooth(LOOP_W, LOOP_H, k / LOOP_FRAMES), seed=100 + k, opaque=True)
        paths.append(tp.save(img, out / f"backdrop_{k:02d}.png"))
    return paths


def background(W):
    # The 16:9 picture's middle on 4:3; the whole of it on 16:9.
    span = 0.75 * (640 / W) if W == 640 else 1.0
    u0 = 0.5 - span / 2
    return tp.to_image(wall(W, 480, 0.0, u0, u0 + span, scale=480 / LOOP_H), opaque=True)


# --- frosted glass -------------------------------------------------------------

def glass(cw, ch, x0, y0, w, h, r, *, lit=False, primary=False, alpha=0.62, sheen=0.10, rim=0.42,
          glow_size=None, shadow=0.45, circle=False, bands=None, flat_top=False, pale=False):
    """A frosted glass shape in a cw x ch canvas: a soft shadow, a dark
    translucent body (lighter at the top), a faint sheen on its upper half
    and a fine light rim; lit, a brighter blue body, a bright rim and a
    white glow around it; `primary` a little bluer."""
    x, y = tp.grid(cw, ch)
    if circle:
        d = tp.circle(x, y, x0 + w / 2, y0 + h / 2, w / 2)
    else:
        d, _, _ = tp.rrect(x, y, x0, y0, w, h, r)
    cov = tp.coverage(d)
    c = tp.canvas(cw, ch)
    if shadow > 0:
        sh = tp.shadow_of(cov, 2, shadow, dy=2)
        tp.over(c, (0, 0, 0), sh[..., 3])
    if lit:
        g = tp.canvas(cw, ch)
        g[..., :3] = GLOW
        g[..., 3] = cov
        g = tp.blur(g, tp.fit_radius(cov, glow_size or 3))
        tp.over(c, GLOW, g[..., 3] * 0.9)
    v = np.clip((y - y0) / h, 0, 1)
    if pale:
        # Pale frosted glass, for buttons whose labels RiftWii draws dark
        # (the HOME Menu's); lit, almost white.
        body = tp.mix((250, 252, 255), (206, 222, 246), tp.smooth(v)) if lit else \
            tp.mix((226, 234, 248), (166, 186, 220), tp.smooth(v))
        a = 0.97
    elif lit:
        body = tp.mix(LIT_TOP, LIT_BOTTOM, tp.smooth(v))
        a = 0.88
    elif primary:
        body = tp.mix((44, 86, 170), (18, 44, 110), tp.smooth(v))
        a = alpha + 0.12
    else:
        body = tp.mix(GLASS_TOP, GLASS_BOTTOM, tp.smooth(v))
        a = alpha
    if bands is not None:
        band_rgb, k, ba = bands(v, y - y0)
        body = tp.mix(body, band_rgb, k)
        a = np.maximum(a, ba)
    # The sheen: the upper half a shade lighter, fading downwards.
    body = tp.mix(body, (255, 255, 255), (sheen * (1.6 if lit else 1.0)) * (1 - tp.smooth(v / 0.55)))
    tp.over(c, body, cov * a)
    # The rim: a fine light line just inside the edge, brightest at the top.
    rim_k = np.clip(1.0 - np.abs(d - 0.9) / 0.9, 0, 1) * cov
    rim_a = (0.95 if lit else rim) * (1.0 - 0.45 * v)
    tp.over(c, (255, 255, 255) if lit else RIM, rim_k * rim_a)
    if primary and not lit:
        ring = np.clip(1.0 - np.abs(d - 1.2) / 1.2, 0, 1) * cov
        tp.over(c, (140, 200, 255), ring * 0.85)
    return c


def img(c):
    return tp.to_image(c)


# --- the pictures --------------------------------------------------------------

def bar(W):
    """W x 124: a dark see-through strip with a fine light top edge and a
    soft glow under it, dipping in the middle under the clock."""
    H = 124
    tops = tp.bar_tops(W)
    x, y = tp.grid(W, H)
    d = y - tops[None, :]
    c = tp.canvas(W, H)
    # A soft shadow just above the edge.
    tp.over(c, (0, 0, 0), 0.30 * np.exp(np.minimum(d, 0) / 3.0) * (d < 0.5))
    k = np.clip((y - 11) / (H - 11), 0, 1)
    body = tp.mix((14, 34, 86), (4, 12, 36), tp.smooth(k))
    body = tp.mix(body, (160, 200, 255), 0.18 * np.exp(-np.maximum(d, 0) / 7.0))
    tp.over(c, body, tp.coverage(d) * 0.80)
    edge = np.clip(1.0 - np.abs(d - 0.8) / 1.1, 0, 1)
    tp.over(c, RIM, edge * 0.85)
    return tp.to_image(c)


def tile(lit=False):
    return img(glass(140, 100, 8, 8, 124, 84, 9, lit=lit))


def tile_empty():
    c = tp.canvas(140, 100)
    x, y = tp.grid(140, 100)
    d, _, _ = tp.rrect(x, y, 8, 8, 124, 84, 9)
    cov = tp.coverage(d)
    tp.over(c, (6, 18, 48), cov * 0.30)
    tp.over(c, RIM, np.clip(1.0 - np.abs(d - 0.8) / 0.9, 0, 1) * cov * 0.22)
    # A faint ribbon across the empty place: three light lines.
    for i in range(3):
        yy = 50 + 6 * np.sin((x - 8) / 124 * TAU * 0.8 + i * 0.6) + (i - 1) * 4
        tp.over(c, (210, 228, 255), np.exp(-((y - yy) / 0.7) ** 2) * cov * 0.22)
    return tp.to_image(c)


def cover_tile(lit=False):
    if lit:
        return img(glass(96, 128, 5, 5, 86, 118, 7, lit=True, glow_size=2))
    return img(glass(96, 128, 8, 8, 80, 112, 6))


def tile9(lit=False, primary=False):
    return img(glass(64, 64, 8, 8, 48, 48, 10, lit=lit, primary=primary))


def pill(lit=False, primary=False):
    return img(glass(252, 60, 4, 4, 244, 52, 26, lit=lit, primary=primary, glow_size=2, shadow=0.35))


def home_button(lit=False):
    # The HOME Menu's buttons: a paler frosted glass, so they read as
    # buttons on the menu's black bars.
    # The HOME Menu's buttons: pale frosted glass, as RiftWii draws their
    # labels in dark ink; lit, white with the white glow.
    c = glass(264, 88, 8, 8, 248, 72, 36, lit=lit, pale=True, sheen=0.22, rim=0.9, glow_size=4)
    return img(c)


def round_button(lit=False):
    return img(glass(80, 80, 3, 3, 74, 74, 37, lit=lit, circle=True, glow_size=2, shadow=0.35))


def chip(on):
    c = glass(212, 36, 2, 2, 208, 30, 9, primary=on, alpha=0.55, shadow=0.25)
    return img(c)


def row_focus():
    """548x44: the lit list row, as a lit item's light: a bright band of
    white-blue light fading at both ends, a fine bright rim."""
    cw, ch = 548, 44
    x, y = tp.grid(cw, ch)
    d, _, _ = tp.rrect(x, y, 1, 1, 546, 42, 10)
    cov = tp.coverage(d)
    c = tp.canvas(cw, ch)
    u = (x - 1) / 546
    v = (y - 1) / 42
    fade = 0.55 + 0.45 * np.clip(np.sin(np.clip(u, 0, 1) * math.pi), 0, 1) ** 0.6
    body = tp.mix((86, 138, 222), (40, 84, 170), tp.smooth(v))
    body = tp.mix(body, (255, 255, 255), 0.16 * (1 - tp.smooth(v / 0.5)))
    tp.over(c, body, cov * 0.78 * fade)
    rim = np.clip(1.0 - np.abs(d - 0.9) / 0.9, 0, 1) * cov
    tp.over(c, (255, 255, 255), rim * 0.9 * fade)
    return tp.to_image(c)


def round_arrow(direction, lit):
    """44x44: a round glass button with a chevron (the option's arrows and a
    list's scroll arrows)."""
    c = glass(44, 44, 4, 4, 34, 34, 17, lit=lit, circle=True, glow_size=2, shadow=0.3)
    tp.chevron(c, 22, 21.5 if direction in ("left", "right") else 22, 13, direction, width=2.6)
    return img(c)


def switch(on):
    """68x40: a track with a knob, on to the right in light blue."""
    cw, ch = 68, 40
    x, y = tp.grid(cw, ch)
    c = tp.canvas(cw, ch)
    d, _, _ = tp.rrect(x, y, 4, 6, 60, 28, 14)
    cov = tp.coverage(d)
    tp.over(c, (0, 0, 0), tp.shadow_of(cov, 2, 0.35)[..., 3])
    v = np.clip((y - 6) / 28, 0, 1)
    if on:
        body = tp.mix((120, 186, 255), (52, 120, 220), tp.smooth(v))
        tp.over(c, body, cov * 0.95)
    else:
        body = tp.mix((24, 44, 92), (10, 24, 58), tp.smooth(v))
        tp.over(c, body, cov * 0.75)
    tp.over(c, RIM, np.clip(1.0 - np.abs(d - 0.8) / 0.9, 0, 1) * cov * 0.6)
    kx = 50 if on else 18
    kd = tp.circle(x, y, kx, 20, 12)
    kc = tp.coverage(kd)
    tp.over(c, (0, 0, 0), tp.shadow_of(kc, 2, 0.4)[..., 3])
    kv = np.clip((y - 8) / 24, 0, 1)
    tp.over(c, tp.mix((255, 255, 255), (200, 214, 236), tp.smooth(kv)), kc)
    tp.over(c, (255, 255, 255), np.clip(1.0 - np.abs(kd - 0.8) / 0.9, 0, 1) * kc * 0.8)
    return tp.to_image(c)


def panel(h):
    return img(glass(580, h, 4, 4, 572, h - 8, 12, alpha=0.58, sheen=0.06, rim=0.5, shadow=0.4))


def popup():
    def bands(v, yy):
        head = yy < 44
        foot = yy > 276 - 18
        rgb = np.where(head[..., None], np.asarray((26, 58, 124), np.float32),
                       np.where(foot[..., None], np.asarray((18, 42, 96), np.float32),
                                np.asarray((0, 0, 0), np.float32)))
        k = np.where(head | foot, 0.85, 0.0)
        line = (np.abs(yy - 44) < 0.7) | (np.abs(yy - (276 - 18)) < 0.7)
        rgb = np.where(line[..., None], np.asarray(RIM, np.float32), rgb)
        k = np.where(line, 0.6, k)
        return rgb, k, np.where(head | foot, 0.8, 0.70)

    return img(glass(580, 284, 4, 4, 572, 276, 12, alpha=0.70, sheen=0.05, rim=0.55, shadow=0.45, bands=bands))


def banner_stripes():
    """640x192: over the game banner while it shows the game's colour: a
    deep blue veil and three fine light lines of the ribbon across it (plain
    across, so copies laid side by side join). The band is rows 8 to 128."""
    W, H = 640, 192
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    band = (y < 128).astype(np.float32)
    k = tp.smooth(y / 122)
    tp.over(c, tp.mix((10, 30, 84), (24, 62, 150), k), band * (0.18 + 0.30 * k))
    for i, (base, amp) in enumerate(((92, 7), (100, 9), (108, 6))):
        # Whole turns across 640, so the copies join.
        yy = base + amp * np.sin(x / W * TAU * (i + 1) + i)
        tp.over(c, LINE, np.exp(-((y - yy) / 0.8) ** 2) * band * 0.35)
    line = np.abs(y - 122) < 1.0
    tp.over(c, RIM, line * 0.7)
    return tp.to_image(c)


def page_arrow(direction, lit):
    """48x48: Home's triangular page buttons, white glass triangles with a
    glow when lit."""
    c = tp.canvas(48, 48)
    if direction == "left":
        pts = ((34, 8), (13, 24), (34, 40))
    else:
        pts = ((14, 8), (35, 24), (14, 40))
    if lit:
        g = tp.canvas(48, 48)
        tp.triangle(g, pts, GLOW, 1.0)
        g = tp.blur(g, 3)
        tp.over(c, GLOW, g[..., 3])
    shadow = tp.canvas(48, 48)
    tp.triangle(shadow, [(px, py + 2) for px, py in pts], (0, 0, 0), 0.45)
    shadow = tp.blur(shadow, 1)
    tp.over(c, (0, 0, 0), shadow[..., 3])
    tp.triangle(c, pts, (238, 244, 255) if lit else (200, 216, 240), 0.95 if lit else 0.80)
    # A soft sheen on its upper half.
    sheen = tp.canvas(48, 48)
    tp.triangle(sheen, pts, (255, 255, 255), 1.0)
    x, y = tp.grid(48, 48)
    tp.over(c, (255, 255, 255), sheen[..., 3] * 0.25 * (y < 24))
    return tp.to_image(c)


def shelf():
    """256x64: a glass shelf: its top (rows 0-47) a faint blue sheen lit
    towards the front, its front edge (48-63) a bright line over a dark
    strip. Plain across, so it repeats without a seam."""
    W, H = 256, 64
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    top = y < 48
    k = y / 48
    tp.over(c, tp.mix((30, 70, 150), (90, 140, 220), tp.smooth(k)), top * (0.20 + 0.35 * k))
    front = ~top
    fk = (y - 48) / 16
    tp.over(c, tp.mix((120, 170, 240), (10, 28, 70), tp.smooth(fk * 1.4)), front * 0.85)
    tp.over(c, (255, 255, 255), (np.abs(y - 48.5) < 1.0) * 0.85)
    return tp.to_image(c)


def blank_cover():
    """160x224: the sleeve of a game without box art, as RiftWii's made-up
    cover prints the name on it: a black band across the top with a fine
    silver edge and a thin red line under it, then a blue that brightens
    diagonally from the top left to the right, with a soft sheen (dark
    enough that RiftWii writes the name in light ink)."""
    W, H = 160, 224
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    diag = np.clip((x / W) * 0.7 + (y / H) * 0.45, 0, 1)
    c[..., :3] = tp.mix((6, 42, 126), (48, 128, 200), tp.smooth(diag))
    c[..., 3] = 1.0
    # A broad soft sheen across the blue, leaning like the light on a case.
    tp.add_light(c, (120, 180, 240), 0.10 * np.exp(-((x * 0.8 - y + 40) / 46) ** 2))
    # Inside the 14 rows RiftWii keeps clear on the box's spine (columns
    # 3-15 stretched down it, the name from row 16).
    band_h = 12.5
    band = np.clip(band_h - y + 0.5, 0, 1)
    tp.over(c, tp.mix((22, 22, 26), (4, 4, 6), tp.smooth(y / band_h)), band)
    # The band's edge: a fine silver line, then a thin red one.
    tp.over(c, (170, 174, 182), np.clip(1.0 - np.abs(y - band_h - 0.4) / 0.7, 0, 1))
    tp.over(c, (196, 22, 34), np.clip(1.6 - np.abs(y - band_h - 2.2), 0, 1))
    return tp.to_image(c, opaque=True)


def blank_back():
    """160x224: the back of that game's box: a blue that brightens from the
    left (the spine's side is the right), the cover's crops set at the top by
    RiftWii (tp.BACK_PICTURES), a rounded panel of yellow feature tiles and a
    smaller one of white tiles, and a white strip along the foot with small
    print, a rating label and a barcode."""
    W, H = 160, 224
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    diag = np.clip((1 - x / W) * 0.55 + (y / H) * 0.25, 0, 1)
    c[..., :3] = tp.mix((46, 124, 198), (6, 40, 122), tp.smooth(diag))
    c[..., 3] = 1.0
    tp.add_light(c, (120, 180, 240), 0.08 * np.exp(-((x * 0.8 - y + 30) / 50) ** 2))
    for x0, y0, x1, y1 in tp.BACK_PICTURES:
        tp.box(c, x0 - 1, y0 - 1, x1 + 1, y1 + 1, (10, 24, 60), 0.6)
    # The feature panels: a pale rim round a darker inside, tiles in a row.
    tp.box(c, 13, 154, 103, 172, (196, 206, 226), r=3)
    tp.box(c, 14.2, 155.2, 101.8, 170.8, (40, 56, 110), r=2.4)
    for k in range(6):
        x0 = 16 + k * 14.4
        tp.box(c, x0, 157, x0 + 12.6, 164, (246, 206, 40), r=0.8)
        tp.box(c, x0 + 3, 158.6, x0 + 9.6, 162.6, (60, 50, 20), 0.85, r=0.6)
        tp.box(c, x0, 165, x0 + 12.6, 169, (240, 242, 248), r=0.6)
    tp.box(c, 106, 154, 147, 172, (196, 206, 226), r=3)
    tp.box(c, 107.2, 155.2, 145.8, 170.8, (40, 56, 110), r=2.4)
    for k in range(2):
        x0 = 109.5 + k * 18.2
        tp.box(c, x0, 157, x0 + 16, 169, (240, 242, 248), r=0.8)
        tp.box(c, x0 + 4, 159, x0 + 12, 165, (60, 110, 200), 0.85, r=0.6)
    # The white strip along the foot.
    tp.box(c, 0, 178, 104, 224, (255, 255, 255))
    tp.text_lines(c, 4, 182, (96, 94, 97, 90, 60), (90, 92, 96), gap=2.4, thick=0.8)
    tp.text_lines(c, 4, 199, (96, 92, 95, 70), (90, 92, 96), gap=2.4, thick=0.8)
    tp.barcode(c, 66, 206, 102, 222, seed=3)
    # A rating label: white on the blue, a red block in it.
    tp.box(c, 122, 186, 146, 216, (255, 255, 255))
    tp.box(c, 124, 188, 144, 208, (214, 20, 30))
    tp.text_lines(c, 124, 211, (20,), (40, 40, 40), thick=1.2)
    return tp.to_image(c, opaque=True)


# --- sound ---------------------------------------------------------------------

RATE = 32000


def soft_tone(freq, dur, attack=0.004, decay=0.05, harmonics=((1, 1.0), (2, 0.25), (3, 0.08))):
    n = int(RATE * dur)
    t = np.arange(n) / RATE
    s = sum(a * np.sin(TAU * freq * h * t) for h, a in harmonics)
    return s * tp.envelope(n, attack, decay, RATE)


def sounds(out):
    hover = 0.22 * soft_tone(1760, 0.07, decay=0.018)
    click = 0.30 * (soft_tone(988, 0.25, decay=0.06) + 0.7 * np.pad(soft_tone(1480, 0.21, decay=0.05), (1280, 0)))
    up = np.zeros(int(RATE * 0.9))
    down = np.zeros(int(RATE * 0.9))
    for k, f in enumerate((784, 988, 1175, 1568)):
        note = 0.20 * soft_tone(f, 0.6, attack=0.006, decay=0.18)
        up[k * 2600:k * 2600 + note.size] += note
        g = (1568, 1175, 988, 784)[k]
        note = 0.20 * soft_tone(g, 0.6, attack=0.006, decay=0.18)
        down[k * 2600:k * 2600 + note.size] += note
    # A soft airy swell under the notes.
    rng = np.random.default_rng(9)
    air = rng.normal(0, 1, up.size)
    air = tp.circular_convolve(air, np.ones(40) / 40)
    swell = np.sin(np.linspace(0, math.pi, up.size)) ** 2
    up += 0.05 * air * swell
    down += 0.05 * air * swell
    for name, s in (("hover", hover), ("click", click), ("home_open", up), ("home_close", down)):
        tp.write_wav(out / f"{name}.wav", s, RATE)


def music(out):
    """A 64-second ambient pad: four slow chords (I - vi - IV - V7sus in D
    major), each a stack of soft detuned sines with a slow swell, a high
    shimmer of chord tones drifting across the stereo, a simple room
    reverb; it loops without a seam (the reverb's tail is folded back to
    the start)."""
    rate = 32000
    secs = 64
    n = rate * secs
    t = np.arange(n) / rate
    left = np.zeros(n)
    right = np.zeros(n)

    def hz(midi):
        return 440.0 * 2 ** ((midi - 69) / 12)

    chords = ((50, 57, 62, 66, 69), (47, 54, 59, 62, 66), (43, 50, 55, 59, 62), (45, 52, 57, 61, 64))
    seg = n // 4
    for ci, chord in enumerate(chords):
        start = ci * seg
        length = seg + rate * 6  # overlaps into the next chord
        tt = np.arange(length) / rate
        env = np.clip(tt / 5.0, 0, 1) * np.clip((length / rate - tt) / 6.0, 0, 1)
        env = env ** 1.5
        for k, m in enumerate(chord):
            f = hz(m)
            vol = 0.06 if k else 0.08
            l = np.sin(TAU * f * 0.9985 * tt + k) + 0.5 * np.sin(TAU * f * 2 * tt + k * 1.3)
            r = np.sin(TAU * f * 1.0015 * tt + k * 0.7) + 0.5 * np.sin(TAU * f * 2.002 * tt + k)
            idx = (start + np.arange(length)) % n
            np.add.at(left, idx, vol * env * l)
            np.add.at(right, idx, vol * env * r)
        # The shimmer: high chord tones, one every second or so, panned.
        rng = np.random.default_rng(ci + 40)
        for j in range(12):
            m = chord[rng.integers(1, len(chord))] + 24
            at = start + int(rng.random() * seg)
            dur = int(rate * 2.5)
            tt2 = np.arange(dur) / rate
            e = np.clip(tt2 / 0.02, 0, 1) * np.exp(-tt2 / 0.9)
            s = 0.035 * e * np.sin(TAU * hz(m) * tt2)
            pan = rng.random()
            idx = (at + np.arange(dur)) % n
            np.add.at(left, idx, s * (1 - pan))
            np.add.at(right, idx, s * pan)
    # A room: a few dozen decaying echoes, folded round the loop.
    rng = np.random.default_rng(5)
    wet_l = np.zeros(n)
    wet_r = np.zeros(n)
    for k in range(36):
        delay = int(rate * (0.03 + rng.random() * 1.6))
        gain = 0.55 * math.exp(-delay / (rate * 0.9))
        wet_l += gain * np.roll(left, delay) * (0.6 if k % 2 else 0.4)
        wet_r += gain * np.roll(right, delay) * (0.4 if k % 2 else 0.6)
    left = left + 0.35 * wet_l
    right = right + 0.35 * wet_r
    # A gentle low-pass, and the level.
    kernel = np.ones(6) / 6
    # Filtered round the loop point too, so the end runs into the start.
    left = tp.circular_convolve(left, kernel)
    right = tp.circular_convolve(right, kernel)
    peak = max(np.abs(left).max(), np.abs(right).max())
    left *= 0.55 / peak
    right *= 0.55 / peak
    with tempfile.TemporaryDirectory() as tmp:
        wav = pathlib.Path(tmp) / "music.wav"
        tp.write_wav(wav, left, rate, stereo=right)
        tp.to_ogg(wav, out / "music.ogg", quality=3)


def main():
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / "build-themes" / "Wave"
    only = set(sys.argv[2:])
    pictures = {
        "background": lambda: background(640),
        "background_wide": lambda: background(856),
        "bar": lambda: bar(640),
        "bar_wide": lambda: bar(856),
        "tile": lambda: tile(False),
        "tile_over": lambda: tile(True),
        "tile_empty": tile_empty,
        "cover_tile": lambda: cover_tile(False),
        "cover_tile_over": lambda: cover_tile(True),
        "tile9": lambda: tile9(),
        "tile9_over": lambda: tile9(lit=True),
        "tile9_primary": lambda: tile9(primary=True),
        "tile9_primary_over": lambda: tile9(lit=True, primary=True),
        "pill": lambda: pill(),
        "pill_over": lambda: pill(lit=True),
        "pill_primary": lambda: pill(primary=True),
        "pill_primary_over": lambda: pill(lit=True, primary=True),
        "home_button": lambda: home_button(False),
        "home_button_over": lambda: home_button(True),
        "round_button": lambda: round_button(False),
        "round_button_over": lambda: round_button(True),
        "chip_off": lambda: chip(False),
        "chip_on": lambda: chip(True),
        "row_focus": row_focus,
        "step_back": lambda: round_arrow("left", False),
        "step_back_over": lambda: round_arrow("left", True),
        "step_forward": lambda: round_arrow("right", False),
        "step_forward_over": lambda: round_arrow("right", True),
        "scroll_up": lambda: round_arrow("up", False),
        "scroll_up_over": lambda: round_arrow("up", True),
        "scroll_down": lambda: round_arrow("down", False),
        "scroll_down_over": lambda: round_arrow("down", True),
        "switch_on": lambda: switch(True),
        "switch_off": lambda: switch(False),
        "panel_game": lambda: panel(240),
        "panel_settings": lambda: panel(284),
        "popup": popup,
        "banner_stripes": banner_stripes,
        "arrow_left": lambda: page_arrow("left", False),
        "arrow_left_over": lambda: page_arrow("left", True),
        "arrow_right": lambda: page_arrow("right", False),
        "arrow_right_over": lambda: page_arrow("right", True),
        "shelf": shelf,
        "blank_cover": blank_cover,
        "blank_back": blank_back,
    }
    # Names that are neither a picture nor a part stop the run before
    # anything is written: a typo would otherwise leave the old file in place.
    unknown = sorted(only - set(pictures) - {"loop", "sounds", "music"})
    if unknown:
        raise SystemExit("not a picture or part of this theme: " + ", ".join(unknown))
    # A full build goes into an empty folder, so nothing left from an
    # older build (a sound, a frame, music) ends up in the theme.
    if not only and out.exists() and any(out.iterdir()):
        raise SystemExit(f"{out} is not empty: build the whole theme into a new folder")
    out.mkdir(parents=True, exist_ok=True)
    # theme.ini and LICENSE: always on a full build; a run for some parts
    # only adds them when they are missing.
    for name, text in (("theme.ini", INI.replace("\n", "\r\n")), ("LICENSE", LICENSE)):
        if not only or not (out / name).exists():
            (out / name).write_bytes(text.encode("utf-8"))
    written = []
    for name, paint in pictures.items():
        if only and name not in only:
            continue
        written.append(tp.save(paint(), out / (name + ".png")))
        print(name)
    if not only or "loop" in only:
        loop_paths = loop_frames(out)
        print(f"loop: {len(loop_paths)} frames")
        written += loop_paths
    if not only or "sounds" in only:
        sounds(out)
        print("sounds")
    if not only or "music" in only:
        music(out)
        print("music")
    if written:
        shrinker = str(HERE / "shrink_theme_pictures.py")
        subprocess.run([sys.executable, shrinker, "--"] + written, check=True)


if __name__ == "__main__":
    main()
