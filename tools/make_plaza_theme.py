#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes Plaza, a RiftWii menu theme: an airy, almost white wall with soft
pale shapes drifting slowly across it, thick glossy white frames with a
pale grey rim, and bright cyan for whatever is lit.

Every picture, sound and the music are worked out here from arithmetic
alone (no photo, texture, screenshot, recording or picture from anywhere
else):

- backdrop_00.png ... : the looping wall, 320x180 frames that RiftWii scales
  to cover the screen: soft rounded squares and circles, a shade off white,
  drifting on small closed paths (whole turns a loop, so it has no seam);
- background.png / background_wide.png: the same wall, still, full size;
- bar.png / bar_wide.png: Home's bottom bar, a bright glossy white strip
  with a soft grey shadow along its top, dipping under the clock;
- tiles, cover cards, the game page's and Settings' tiles, the wide and
  round buttons, the HOME Menu's buttons: thick glossy white frames, a pale
  grey rim, a fine inner bevel and a soft shadow; lit, a cyan ring;
- option chips, the lit list row, the option and scroll arrows, switches,
  panels, the popup, the banner's stripes, Home's page arrows, the shelf
  and the blank cover in the same white and cyan;
- music.ogg: a light, bouncy original tune (soft mallets over a round bass
  and brushed beat), synthesised here and encoded with ffmpeg's Vorbis;
- hover.wav, click.wav, home_open.wav, home_close.wav: soft synthesised
  pops and chimes;
- theme.ini and LICENSE.

    py -3 tools/make_plaza_theme.py [out_dir [picture ...]]   (needs numpy, Pillow, ffmpeg)

The default out_dir is build-themes/Plaza; the folder goes to the community
themes repository (RiftWii/themes, themes/Plaza) as it is. Naming pictures
(or "music", "sounds", "loop") repaints only those.
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

CYAN = (30, 178, 232)
CYAN_DEEP = (8, 134, 190)
CYAN_GLOW = (110, 214, 250)
WHITE = (255, 255, 255)
RIM = (196, 202, 210)
RIM_DARK = (160, 168, 178)

INI = """[theme]
name = Plaza
author = RiftWii contributors

[shape]
# Thick, well rounded glossy frames.
corners = 1.3
gloss = yes
bar = dip
shelf_grain = no
home_menu = wii

[backdrop]
# backdrop_00.png to backdrop_35.png: soft pale shapes drift on the white
# wall, 4.5 seconds a loop. RiftWii versions without looping backdrops
# show background.png.
stripes = no
frames = 36
fps = 8

[colors]
# Text: dark grey on white
ink = #333840
ink_soft = #4E5560
ink_dim = #6C737E
clock = #5E6570
# Cyan marks what is lit; dark text on it
accent = #1EB2E8
accent_ink = #0A6E9A
text_on_accent = #062A3C
warn = #C0392B

# Glossy white frames
card = #FFFFFF
card_edge = #C9CED6
card_edge_strong = #AEB5BF
shadow = #3C465024
glow = #1EB2E860
glyph = #5A616C

# Option chips and switches
chip_on = #DDF3FC
chip_off = #F4F6F8
chip_off_edge = #CDD2DA
switch_off = #CDD2DA

# Background and bars
bar = #EEF1F4
backdrop = #F3F5F7
backdrop_stripe = #F3F5F7
banner_stripe = #FFFFFF26
# The games' own colours, kept
banner_tint = #00000000

# Lists and badges
divider = #E3E7EC
scroll_track = #E6E9EE
scroll_thumb = #A9B0BA
badge = #EDF0F3
shelf = #FBFCFD
shelf_edge = #D3D8DF

# Player pointers
pointer1 = #3B8FD6
pointer2 = #D64545
pointer3 = #3FA34D
pointer4 = #D9A21B
"""

LICENSE = """License: CC0-1.0
Author: RiftWii contributors

Plaza, a theme for RiftWii: an airy, almost white wall with soft pale
shapes drifting across it, thick glossy white frames with a pale grey rim,
and bright cyan for what is lit.

To the extent possible under law, the author has waived all copyright and
related or neighboring rights to this theme (theme.ini, every PNG picture,
music.ogg and the WAV sounds in its folder), under Creative Commons CC0 1.0
Universal: https://creativecommons.org/publicdomain/zero/1.0/

The pictures, including the looping backdrop's 36 frames, the music and the
sounds were made by RiftWii's tools/make_plaza_theme.py
(https://github.com/KakarottoCake/Riftwii) from arithmetic alone: no source
images, textures, fonts, recordings or samples. The tune is an original
composition.

Third-party parts: none.
"""


# --- the wall ----------------------------------------------------------------

def wall(W, H, t, u0=0.0, u1=1.0, scale=None):
    if scale is None:
        scale = H / LOOP_H
    x, y = tp.grid(W, H)
    u = u0 + (u1 - u0) * x / W
    v = y / H
    c = tp.canvas(W, H)
    base = tp.mix((250, 251, 252), (232, 236, 240), tp.smooth(v * 0.9 + 0.1 * np.abs(u - 0.5)))
    vig = tp.smooth(((u - 0.5) / 0.8) ** 2 + ((v - 0.45) / 0.8) ** 2)
    base = tp.mix(base, (214, 220, 228), 0.45 * vig)
    c[..., :3] = base
    c[..., 3] = 1.0
    # A faint square grid, as squared paper: fine light lines, a little
    # darker every fourth one, fading out towards the floor.
    cell = 20 * scale
    gx = np.abs(((x + u0 * W / (u1 - u0)) % cell) - cell / 2)
    gy = np.abs((y % cell) - cell / 2)
    line = np.clip(1.2 - (cell / 2 - np.minimum(gx, gy)) / max(0.6 * scale, 0.6), 0, 1)
    tp.over(c, (206, 213, 222), line * 0.22 * (1 - 0.6 * v))
    ph = TAU * t
    rng = np.random.default_rng(1211)
    count = 26
    for j in range(count):
        # Kept clear of the far edges, which RiftWii may mirror.
        cu = 0.07 + rng.random() * 0.86
        cv = rng.random() * 0.95
        size = 0.035 + rng.random() * 0.085      # half-size, in screen heights
        radius_turn = 0.008 + rng.random() * 0.02
        turns = 1 if rng.random() < 0.7 else -1
        phase = rng.random() * TAU
        rounded = rng.random() < 0.6
        tint = (226, 232, 240) if rng.random() < 0.7 else (214, 236, 246)
        depth = rng.random()
        # A small closed path: a whole turn a loop.
        pu = cu + radius_turn * math.cos(turns * ph + phase) * (H / W) * 1.6
        pv = cv + radius_turn * math.sin(turns * ph + phase)
        px = (pu - u0) / (u1 - u0) * W
        py = pv * H
        half = size * H
        blur_px = (1.0 + 3.5 * depth) * scale
        if rounded:
            d, _, _ = tp.rrect(x, y, px - half, py - half, 2 * half, 2 * half, half * 0.32)
        else:
            d = tp.circle(x, y, px, py, half)
        a = tp.smooth(d / blur_px + 0.5) * (0.38 + 0.25 * (1 - depth))
        # A light rim: the shapes are glassy, lit from above.
        rim = np.exp(-((d - 1.5 * scale) / (1.2 * scale)) ** 2) * (0.25 * (1 - depth))
        tp.over(c, tint, a)
        tp.over(c, (255, 255, 255), rim * tp.smooth((py - y) / half + 0.6))
    return c


def wall_smooth(W, H, t, u0=0.0, u1=1.0, ss=2):
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


# --- glossy white frames ---------------------------------------------------------

def frame(cw, ch, x0, y0, w, h, r, *, lit=False, primary=False, circle=False, thick=None, inner=True,
          shadow=0.30, face=None, glow_size=3, ring_w=3.0, bands=None):
    """A thick glossy white frame: a soft shadow, a white body with a gentle
    top-to-bottom shade, a pale grey rim, a fine bevel inside it (the frame
    a few pixels thick round a slightly greyer face) and a soft gloss on the
    upper half; `primary` a cyan body; lit, a cyan ring and a faint cyan
    glow."""
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
        g = tp.blur(g, glow_size)
        tp.over(c, CYAN_GLOW, g[..., 3] * 0.75)
    v = np.clip((y - y0) / h, 0, 1)
    if primary:
        body = tp.mix((96, 212, 250), (14, 150, 214), tp.smooth(v))
    else:
        body = tp.mix((255, 255, 255), (232, 236, 241), tp.smooth(v))
    if face is not None:
        body = tp.mix(body, face, 0.0)
    if bands is not None:
        brgb, k = bands(v, y - y0)
        body = tp.mix(body, brgb, k)
    tp.over(c, body, cov)
    t = thick if thick is not None else max(3.0, min(w, h) * 0.07)
    if inner:
        # The face inside the frame: a touch greyer, sunk by a fine bevel
        # (dark at its top edge, light at its foot).
        fd = d - t
        fcov = tp.coverage(fd)
        face_rgb = tp.mix((246, 248, 250), (236, 240, 244), tp.smooth(v)) if not primary else \
            tp.mix((70, 196, 244), (20, 160, 222), tp.smooth(v))
        tp.over(c, face_rgb, fcov * 0.9)
        bevel_dark = np.exp(-((fd - 0.4) / 0.8) ** 2) * (1 - v)
        bevel_light = np.exp(-((fd - 0.4) / 0.8) ** 2) * v
        tp.over(c, (150, 160, 172) if not primary else (6, 110, 160), bevel_dark * 0.45)
        tp.over(c, (255, 255, 255), bevel_light * 0.8)
    # The gloss: the upper half brighter, ending in a soft line.
    gl = (1 - tp.smooth((v - 0.42) / 0.08)) * 0.35 * tp.coverage(d - 2)
    tp.over(c, (255, 255, 255), gl * (0.55 if primary else 0.8))
    # The rim: pale grey, darker towards the bottom.
    rim_k = np.clip(1.0 - np.abs(d - 0.7) / 0.9, 0, 1) * cov
    rim_rgb = tp.mix(RIM, RIM_DARK, tp.smooth(v))
    tp.over(c, rim_rgb if not primary else CYAN_DEEP, rim_k * 0.95)
    if lit:
        ring = np.clip(ring_w + 0.5 - np.abs(d - ring_w / 2 - 0.2), 0, 1) * np.clip(d + 0.5, 0, 1)
        tp.over(c, CYAN, ring)
        tp.over(c, (180, 236, 255), np.clip(1.0 - np.abs(d - ring_w - 0.4) / 0.7, 0, 1) * cov * 0.6)
    return c


def img(c):
    return tp.to_image(c)


def bar(W):
    """W x 124: a glossy white strip with a soft grey shadow along its top
    edge and a fine cyan line, dipping under the clock."""
    H = 124
    tops = tp.bar_tops(W)
    x, y = tp.grid(W, H)
    d = y - tops[None, :]
    c = tp.canvas(W, H)
    tp.over(c, (60, 72, 88), 0.22 * np.exp(np.minimum(d, 0) / 4.0) * (d < 0.5))
    k = np.clip((y - 11) / (H - 11), 0, 1)
    body = tp.mix((252, 253, 254), (226, 231, 237), tp.smooth(k))
    tp.over(c, body, tp.coverage(d))
    tp.over(c, (255, 255, 255), 0.7 * np.exp(-np.maximum(d - 2, 0) / 5.0) * (d > 1.5))
    tp.over(c, RIM_DARK, np.clip(1.0 - np.abs(d - 0.6) / 0.9, 0, 1) * 0.9)
    tp.over(c, CYAN, np.clip(1.0 - np.abs(d - 2.2) / 0.8, 0, 1) * 0.55)
    return tp.to_image(c)


def tile(lit=False):
    return img(frame(140, 100, 8, 7, 124, 84, 14, lit=lit, thick=5))


def tile_empty():
    """An empty place: a faint sunken outline in the wall's white."""
    c = tp.canvas(140, 100)
    x, y = tp.grid(140, 100)
    d, _, _ = tp.rrect(x, y, 8, 7, 124, 84, 14)
    cov = tp.coverage(d)
    tp.over(c, (236, 240, 244), cov * 0.55)
    top = np.exp(-((d - 0.8) / 1.0) ** 2) * cov
    v = np.clip((y - 7) / 84, 0, 1)
    tp.over(c, (170, 178, 190), top * (0.55 - 0.4 * v))
    tp.over(c, (255, 255, 255), top * v * 0.8)
    return tp.to_image(c)


def cover_tile(lit=False):
    return img(frame(96, 128, 6, 6, 84, 116, 10, lit=lit, thick=4, glow_size=2))


def tile9(lit=False, primary=False):
    return img(frame(64, 64, 8, 8, 48, 48, 12, lit=lit, primary=primary, thick=3, inner=False))


def pill(lit=False, primary=False):
    return img(frame(252, 60, 4, 4, 244, 52, 26, lit=lit, primary=primary, inner=False, shadow=0.25,
                     glow_size=2))


def home_button(lit=False):
    return img(frame(264, 88, 8, 8, 248, 72, 36, lit=lit, inner=False, glow_size=3))


def round_button(lit=False):
    return img(frame(80, 80, 3, 3, 74, 74, 37, lit=lit, circle=True, thick=5, shadow=0.25, glow_size=2))


def chip(on):
    c = frame(212, 36, 2, 2, 208, 30, 10, inner=False, shadow=0.12)
    if on:
        x, y = tp.grid(212, 36)
        d, _, _ = tp.rrect(x, y, 2, 2, 208, 30, 10)
        tp.over(c, (221, 243, 252), tp.coverage(d - 1.5) * 0.9)
        tp.over(c, CYAN, np.clip(1.0 - np.abs(d - 0.8) / 0.9, 0, 1) * tp.coverage(d) * 0.9)
    return img(c)


def row_focus():
    cw, ch = 548, 44
    x, y = tp.grid(cw, ch)
    d, _, _ = tp.rrect(x, y, 1.5, 1.5, 545, 41, 12)
    cov = tp.coverage(d)
    c = tp.canvas(cw, ch)
    v = np.clip((y - 1.5) / 41, 0, 1)
    tp.over(c, tp.mix((236, 249, 255), (204, 238, 252), tp.smooth(v)), cov)
    tp.over(c, (255, 255, 255), cov * (1 - tp.smooth((v - 0.42) / 0.08)) * 0.45)
    ring = np.clip(3.0 - np.abs(d - 1.3), 0, 1) * cov
    tp.over(c, CYAN, ring)
    return tp.to_image(c)


def round_arrow(direction, lit):
    c = frame(44, 44, 4, 4, 34, 34, 17, lit=lit, circle=True, inner=False, shadow=0.22, glow_size=2, ring_w=2.4)
    tp.chevron(c, 22, 21.5 if direction in ("left", "right") else 22, 12, direction,
               rgb=CYAN_DEEP if lit else (110, 118, 130), width=2.8)
    return img(c)


def switch(on):
    cw, ch = 68, 40
    x, y = tp.grid(cw, ch)
    c = tp.canvas(cw, ch)
    d, _, _ = tp.rrect(x, y, 4, 6, 60, 28, 14)
    cov = tp.coverage(d)
    v = np.clip((y - 6) / 28, 0, 1)
    if on:
        body = tp.mix((96, 212, 250), (16, 156, 218), tp.smooth(v))
    else:
        body = tp.mix((214, 219, 226), (196, 202, 211), tp.smooth(v))
    tp.over(c, body, cov)
    tp.over(c, (90, 100, 112), np.exp(-((d - 0.4) / 1.2) ** 2) * (1 - v) * cov * 0.4)
    tp.over(c, CYAN_DEEP if on else RIM_DARK, np.clip(1.0 - np.abs(d - 0.7) / 0.9, 0, 1) * cov * 0.8)
    kx = 50 if on else 18
    kd = tp.circle(x, y, kx, 20, 12.5)
    kc = tp.coverage(kd)
    tp.over(c, (40, 52, 66), tp.shadow_of(kc, 2, 0.35)[..., 3])
    kv = np.clip((y - 7.5) / 25, 0, 1)
    tp.over(c, tp.mix((255, 255, 255), (226, 231, 237), tp.smooth(kv)), kc)
    tp.over(c, RIM, np.clip(1.0 - np.abs(kd - 0.7) / 0.9, 0, 1) * kc)
    return tp.to_image(c)


def panel(h):
    return img(frame(580, h, 4, 4, 572, h - 8, 18, thick=6, shadow=0.25))


def popup():
    def bands(v, yy):
        head = yy < 44
        foot = yy > 276 - 18
        rgb = np.where(head[..., None], np.asarray((238, 242, 246), np.float32),
                       np.asarray((228, 233, 239), np.float32))
        k = np.where(head | foot, 1.0, 0.0)
        line = np.abs(yy - 44) < 0.8
        rgb = np.where(line[..., None], np.asarray(CYAN, np.float32), rgb)
        k = np.where(line, 0.9, k)
        return rgb, k
    return img(frame(580, 284, 4, 4, 572, 276, 18, inner=False, shadow=0.28, bands=bands))


def banner_stripes():
    """640x192: a soft white veil with wide, faint diagonal light stripes
    (spacing 32, which divides 640) over the game banner's colour."""
    W, H = 640, 192
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    band = (y < 128).astype(np.float32)
    k = tp.smooth(y / 122)
    tp.over(c, (255, 255, 255), band * (0.10 + 0.12 * k))
    stripe = ((x + y) % 32) < 12
    tp.over(c, (255, 255, 255), stripe * band * 0.10)
    tp.over(c, (255, 255, 255), (np.abs(y - 122) < 1.0) * 0.8)
    return tp.to_image(c)


def page_arrow(direction, lit):
    """48x48: Home's page buttons, glossy white triangles with a grey rim; lit,
    a cyan glow."""
    c = tp.canvas(48, 48)
    pts = ((34, 7), (12, 24), (34, 41)) if direction == "left" else ((14, 7), (36, 24), (14, 41))
    if lit:
        g = tp.canvas(48, 48)
        tp.triangle(g, pts, CYAN_GLOW, 1.0)
        g = tp.blur(g, 3)
        tp.over(c, CYAN_GLOW, g[..., 3] * 0.9)
    sh = tp.canvas(48, 48)
    tp.triangle(sh, [(px, py + 2.5) for px, py in pts], (40, 52, 66), 0.35)
    sh = tp.blur(sh, 1)
    tp.over(c, (40, 52, 66), sh[..., 3])
    tp.triangle(c, pts, RIM_DARK, 1.0)
    # A white body inset a pixel, glossy on top.
    cx = sum(p[0] for p in pts) / 3
    cy = sum(p[1] for p in pts) / 3
    inner = [(cx + (px - cx) * 0.86, cy + (py - cy) * 0.86) for px, py in pts]
    tp.triangle(c, inner, (255, 255, 255), 1.0)
    shade = tp.canvas(48, 48)
    tp.triangle(shade, inner, (220, 226, 234), 1.0)
    x, y = tp.grid(48, 48)
    tp.over(c, (220, 226, 234), shade[..., 3] * tp.smooth((y - 24) / 14))
    if lit:
        tp.triangle(c, [(cx + (px - cx) * 0.5, cy + (py - cy) * 0.5) for px, py in pts], CYAN, 0.9)
    return tp.to_image(c)


def shelf():
    """256x64: a glossy white ledge: its top (rows 0-47) pale, brightening
    to the front; its front edge (48-63) white with a grey shade under it."""
    W, H = 256, 64
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    top = y < 48
    k = y / 48
    tp.over(c, tp.mix((226, 231, 237), (250, 251, 253), tp.smooth(k)), top * 0.95)
    front = ~top
    fk = (y - 48) / 16
    tp.over(c, tp.mix((255, 255, 255), (196, 203, 212), tp.smooth(fk)), front)
    tp.over(c, (255, 255, 255), (np.abs(y - 48.5) < 1.0))
    return tp.to_image(c)


def blank_cover():
    """160x224: a clean white case: a soft grey frame, a pale face, a thin
    cyan line near the top; the middle kept plain for the name."""
    W, H = 160, 224
    c = frame(W, H, 0, 0, W, H, 8, thick=7, shadow=0.0)
    x, y = tp.grid(W, H)
    tp.over(c, CYAN, (np.abs(y - 22) < 1.2) * (x > 16) * (x < W - 16) * 0.8)
    out = tp.to_image(c)
    # The cover is opaque.
    return out


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
    out.mkdir(parents=True, exist_ok=True)
    only = set(sys.argv[2:])
    (out / "theme.ini").write_bytes(INI.replace("\n", "\r\n").encode("utf-8"))
    (out / "LICENSE").write_bytes(LICENSE.encode("utf-8"))
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
    }
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
