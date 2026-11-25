#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes Plaza, a RiftWii menu theme in the spirit of the early-2010s
console menus and their glossy "aero" look: an airy white wall with big
faint ghost tiles and glossy aqua bubbles drifting on it, thick frosted
glass bezels round every tile and cover, big glossy white arrows, and sky
blue for game boxes, with bright cyan for whatever is lit.

Every picture, sound and the music are worked out here from arithmetic
alone (no photo, texture, screenshot, logo, recording or picture from
anywhere else):

- backdrop_00.png ... : the looping wall, 320x180 frames that RiftWii scales
  to cover the screen: ghost tiles and rings, soft light and glass bubbles
  bobbing on small closed paths (whole turns a loop, so it has no seam);
- background.png / background_wide.png: the same wall, still, full size;
- bar.png / bar_wide.png: Home's bottom bar, a strip of frosted glass with a
  bright rim, dipping under the clock;
- tiles, cover cards, the game page's and Settings' tiles, the wide buttons,
  the HOME Menu's buttons: thick frosted glass bezels, lit from the top
  left, a gloss over their upper half, a fine dark line round the inside;
  lit, a cyan rim and glow; the main button in aqua glass;
- round buttons: white glass discs; Home's page arrows: big glossy white
  triangles; option chips, the lit list row, switches, panels, the popup,
  the banner's stripes and the shelf in the same glass;
- blank_cover.png: the sleeve of a game without box art: sky blue with a
  gold arc across the top; blank_back.png: its box's back, the same blue
  with outlined info panels, a rating box and a barcode;
- music.ogg: a light, bouncy original tune (soft mallets over a round bass
  and brushed beat), synthesised here and encoded with ffmpeg's Vorbis;
- hover.wav, click.wav, home_open.wav, home_close.wav: soft synthesised
  pops and chimes;
- theme.ini and LICENSE.

    py -3 tools/make_plaza_theme.py [out_dir [picture ...]]   (needs numpy, Pillow, ffmpeg)

The default out_dir is build-themes/Plaza; the folder goes to the community
themes repository (RiftWii/themes, themes/Plaza) as it is.
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
TAU = 2 * math.pi

LOOP_W, LOOP_H, LOOP_FRAMES, LOOP_FPS = 320, 180, 36, 8

CYAN = (24, 176, 230)
CYAN_DEEP = (6, 128, 186)
CYAN_GLOW = (120, 220, 252)
RIM = (150, 158, 170)
RIM_DARK = (112, 120, 134)
SKY = (52, 160, 208)
SKY_DEEP = (26, 132, 190)
GOLD = (246, 196, 40)

INI = """[theme]
name = Plaza
author = RiftWii contributors

[shape]
# Thick, well rounded glass bezels, their gloss drawn in the pictures.
corners = 1.3
gloss = no
bar = dip
shelf_grain = no
home_menu = wii
# Every cover sits in its glass bezel (cover_tile.png), not only the lit one.
cover_frames = yes

[backdrop]
# backdrop_00.png to backdrop_35.png: ghost tiles and glass bubbles bob on
# the white wall, 4.5 seconds a loop. RiftWii versions without looping
# backdrops show background.png.
stripes = no
frames = 36
fps = 8

[colors]
# Text: dark grey on white
ink = #30353D
ink_soft = #4A515C
ink_dim = #646B76
clock = #5A616C
# Cyan marks what is lit; dark text on it
accent = #18B0E6
accent_ink = #0A6A96
text_on_accent = #05283A
warn = #C0392B

# Frosted glass frames
card = #F7F9FB
card_edge = #C2C9D2
card_edge_strong = #A6AEB9
shadow = #3C465030
glow = #18B0E670
glyph = #59616C

# Option chips and switches
chip_on = #DDF3FC
chip_off = #F2F5F8
chip_off_edge = #C9CFD8
switch_off = #C9CFD8

# Background and bars
bar = #EDF1F5
backdrop = #F1F4F7
backdrop_stripe = #F1F4F7
banner_stripe = #FFFFFF26
# The games' own colours, kept
banner_tint = #00000000

# Lists and badges
divider = #E0E5EB
scroll_track = #E3E7EC
scroll_thumb = #A3ABB6
badge = #EAEEF2
shelf = #F8FAFC
shelf_edge = #CDD3DB

# Player pointers
pointer1 = #3B8FD6
pointer2 = #D64545
pointer3 = #3FA34D
pointer4 = #D9A21B
"""

LICENSE = """License: CC0-1.0
Author: RiftWii contributors

Plaza, a theme for RiftWii: an airy white wall with ghost tiles and glossy
bubbles drifting across it, thick frosted glass bezels, big glossy arrows,
sky blue game boxes and bright cyan for what is lit.

To the extent possible under law, the author has waived all copyright and
related or neighboring rights to this theme (theme.ini, every PNG picture,
music.ogg and the WAV sounds in its folder), under Creative Commons CC0 1.0
Universal: https://creativecommons.org/publicdomain/zero/1.0/

The pictures, including the looping backdrop's 36 frames, the music and the
sounds were made by RiftWii's tools/make_plaza_theme.py
(https://github.com/KakarottoCake/Riftwii) from arithmetic alone: no source
images, textures, logos, fonts, recordings or samples. The tune is an
original composition.

Third-party parts: none.
"""


# --- the wall ----------------------------------------------------------------

def bubble(c, x, y, cx, cy, r, a=1.0):
    """A glass bubble: clear in the middle, aqua towards its rim (as glass
    seen edge on), a bright rim, a big soft highlight at its top left and a
    small reflected light at its foot."""
    d = r - np.hypot(x - cx, y - cy)            # > 0 inside
    cov = tp.coverage(d)
    edge = np.clip(1 - d / r, 0, 1)
    tp.over(c, (150, 214, 240), cov * a * (0.10 + 0.38 * edge ** 3))
    tp.over(c, (255, 255, 255), np.clip(1.0 - np.abs(d - 0.8) / 1.0, 0, 1) * a * 0.75)
    hx, hy = cx - 0.36 * r, cy - 0.40 * r
    spec = np.exp(-(((x - hx) / (0.34 * r)) ** 2 + ((y - hy) / (0.22 * r)) ** 2))
    tp.over(c, (255, 255, 255), spec * cov * a * 0.85)
    foot = np.exp(-(((x - cx - 0.15 * r) / (0.45 * r)) ** 2 + ((y - cy - 0.62 * r) / (0.14 * r)) ** 2))
    tp.add_light(c, (190, 240, 255), foot * cov * a * 0.35)


def ghost_tile(c, x, y, cx, cy, half, blur, a):
    """A big faint ghost of a tile: a thick soft white bezel with a grey
    edge, as the menu's own tiles seen out of focus behind it."""
    d, _, _ = tp.rrect(x, y, cx - half, cy - half, 2 * half, 2 * half, half * 0.30)
    t = half * 0.16
    ring = tp.smooth(d / blur + 0.5) * (1 - tp.smooth((d - t) / blur + 0.5))
    tp.over(c, (255, 255, 255), ring * a)
    tp.over(c, (196, 204, 214), np.exp(-(d / (blur * 1.2)) ** 2) * a * 0.5)
    tp.over(c, (210, 216, 224), np.exp(-((d - t) / (blur * 1.2)) ** 2) * a * 0.4)


def wall(W, H, t, u0=0.0, u1=1.0, scale=None):
    if scale is None:
        scale = H / LOOP_H
    x, y = tp.grid(W, H)
    u = u0 + (u1 - u0) * x / W
    v = y / H
    c = tp.canvas(W, H)
    # White at the top, a breath of sky blue towards the foot, a soft grey
    # vignette; a broad light from the top left.
    base = tp.mix((252, 253, 254), (226, 236, 244), tp.smooth(v * 0.85 + 0.15 * np.abs(u - 0.5)))
    vig = tp.smooth(((u - 0.5) / 0.75) ** 2 + ((v - 0.45) / 0.8) ** 2)
    base = tp.mix(base, (206, 214, 224), 0.40 * vig)
    c[..., :3] = base
    c[..., 3] = 1.0
    ray = np.exp(-((u * 0.9 - v * 0.55 - 0.05) / 0.16) ** 2) * (1 - tp.smooth(v / 0.9))
    tp.add_light(c, (255, 255, 255), 0.35 * ray)
    ph = TAU * t
    rng = np.random.default_rng(2112)
    # Ghost tiles and rings, large and out of focus.
    for _ in range(9):
        cu = 0.10 + rng.random() * 0.80
        cv = 0.08 + rng.random() * 0.80
        half = (0.10 + rng.random() * 0.10) * H
        a = 0.30 + rng.random() * 0.20
        phase = rng.random() * TAU
        radius = 0.010 + rng.random() * 0.012
        pu = cu + radius * math.cos(ph + phase) * (H / W) * 1.6
        pv = cv + radius * math.sin(ph + phase)
        px = (pu - u0) / (u1 - u0) * W
        if rng.random() < 0.65:
            ghost_tile(c, x, y, px, pv * H, half, (2.4 + 3.0 * rng.random()) * scale, a)
        else:
            d = half - np.hypot(x - px, y - pv * H)
            t_ring = half * 0.14
            blur = 3.2 * scale
            ring = tp.smooth(d / blur + 0.5) * (1 - tp.smooth((d - t_ring) / blur + 0.5))
            tp.over(c, (255, 255, 255), ring * a)
            tp.over(c, (200, 208, 218), np.exp(-(d / (blur * 1.2)) ** 2) * a * 0.45)
    # Glossy swooshes of aqua light across the lower part, each a
    # see-through band with a bright upper edge, waving a whole turn a loop.
    for k, (base, amp, freq, thick, a) in enumerate(((0.70, 0.07, 0.8, 0.055, 0.30),
                                                      (0.80, 0.05, 1.1, 0.040, 0.26),
                                                      (0.62, 0.04, 0.6, 0.025, 0.20))):
        mid = base + amp * np.sin(TAU * freq * u + 1.7 * k + ph * (1 if k % 2 == 0 else -1))
        band = np.exp(-((v - mid) / thick) ** 2)
        tint = (150, 222, 236) if k != 1 else (170, 230, 200)
        tp.over(c, tint, band * a * 0.8)
        top = np.exp(-((v - (mid - thick * 0.9)) / 0.006) ** 2)
        tp.over(c, (255, 255, 255), top * a * 1.6)
    # Glass bubbles, bobbing.
    for _ in range(20):
        cu = 0.06 + rng.random() * 0.88
        cv = 0.06 + rng.random() * 0.88
        r = (0.020 + rng.random() ** 2 * 0.085) * H
        phase = rng.random() * TAU
        turns = 1 if rng.random() < 0.6 else -1
        amp = 0.010 + rng.random() * 0.016
        pu = cu + 0.5 * amp * math.sin(turns * ph + phase) * (H / W)
        pv = cv + amp * math.cos(turns * ph + phase)
        bubble(c, x, y, (pu - u0) / (u1 - u0) * W, pv * H, r, a=0.55 + 0.4 * rng.random())
    return c


def wall_smooth(W, H, t, u0=0.0, u1=1.0, ss=3):
    big = wall(W * ss, H * ss, t, u0, u1, scale=H * ss / LOOP_H)
    out = tp.canvas(W, H)
    out[..., :3] = big[..., :3].reshape(H, ss, W, ss, 3).mean(axis=(1, 3))
    out[..., 3] = 1.0
    return out


def loop_frames(out):
    paths = []
    for k in range(LOOP_FRAMES):
        img = tp.to_image(wall_smooth(LOOP_W, LOOP_H, k / LOOP_FRAMES), seed=300 + k, opaque=True)
        paths.append(tp.save(img, out / f"backdrop_{k:02d}.png"))
    return paths


def background(W):
    span = 0.75 if W == 640 else 1.0
    u0 = 0.5 - span / 2
    return tp.to_image(wall(W, 480, 0.0, u0, u0 + span, scale=480 / LOOP_H), opaque=True)


# --- frosted glass bezels ------------------------------------------------------

def frame(cw, ch, x0, y0, w, h, r, *, lit=False, primary=False, circle=False, thick=None, face=True,
          shadow=0.42, glow_size=3, ring_w=3.0, bands=None, gloss=0.55):
    """A thick frosted glass bezel: a soft shadow; a rim of fine grey; the
    glass itself, white and a little see-through, bright at its top left and
    greyer at its bottom right; a gloss over its upper half; a fine dark line
    round its inside edge and a pale face sunk inside it (`face`); the whole
    shape solid glass when `face` is off. `primary`: aqua glass. Lit: a cyan
    rim and a cyan glow round it."""
    x, y = tp.grid(cw, ch)
    if circle:
        d = tp.circle(x, y, x0 + w / 2, y0 + h / 2, w / 2)
    else:
        d, _, _ = tp.rrect(x, y, x0, y0, w, h, r)
    cov = tp.coverage(d)
    c = tp.canvas(cw, ch)
    if shadow > 0:
        sh = tp.shadow_of(cov, 3, shadow, dy=3)
        tp.over(c, (40, 52, 66), sh[..., 3])
    if lit:
        g = tp.canvas(cw, ch)
        g[..., :3] = CYAN_GLOW
        g[..., 3] = cov
        g = tp.blur(g, tp.fit_radius(cov, glow_size))
        tp.over(c, CYAN_GLOW, g[..., 3] * 0.85)
    u = np.clip((x - x0) / w, 0, 1)
    v = np.clip((y - y0) / h, 0, 1)
    diag = tp.smooth(0.35 * u + 0.65 * v)
    if primary:
        body = tp.mix((132, 222, 252), (16, 150, 214), diag)
    else:
        body = tp.mix((255, 255, 255), (206, 214, 225), diag)
    if bands is not None:
        brgb, k = bands(v, y - y0)
        body = tp.mix(body, brgb, k)
    tp.over(c, body, cov * 0.96)
    t = thick if thick is not None else max(4.0, min(w, h) * 0.085)
    if face and not primary:
        fd = d - t
        fcov = tp.coverage(fd)
        fv = np.clip((y - y0 - t) / max(h - 2 * t, 1), 0, 1)
        tp.over(c, tp.mix((238, 242, 247), (224, 230, 238), tp.smooth(fv)), fcov)
        # Sunk: a soft shade under the bezel's top edge inside, and the
        # fine dark line round the inside.
        tp.over(c, (120, 130, 144), np.exp(-np.maximum(fd, 0) / 2.5) * fcov * 0.22 * (1 - 0.6 * fv))
        tp.over(c, (132, 140, 154), np.clip(1.3 - np.abs(fd - 0.4), 0, 1) * 0.9)
        # The bezel's inner bevel: light at its foot, as glass catching light.
        tp.over(c, (255, 255, 255), np.clip(1.0 - np.abs(fd + 1.3) / 0.9, 0, 1) * 0.9)
    # The gloss: the upper half lit, ending in a soft curve.
    curve = 0.46 - 0.10 * (u - 0.5) ** 2 * 4
    gl = (1 - tp.smooth((v - curve) / 0.06)) * tp.coverage(d - 1.5)
    tp.over(c, (255, 255, 255), gl * gloss * (0.55 if primary else 0.65))
    # The top-left highlight just inside the rim.
    tl = np.clip(1.0 - np.abs(d - 1.6) / 0.9, 0, 1) * cov * (1 - diag)
    tp.over(c, (255, 255, 255), tl * 0.9)
    # The rim: fine grey, darker at the bottom right.
    rim_k = np.clip(1.4 - np.abs(d - 0.8), 0, 1) * cov
    tp.over(c, tp.mix(RIM, RIM_DARK, diag) if not primary else CYAN_DEEP, rim_k)
    if primary:
        # Aqua glass's own edge: a white line just inside the rim.
        tp.over(c, (220, 248, 255), np.clip(1.0 - np.abs(d - 2.6) / 0.8, 0, 1) * cov * 0.8)
    if lit:
        ring = np.clip(ring_w + 0.5 - np.abs(d - ring_w / 2 - 0.2), 0, 1) * np.clip(d + 0.5, 0, 1)
        tp.over(c, CYAN, ring)
        tp.over(c, (200, 244, 255), np.clip(1.0 - np.abs(d - ring_w - 0.5) / 0.7, 0, 1) * cov * 0.7)
    return c


def img(c):
    return tp.to_image(c)


def bar(W):
    """W x 124: a strip of frosted glass with a bright rim and a fine cyan
    line along its curved top, a gloss under the edge, dipping under the
    clock."""
    H = 124
    tops = tp.bar_tops(W)
    x, y = tp.grid(W, H)
    d = y - tops[None, :]
    c = tp.canvas(W, H)
    tp.over(c, (60, 72, 88), 0.18 * np.exp(np.minimum(d, 0) / 4.0) * (d < 0.5))
    k = np.clip((y - 11) / (H - 11), 0, 1)
    body = tp.mix((250, 252, 254), (222, 230, 238), tp.smooth(k))
    tp.over(c, body, tp.coverage(d) * 0.92)
    tp.over(c, (255, 255, 255), 0.75 * np.exp(-np.maximum(d - 2, 0) / 7.0) * (d > 1.5))
    tp.over(c, RIM, np.clip(1.0 - np.abs(d - 0.6) / 0.9, 0, 1) * 0.9)
    tp.over(c, CYAN, np.clip(1.0 - np.abs(d - 2.2) / 0.8, 0, 1) * 0.45)
    return tp.to_image(c)


def tile(lit=False):
    return img(frame(140, 100, 8, 7, 124, 84, 16, lit=lit, thick=8))


def tile_empty():
    """An empty place: a faint ghost of a bezel."""
    c = tp.canvas(140, 100)
    x, y = tp.grid(140, 100)
    d, _, _ = tp.rrect(x, y, 8, 7, 124, 84, 16)
    cov = tp.coverage(d)
    tp.over(c, (255, 255, 255), cov * 0.35)
    tp.over(c, (180, 188, 198), np.clip(1.0 - np.abs(d - 0.8) / 0.9, 0, 1) * cov * 0.5)
    tp.over(c, (190, 198, 208), np.clip(1.0 - np.abs(d - 7.3) / 0.8, 0, 1) * cov * 0.35)
    return tp.to_image(c)


def cover_tile(lit=False):
    """96x128: the bezel round a cover (the cover, 80x112, fills its middle,
    so the bezel is the 6 pixels round it; 2 clear outside, so neighbouring
    covers' bezels keep a gap)."""
    return img(frame(96, 128, 2, 2, 92, 124, 12, lit=lit, thick=6, glow_size=2, ring_w=2.4))


def tile9(lit=False, primary=False):
    return img(frame(64, 64, 8, 8, 48, 48, 13, lit=lit, primary=primary, thick=4.5, gloss=0.5))


def pill(lit=False, primary=False):
    return img(frame(252, 60, 4, 4, 244, 52, 26, lit=lit, primary=primary, thick=4.5, glow_size=2))


def home_button(lit=False):
    return img(frame(264, 88, 8, 8, 248, 72, 36, lit=lit, thick=6, glow_size=3))


def round_button(lit=False):
    """80x80: a white glass disc with a grey rim and a deep gloss."""
    c = frame(80, 80, 3, 3, 74, 74, 37, lit=lit, circle=True, thick=6, glow_size=2, gloss=0.7)
    return img(c)


def chip(on):
    c = frame(212, 36, 2, 2, 208, 30, 10, face=False, shadow=0.12, gloss=0.4)
    if on:
        x, y = tp.grid(212, 36)
        d, _, _ = tp.rrect(x, y, 2, 2, 208, 30, 10)
        tp.over(c, (210, 240, 252), tp.coverage(d - 1.5) * 0.85)
        tp.over(c, (255, 255, 255), (1 - tp.smooth(((y - 2) / 30 - 0.45) / 0.06)) * tp.coverage(d - 1.5) * 0.4)
        tp.over(c, CYAN, np.clip(1.0 - np.abs(d - 0.8) / 0.9, 0, 1) * tp.coverage(d) * 0.9)
    return img(c)


def row_focus():
    cw, ch = 548, 44
    x, y = tp.grid(cw, ch)
    d, _, _ = tp.rrect(x, y, 1.5, 1.5, 545, 41, 12)
    cov = tp.coverage(d)
    c = tp.canvas(cw, ch)
    v = np.clip((y - 1.5) / 41, 0, 1)
    tp.over(c, tp.mix((236, 250, 255), (196, 236, 252), tp.smooth(v)), cov)
    tp.over(c, (255, 255, 255), cov * (1 - tp.smooth((v - 0.45) / 0.06)) * 0.55)
    tp.over(c, CYAN, np.clip(3.0 - np.abs(d - 1.3), 0, 1) * cov)
    return tp.to_image(c)


def round_arrow(direction, lit):
    c = frame(44, 44, 4, 4, 34, 34, 17, lit=lit, circle=True, face=False, shadow=0.22, glow_size=2, ring_w=2.4,
              gloss=0.6)
    tp.chevron(c, 22, 22, 12, direction, rgb=CYAN_DEEP if lit else (104, 112, 124), width=2.8)
    return img(c)


def switch(on):
    cw, ch = 68, 40
    x, y = tp.grid(cw, ch)
    c = tp.canvas(cw, ch)
    d, _, _ = tp.rrect(x, y, 4, 6, 60, 28, 14)
    cov = tp.coverage(d)
    v = np.clip((y - 6) / 28, 0, 1)
    body = tp.mix((120, 220, 252), (16, 152, 216), tp.smooth(v)) if on else \
        tp.mix((214, 220, 228), (192, 199, 208), tp.smooth(v))
    tp.over(c, body, cov)
    tp.over(c, (90, 100, 112), np.exp(-((d - 0.4) / 1.2) ** 2) * (1 - v) * cov * 0.4)
    tp.over(c, (255, 255, 255), (1 - tp.smooth((v - 0.45) / 0.08)) * tp.coverage(d - 1.5) * 0.35)
    tp.over(c, CYAN_DEEP if on else RIM_DARK, np.clip(1.0 - np.abs(d - 0.7) / 0.9, 0, 1) * cov * 0.8)
    kx = 50 if on else 18
    kc = tp.coverage(tp.circle(x, y, kx, 20, 12.5))
    tp.over(c, (40, 52, 66), tp.shadow_of(kc, 2, 0.35)[..., 3])
    kv = np.clip((y - 7.5) / 25, 0, 1)
    tp.over(c, tp.mix((255, 255, 255), (222, 228, 236), tp.smooth(kv)), kc)
    tp.over(c, (255, 255, 255), (1 - tp.smooth((kv - 0.4) / 0.1)) * kc * 0.5)
    tp.over(c, RIM, np.clip(1.0 - np.abs(tp.circle(x, y, kx, 20, 12.5) - 0.7) / 0.9, 0, 1) * kc)
    return tp.to_image(c)


def panel(h):
    return img(frame(580, h, 4, 4, 572, h - 8, 20, thick=7, shadow=0.22, gloss=0.25))


def popup():
    def bands(v, yy):
        head = yy < 44
        foot = yy > 276 - 18
        rgb = np.where(head[..., None], np.asarray((236, 242, 247), np.float32),
                       np.asarray((226, 232, 239), np.float32))
        k = np.where(head | foot, 1.0, 0.0)
        line = np.abs(yy - 44) < 0.8
        rgb = np.where(line[..., None], np.asarray(CYAN, np.float32), rgb)
        k = np.where(line, 0.9, k)
        return rgb, k
    return img(frame(580, 284, 4, 4, 572, 276, 20, face=False, shadow=0.28, bands=bands, gloss=0.2))


def banner_stripes():
    """640x192: a soft white veil with faint glossy diagonal light (spacing
    32, which divides 640) over the game banner's colour."""
    W, H = 640, 192
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    band = (y < 128).astype(np.float32)
    k = tp.smooth(y / 122)
    tp.over(c, (255, 255, 255), band * (0.10 + 0.12 * k))
    tp.over(c, (255, 255, 255), band * 0.16 * (1 - tp.smooth((y - 50) / 8)))
    tp.over(c, (255, 255, 255), (((x + y) % 32) < 12) * band * 0.08)
    tp.over(c, (255, 255, 255), (np.abs(y - 122) < 1.0) * 0.8)
    return tp.to_image(c)


def page_arrow(direction, lit):
    """48x48: Home's page buttons: big glossy white triangles with rounded
    corners, shaded grey towards their foot, a gloss on top; lit, a cyan
    glow."""
    S = 4
    x, y = tp.grid(48 * S, 48 * S)
    x = x / S
    y = y / S
    pts = ((35, 5), (9, 24), (35, 43)) if direction == "left" else ((13, 5), (39, 24), (13, 43))
    # A rounded triangle: the distance inside the three edges, less a corner.
    def inside(px, py):
        ds = []
        for i in range(3):
            ax, ay = pts[i]
            bx, by = pts[(i + 1) % 3]
            nx, ny = by - ay, -(bx - ax)
            ln = math.hypot(nx, ny)
            cx3 = sum(p[0] for p in pts) / 3
            cy3 = sum(p[1] for p in pts) / 3
            sgn = 1 if ((cx3 - ax) * nx + (cy3 - ay) * ny) > 0 else -1
            ds.append(sgn * ((px - ax) * nx + (py - ay) * ny) / ln)
        return np.minimum(np.minimum(ds[0], ds[1]), ds[2])
    d = inside(x, y) - 2.5
    big = tp.canvas(48 * S, 48 * S)
    cov = np.clip(d * S + 0.5, 0, 1)
    if lit:
        g = tp.canvas(48 * S, 48 * S)
        g[..., :3] = CYAN_GLOW
        g[..., 3] = cov
        g = tp.blur(g, 3 * S)
        tp.over(big, CYAN_GLOW, g[..., 3] * 0.9)
    sh = tp.shadow_of(cov, 2 * S, 0.3, dy=2 * S)
    tp.over(big, (40, 52, 66), sh[..., 3])
    v = np.clip((y - 5) / 38, 0, 1)
    tp.over(big, tp.mix((255, 255, 255), (206, 212, 222), tp.smooth(v)), cov)
    tp.over(big, (255, 255, 255), cov * 0.7 * (1 - tp.smooth((v - 0.42) / 0.06)) * np.clip(d - 1.5, 0, 1))
    tp.over(big, RIM_DARK, np.clip(1.0 - np.abs(d - 0.3) / 0.5, 0, 1) * cov * 0.8)
    if lit:
        tp.over(big, CYAN, np.clip(1.0 - np.abs(d - 1.4) / 0.9, 0, 1) * cov)
    # Down to 48x48, averaging the colours by their alpha.
    blocks = big.reshape(48, S, 48, S, 4)
    alpha = blocks[..., 3].mean(axis=(1, 3))
    out = tp.canvas(48, 48)
    out[..., :3] = (blocks[..., :3] * blocks[..., 3:4]).mean(axis=(1, 3)) / np.maximum(alpha, 1e-6)[..., None]
    out[..., 3] = alpha
    return tp.to_image(out)


def shelf():
    """256x64: a glossy glass ledge: its top (rows 0-47) pale, brightening to
    the front; its front edge (48-63) white with a grey shade under it."""
    W, H = 256, 64
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    top = y < 48
    tp.over(c, tp.mix((222, 230, 238), (250, 252, 254), tp.smooth(y / 48)), top * 0.92)
    tp.over(c, tp.mix((255, 255, 255), (190, 200, 212), tp.smooth((y - 48) / 16)), ~top)
    tp.over(c, (255, 255, 255), (np.abs(y - 48.5) < 1.0))
    tp.over(c, CYAN_GLOW, (np.abs(y - 50.5) < 0.6) * 0.5)
    return tp.to_image(c)


def blank_cover():
    """160x224: the sleeve of a game without box art, as RiftWii's made-up
    cover prints the name on it: sky blue, a little deeper at the foot, a
    gold arc sweeping across the top with a white light along it, and a soft
    sheen (dark enough that RiftWii writes the name in light ink). The arc
    stays in the top 14 rows over the left edge, which RiftWii stretches down
    the box's spine."""
    W, H = 160, 224
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    v = y / H
    c[..., :3] = tp.mix(SKY, SKY_DEEP, tp.smooth(v * 0.9 + 0.1 * (x / W)))
    c[..., 3] = 1.0
    tp.add_light(c, (110, 200, 240), 0.10 * np.exp(-((x * 0.7 - y + 60) / 50) ** 2))
    # The arc: high at both edges, dipping evenly to its lowest in the
    # middle (about 12 rows down over the spine's strip, columns 3-15).
    s = x / W
    arc = 8.0 + 24.0 * np.clip(np.sin(s * math.pi), 0, 1) ** 1.5
    tp.over(c, GOLD, np.clip(1.3 - np.abs(y - arc), 0, 1))
    tp.over(c, (255, 244, 200), np.clip(1.0 - np.abs(y - arc + 1.6) / 0.7, 0, 1) * 0.7)
    # A faint lighter blue above the arc.
    tp.add_light(c, (90, 180, 230), 0.18 * (y < arc) * np.exp(-(arc - y) / 12))
    return tp.to_image(c, opaque=True)


def blank_back():
    """160x224: the back of that game's box: the same sky blue, the cover's
    crops set at the top by RiftWii (tp.BACK_PICTURES), and below them
    rounded panels outlined in white with small print, a row of white
    feature icons and an orange tile, a white panel, a rating box and a
    barcode."""
    W, H = 160, 224
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    c[..., :3] = tp.mix(SKY, SKY_DEEP, tp.smooth(y / H))
    c[..., 3] = 1.0
    for x0, y0, x1, y1 in tp.BACK_PICTURES:
        tp.box(c, x0 - 1, y0 - 1, x1 + 1, y1 + 1, (255, 255, 255), 0.85)
        tp.box(c, x0, y0, x1, y1, (96, 184, 222))

    def outlined(x0, y0, x1, y1):
        tp.box(c, x0, y0, x1, y1, (255, 255, 255), r=2.5)
        tp.box(c, x0 + 0.9, y0 + 0.9, x1 - 0.9, y1 - 0.9, (40, 150, 200), r=1.8)

    # Players and features: a panel with white icons.
    outlined(8, 154, 98, 167)
    tp.box(c, 10.5, 156.5, 18, 164.5, (255, 255, 255), r=1.2)
    for k in range(5):
        x0 = 30 + k * 13.5
        tp.box(c, x0, 157.5, x0 + 10, 163.5, (255, 255, 255), 0.9, r=1.5)
    # The publisher's panel: an orange tile and lines.
    outlined(101, 154, 152, 167)
    tp.box(c, 103.5, 156, 112, 165, (244, 132, 34), r=1.5)
    tp.text_lines(c, 115, 158, (32, 26), (255, 255, 255), a=0.85, gap=3, thick=0.9)
    # Small print in a panel.
    outlined(8, 170, 98, 186)
    tp.text_lines(c, 11, 173, (84, 82, 84, 60), (255, 255, 255), a=0.85, gap=3, thick=0.8)
    tp.box(c, 101, 170, 152, 186, (255, 255, 255), r=1.5)
    tp.text_lines(c, 104, 174, (40, 34), (60, 70, 80), gap=3.4, thick=0.9)
    # The rating box and the barcode.
    tp.box(c, 10, 192, 30, 218, (20, 20, 20))
    tp.box(c, 11.2, 193.2, 28.8, 216.8, (255, 255, 255))
    tp.box(c, 13, 196, 27, 209, (20, 20, 20))
    tp.box(c, 13, 211, 27, 215, (20, 20, 20))
    tp.box(c, 40, 194, 54, 208, (255, 255, 255), 0.85, r=7)
    tp.barcode(c, 96, 196, 150, 218, seed=11)
    return tp.to_image(c, opaque=True)


# --- sound -----------------------------------------------------------------------

RATE = 32000


def mallet(freq, dur, vol=1.0, rate=RATE):
    """A soft mallet note: a sine with a quickly fading fourth partial and a
    touch of the second, as a marimba bar."""
    n = int(rate * dur)
    t = np.arange(n) / rate
    s = (np.sin(TAU * freq * t) * np.exp(-t / 0.45)
         + 0.35 * np.sin(TAU * freq * 4.0 * t) * np.exp(-t / 0.05)
         + 0.12 * np.sin(TAU * freq * 2.0 * t) * np.exp(-t / 0.12))
    s *= np.clip(t / 0.003, 0, 1)
    # A 10 ms release, so a note cut at `dur` ends at silence, not in a click.
    s *= np.clip((n - 1 - np.arange(n)) / (rate * 0.010), 0, 1)
    return vol * s


def sounds(out):
    rate = RATE
    hover = 0.25 * mallet(1568, 0.08) * np.exp(-np.arange(int(rate * 0.08)) / (rate * 0.02))
    click = 0.28 * mallet(1047, 0.3)
    second = 0.22 * mallet(1568, 0.25)
    click[1100:1100 + second.size] += second[:click.size - 1100]
    up = np.zeros(int(rate * 0.8))
    down = np.zeros(int(rate * 0.8))
    for k, f in enumerate((1047, 1319, 1568, 2093)):
        note = 0.22 * mallet(f, 0.5)
        up[k * 2300:k * 2300 + note.size] += note[:up.size - k * 2300]
        g = (2093, 1568, 1319, 1047)[k]
        note = 0.22 * mallet(g, 0.5)
        down[k * 2300:k * 2300 + note.size] += note[:down.size - k * 2300]
    for name, s in (("hover", hover), ("click", click), ("home_open", up), ("home_close", down)):
        tp.write_wav(out / f"{name}.wav", s, rate)


def music(out):
    """An original, light tune, 32 bars of 4/4 at 104 beats a minute in F
    major (about 74 seconds): mallets play a bouncing melody over
    arpeggiated chords (F - Dm - Bb - C, then F - Am - Bb - C/G), a round
    sine bass on the beats and a soft brushed hi-hat. Built on a bar grid,
    so the end runs back into the start."""
    rate = RATE
    bpm = 104
    beat = 60 / bpm
    bars = 32
    n = int(round(rate * beat * 4 * bars))
    left = np.zeros(n)
    right = np.zeros(n)

    def hz(midi):
        return 440.0 * 2 ** ((midi - 69) / 12)

    def put(sig, at, pan=0.5):
        i = int(round(at * rate)) % n
        idx = (i + np.arange(sig.size)) % n
        np.add.at(left, idx, sig * (1 - pan) * 2 * 0.5)
        np.add.at(right, idx, sig * pan * 2 * 0.5)

    prog_a = ((65, 69, 72), (62, 65, 69), (58, 62, 65), (60, 64, 67))   # F Dm Bb C
    prog_b = ((65, 69, 72), (57, 60, 64), (58, 62, 65), (55, 60, 64))   # F Am Bb C/G
    bass_a = (41, 38, 34, 36)
    bass_b = (41, 33, 34, 31)
    # The melody, eight bars, in eighth notes (None: a rest), played twice
    # over A and twice (an octave higher in the second half) over B.
    mel_a = (72, None, 74, 72, 69, None, 67, 69, 65, None, 69, 72, 74, None, None, None,
             70, None, 69, 67, 65, None, 62, 65, 67, None, 69, None, 67, None, None, None,
             72, None, 74, 76, 77, None, 76, 74, 72, None, 69, 72, 74, None, 72, None,
             70, None, 69, 67, 69, None, 72, None, 67, None, None, None, 65, None, None, None)
    mel_b = (69, None, 72, None, 76, None, 74, 72, 69, None, 67, None, 64, None, None, None,
             70, None, 72, 74, 72, None, 70, 69, 67, None, 65, None, 67, None, None, None,
             69, None, 72, 74, 76, None, 77, 76, 74, None, 72, None, 70, None, 69, None,
             70, None, 74, None, 72, None, 70, None, 69, None, None, None, 65, None, None, None)
    for b in range(bars):
        section_b = (b // 8) % 2 == 1
        prog = prog_b if section_b else prog_a
        bass = bass_b if section_b else bass_a
        chord = prog[b % 4]
        t0 = b * 4 * beat
        # Arpeggio: up and down the chord in eighths, soft, panned right.
        pattern = (0, 1, 2, 1, 0, 1, 2, 1)
        for k, idx in enumerate(pattern):
            note = chord[idx] - 12
            put(0.10 * mallet(hz(note), 0.5), t0 + k * beat / 2, pan=0.68)
        # Bass on beats 1 and 3, a round sine with a soft attack.
        for k in (0, 2):
            dur = beat * 1.8
            tt = np.arange(int(rate * dur)) / rate
            env = np.clip(tt / 0.01, 0, 1) * np.exp(-tt / 0.6)
            # A 20 ms release, so the note ends at silence, not in a click.
            env *= np.clip((tt.size - 1 - np.arange(tt.size)) / (rate * 0.020), 0, 1)
            f = hz(bass[b % 4] if k == 0 else bass[b % 4] + 7)
            put(0.20 * env * (np.sin(TAU * f * tt) + 0.2 * np.sin(TAU * 2 * f * tt)), t0 + k * beat, pan=0.5)
        # Brushed hi-hat on the off-beats.
        rng = np.random.default_rng(b)
        for k in range(4):
            tt = np.arange(int(rate * 0.09)) / rate
            noise = rng.normal(0, 1, tt.size)
            noise = noise - np.convolve(noise, np.ones(3) / 3, mode="same")
            put(0.035 * noise * np.exp(-tt / 0.025), t0 + (k + 0.5) * beat, pan=0.42)
        # The melody.
        mel = mel_b if section_b else mel_a
        lift = 12 if (b // 16) % 2 == 1 else 0
        for k in range(8):
            m = mel[(b % 8) * 8 + k]
            if m is None:
                continue
            put(0.20 * mallet(hz(m + lift), 0.7), t0 + k * beat / 2, pan=0.40)
    # A little room.
    wet_l = np.zeros(n)
    wet_r = np.zeros(n)
    rng = np.random.default_rng(3)
    for k in range(18):
        delay = int(rate * (0.02 + rng.random() * 0.35))
        g = 0.45 * math.exp(-delay / (rate * 0.25))
        wet_l += g * np.roll(right if k % 2 else left, delay)
        wet_r += g * np.roll(left if k % 2 else right, delay)
    left = left + 0.25 * wet_l
    right = right + 0.25 * wet_r
    peak = max(np.abs(left).max(), np.abs(right).max())
    left *= 0.6 / peak
    right *= 0.6 / peak
    with tempfile.TemporaryDirectory() as tmp:
        wav = pathlib.Path(tmp) / "music.wav"
        tp.write_wav(wav, left, rate, stereo=right)
        tp.to_ogg(wav, out / "music.ogg", quality=3)


def main():
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / "build-themes" / "Plaza"
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
