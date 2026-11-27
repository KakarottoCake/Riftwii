#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes Dashboard, a RiftWii menu theme in the spirit of the mid-2000s
"blades" console dashboards: a deep, glowing green wall framed by layered
silver blades down both sides, glossy green glass panels and buttons with a
bright rim, and game boxes in green plastic cases with a white strip across
the top of the sleeve.

Every picture and sound is worked out here from arithmetic alone: no
photo, texture, screenshot, logo, font or recording from anywhere else.

- background.png / background_wide.png: the green wall, brightest behind
  Home's tiles, with soft out-of-focus light swirls, and three silver blades
  curving down each side (inside the TV's overscan margin);
- bar.png / bar_wide.png: Home's bottom bar, a dark green glass footer with
  a bright rim, dipping under the clock;
- tiles, the game page's and Settings' tiles, the wide buttons, chips, the
  lit list row, switches, panels and the popup: green glass with a gloss
  cap and a fine bright rim; lit, a brighter body, a lime and white rim and
  a soft glow;
- round buttons: green glass orbs in a silver bezel; the HOME Menu's buttons
  and the arrows: silver;
- cover_tile*: a game box, a green plastic case round the cover;
- blank_cover.png: the sleeve RiftWii prints a game's name on when it has
  no box art: a white band across the top with green swooshes at its
  right, silver-grey below;
- blank_back.png: the back of that box on the shelf: silver-grey, feature
  tags, small print, a rating box and a barcode round the cover's crops;
- the banner's stripes, the shelf, Home's page arrows;
- hover.wav, click.wav, home_open.wav, home_close.wav: soft synthesised
  glassy blips and sweeps;
- theme.ini and LICENSE. The music is RiftWii's own.

    py -3 tools/make_dashboard_theme.py [out_dir [picture ...]]   (needs numpy, Pillow)

The default out_dir is build-themes/Dashboard; the folder goes to the
community themes repository (RiftWii/themes, themes/Dashboard) as it is.
A whole build needs a new or empty folder, so nothing from an older
build is left in the theme; naming pictures (or "sounds") repaints only
those in place, and an unknown name stops the run.
"""
import math
import pathlib
import subprocess
import sys

import numpy as np

import themepaint as tp

HERE = pathlib.Path(__file__).resolve().parent
TAU = 2 * math.pi

# The green glass: its body at the top and the foot. White text sits on the
# middle, which stays dark enough for 4.5:1.
GLASS_TOP = (38, 118, 20)
GLASS_FOOT = (20, 86, 12)
LIT_TOP = (32, 108, 16)
LIT_FOOT = (26, 102, 14)
LIME = (178, 240, 96)
GLOW = (206, 255, 150)
RIM = (170, 226, 120)
SILVER_TOP = (244, 247, 244)
SILVER_FOOT = (160, 168, 162)

INI = """[theme]
name = Dashboard
author = RiftWii contributors

[shape]
# Rounded glass, its own gloss drawn in the pictures.
corners = 0.9
gloss = no
bar = dip
shelf_grain = no
home_menu = wii

[colors]
# Text: white, and pale greens for the quieter levels
ink = #FFFFFF
ink_soft = #E2F4D6
ink_dim = #C2E2AE
clock = #FFFFFF
# Lime marks what is lit; its text is dark
accent = #9BE65A
accent_ink = #CBF5A0
text_on_accent = #10300A
warn = #FFB4A8

# Green glass tiles, buttons and panels
card = #2A7A16
card_edge = #8ECF62
card_edge_strong = #D2F5B4
shadow = #00000066
glow = #D8FFB070
glyph = #FFFFFF

# Option chips and switches
chip_on = #3E9A22
chip_off = #1E5A10
chip_off_edge = #6AAE46
switch_off = #245E14

# Background and bars
bar = #1C5E0E
backdrop = #2A7E16
backdrop_stripe = #2A7E16
banner_stripe = #FFFFFF14
# The games' own colours, a little toward the green
banner_tint = #1E6A0E30

# Lists and badges
divider = #5CA63A
scroll_track = #18500E
scroll_thumb = #B8E896
badge = #1E5A10
shelf = #3C9A22
shelf_edge = #1C560E

# Player pointers
pointer1 = #3B8FD6
pointer2 = #E05252
pointer3 = #F2F2F2
pointer4 = #E5AC24
"""

LICENSE = """License: CC0-1.0
Author: RiftWii contributors

Dashboard, a theme for RiftWii: a glowing green wall framed by silver
blades, glossy green glass buttons and panels, and game boxes in green
plastic cases.

To the extent possible under law, the author has waived all copyright and
related or neighboring rights to this theme (theme.ini, every PNG picture
and the WAV sounds in its folder), under Creative Commons CC0 1.0
Universal: https://creativecommons.org/publicdomain/zero/1.0/

The pictures and sounds were made by RiftWii's
tools/make_dashboard_theme.py (https://github.com/KakarottoCake/Riftwii)
from arithmetic alone: no source images, textures, logos, fonts,
recordings or samples. The music is RiftWii's own.

Third-party parts: none.
"""


# --- the wall --------------------------------------------------------------------

def blade(c, x, y, W, H, peak, bow, top_rgb, foot_rgb, *, rim_rgb, rim_a, shine, cast):
    """One silver (or green) blade down the left edge of a W x H canvas, and
    its mirror on the right: its inner edge bows towards the middle of the
    screen, furthest at half height (`peak` pixels in), `bow` pixels less at
    the top and the foot. Shaded as a curved sheet: a bright streak near its
    inner edge, darker towards the screen's edge; a fine rim along the edge
    and a soft shadow cast on whatever is inside it."""
    yy = (y / H - 0.5) / 0.5
    for side in (0, 1):
        xs = x if side == 0 else (W - 1) - x
        edge = peak - bow * yy ** 2
        d = edge - xs                      # > 0 inside the blade
        cov = tp.coverage(d)
        # The shadow it casts inwards.
        tp.over(c, (0, 18, 0), cast * np.exp(np.minimum(d, 0) / 9.0) * (d < 0.5))
        across = np.clip(d / np.maximum(edge, 1.0), 0, 1)   # 0 at the edge, 1 at the screen's edge
        body = tp.mix(top_rgb, foot_rgb, tp.smooth(np.abs(yy) * 0.8 + across * 0.35))
        streak = np.exp(-((d - 5.0) / 3.2) ** 2)
        body = tp.mix(body, (255, 255, 255), shine * streak)
        tp.over(c, body, cov)
        tp.over(c, rim_rgb, np.clip(1.0 - np.abs(d - 0.6) / 0.9, 0, 1) * rim_a)


def wall(W, H):
    x, y = tp.grid(W, H)
    u = x / W
    v = y / H
    c = tp.canvas(W, H)
    # A deep green, glowing brightest behind Home's tiles (upper middle) and
    # darker towards the foot, where RiftWii writes white text on it.
    glow = np.exp(-(((u - 0.5) / 0.42) ** 2 + ((v - 0.30) / 0.36) ** 2))
    base = tp.mix((14, 62, 8), (40, 128, 22), glow)
    base = tp.mix(base, (82, 182, 42), 0.60 * glow ** 3)
    base = tp.mix(base, (12, 52, 6), 0.35 * tp.smooth((v - 0.70) / 0.30))
    c[..., :3] = base
    c[..., 3] = 1.0
    # Soft, out-of-focus swirls of lighter green light across the wall.
    for k, (cy, amp, freq, phase, width, a) in enumerate((
            (0.26, 0.10, 0.9, 0.4, 0.060, 0.16),
            (0.40, 0.08, 1.3, 2.1, 0.040, 0.12),
            (0.55, 0.12, 0.7, 4.0, 0.080, 0.10),
            (0.18, 0.06, 1.6, 1.0, 0.030, 0.10))):
        mid = cy + amp * np.sin(TAU * freq * u + phase)
        band = np.exp(-((v - mid) / width) ** 2)
        tp.add_light(c, (110, 200, 60), a * band * (0.4 + 0.6 * np.sin(math.pi * u) ** 2))
    # A faint diagonal sheen, as on glass.
    sheen = np.exp(-((u * 0.8 + v - 0.55) / 0.20) ** 2)
    tp.add_light(c, (90, 170, 50), 0.06 * sheen)
    # A soft vignette.
    vig = tp.smooth(((u - 0.5) / 0.62) ** 2 + ((v - 0.42) / 0.72) ** 2)
    c[..., :3] = tp.mix(c[..., :3], (6, 30, 4), 0.45 * vig)
    # The blades, innermost first so the outer ones lie over them: a green
    # one with a lime rim, a bright silver one, a dark steel one.
    # Kept to the edges RiftWii leaves clear: a 4:3 menu's text and buttons
    # start 40 pixels in, a widescreen one's about 50.
    s = 1.0 if W >= 856 else 0.6
    blade(c, x, y, W, H, 50 * s, 26 * s, (70, 168, 40), (24, 92, 14), rim_rgb=LIME, rim_a=0.9, shine=0.35, cast=0.40)
    blade(c, x, y, W, H, 40 * s, 22 * s, (226, 232, 228), (128, 138, 132), rim_rgb=(255, 255, 255), rim_a=0.9,
          shine=0.55, cast=0.45)
    blade(c, x, y, W, H, 24 * s, 14 * s, (124, 132, 128), (62, 68, 66), rim_rgb=(210, 216, 212), rim_a=0.7,
          shine=0.30, cast=0.40)
    return c


def background(W):
    return tp.to_image(wall(W, 480), opaque=True)


# --- glass -----------------------------------------------------------------------

def glass(cw, ch, x0, y0, w, h, r, *, lit=False, primary=False, silver=False, alpha=0.92, circle=False,
          shadow=0.45, glow_size=3, gloss=0.10, bands=None, bezel=0.0):
    """A glossy glass shape in a cw x ch canvas: a soft shadow; a green body
    (silver for `silver`), lighter at the top; a gloss cap over its upper
    third; a fine bright rim. Lit: a brighter body, a lime and white rim and
    a soft glow round it. `bezel` adds a silver ring that many pixels wide
    round the edge (the round buttons)."""
    x, y = tp.grid(cw, ch)
    if circle:
        d = tp.circle(x, y, x0 + w / 2, y0 + h / 2, w / 2)
    else:
        d, _, _ = tp.rrect(x, y, x0, y0, w, h, r)
    cov = tp.coverage(d)
    c = tp.canvas(cw, ch)
    if shadow > 0:
        tp.over(c, (0, 0, 0), tp.shadow_of(cov, 2, shadow, dy=2)[..., 3])
    if lit:
        g = tp.canvas(cw, ch)
        g[..., :3] = GLOW
        g[..., 3] = cov
        g = tp.blur(g, tp.fit_radius(cov, glow_size))
        tp.over(c, GLOW, g[..., 3] * 0.95)
    v = np.clip((y - y0) / h, 0, 1)
    if silver:
        top, foot = ((255, 255, 255), (196, 204, 198)) if lit else (SILVER_TOP, SILVER_FOOT)
    elif lit:
        top, foot = LIT_TOP, LIT_FOOT
    elif primary:
        top, foot = (60, 152, 26), (22, 92, 8)
    else:
        top, foot = GLASS_TOP, GLASS_FOOT
    body = tp.mix(top, foot, tp.smooth(v))
    if bands is not None:
        brgb, k = bands(v, y - y0)
        body = tp.mix(body, brgb, k)
    tp.over(c, body, cov * (1.0 if silver else alpha))
    if bezel > 0:
        ring = tp.coverage(d) - tp.coverage(d - bezel)
        bv = tp.smooth(v)
        tp.over(c, tp.mix((250, 252, 250), (120, 130, 124), bv), ring)
        tp.over(c, (60, 66, 62), np.clip(1.0 - np.abs(d - bezel) / 0.8, 0, 1) * cov * 0.6)
        d_in = d - bezel
    else:
        d_in = d
    # The gloss cap: the upper third lit, ending in a soft line.
    cap_v = np.clip((y - y0 - bezel) / max(h - 2 * bezel, 1), 0, 1)
    cap = (1 - tp.smooth((cap_v - 0.30) / 0.10)) * tp.coverage(d_in - 1.5)
    # Kept faint: RiftWii writes white titles over the top of a tile, and
    # the lit one is marked by its rim and glow, not a brighter gloss.
    tp.over(c, (255, 255, 255), cap * (gloss * (0.6 if lit else 1.0)))
    # A faint light pooled at the foot, as through coloured glass.
    foot_light = np.exp(-((cap_v - 0.92) / 0.10) ** 2) * tp.coverage(d_in - 1.0)
    tp.add_light(c, (90, 190, 50) if not silver else (255, 255, 255), foot_light * 0.12)
    # The rim.
    rim = np.clip(1.0 - np.abs(d_in - 0.8) / 0.9, 0, 1) * cov
    if silver:
        tp.over(c, (255, 255, 255) if not lit else LIME, rim * (0.9 if not lit else 1.0))
        if lit:
            tp.over(c, (90, 200, 40), np.clip(2.4 - np.abs(d - 1.6), 0, 1) * cov)
    elif lit:
        tp.over(c, LIME, np.clip(2.6 - np.abs(d_in - 1.8), 0, 1) * cov)
        tp.over(c, (255, 255, 255), np.clip(1.0 - np.abs(d_in - 0.7) / 0.8, 0, 1) * cov)
    else:
        tp.over(c, RIM, rim * (0.75 - 0.35 * v))
        if primary:
            tp.over(c, LIME, np.clip(1.6 - np.abs(d_in - 1.2), 0, 1) * cov * 0.9)
    return c


def img(c):
    return tp.to_image(c)


# --- the pictures ------------------------------------------------------------------

def bar(W):
    """W x 124: a dark green glass footer, a bright rim along its curved top
    and a gloss under it, dipping in the middle under the clock."""
    H = 124
    tops = tp.bar_tops(W)
    x, y = tp.grid(W, H)
    d = y - tops[None, :]
    c = tp.canvas(W, H)
    tp.over(c, (0, 0, 0), 0.40 * np.exp(np.minimum(d, 0) / 3.5) * (d < 0.5))
    k = np.clip((y - 11) / (H - 11), 0, 1)
    body = tp.mix((30, 104, 18), (10, 48, 6), tp.smooth(k))
    tp.over(c, body, tp.coverage(d) * 0.94)
    # The gloss: a lighter band just under the edge, fading down.
    tp.over(c, (150, 220, 100), 0.22 * np.exp(-np.maximum(d - 2, 0) / 9.0) * (d > 1.5))
    tp.over(c, (255, 255, 255), np.clip(1.0 - np.abs(d - 0.7) / 0.9, 0, 1) * 0.85)
    tp.over(c, LIME, np.clip(1.0 - np.abs(d - 2.3) / 0.8, 0, 1) * 0.55)
    return tp.to_image(c)


def tile(lit=False):
    return img(glass(140, 100, 8, 8, 124, 84, 11, lit=lit))


def tile_empty():
    c = tp.canvas(140, 100)
    x, y = tp.grid(140, 100)
    d, _, _ = tp.rrect(x, y, 8, 8, 124, 84, 11)
    cov = tp.coverage(d)
    tp.over(c, (6, 34, 4), cov * 0.35)
    tp.over(c, RIM, np.clip(1.0 - np.abs(d - 0.8) / 0.9, 0, 1) * cov * 0.30)
    return tp.to_image(c)


def cover_tile(lit=False):
    """96x128: a game box. The cover (80x112) is drawn over its middle; the
    case shows round it as green plastic with a moulded edge."""
    W, H = 96, 128
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    d, _, _ = tp.rrect(x, y, 2, 2, 92, 124, 7)
    cov = tp.coverage(d)
    tp.over(c, (0, 0, 0), tp.shadow_of(cov, 2, 0.45, dy=2)[..., 3])
    if lit:
        g = tp.canvas(W, H)
        g[..., :3] = GLOW
        g[..., 3] = cov
        g = tp.blur(g, tp.fit_radius(cov, 2))
        tp.over(c, GLOW, g[..., 3])
    v = np.clip((y - 2) / 124, 0, 1)
    case = tp.mix((88, 190, 44), (36, 122, 18), tp.smooth(v)) if lit else \
        tp.mix((70, 168, 34), (26, 104, 12), tp.smooth(v))
    tp.over(c, case, cov)
    # The moulded edge: a highlight just inside the case's edge, and a
    # brighter strip down its left, where the hinge is.
    tp.over(c, (255, 255, 255), np.clip(1.0 - np.abs(d - 1.0) / 0.9, 0, 1) * cov * 0.55)
    tp.over(c, (200, 255, 150), np.exp(-((x - 4.5) / 1.2) ** 2) * cov * 0.35)
    # The sleeve under the clear front: plain and dark, for the rare tile
    # without even a made-up cover (RiftWii writes the name on it in white).
    sd, _, _ = tp.rrect(x, y, 8, 8, 80, 112, 1.5)
    sleeve = tp.coverage(sd)
    sv = np.clip((y - 8) / 112, 0, 1)
    tp.over(c, tp.mix((70, 76, 70), (40, 46, 40), tp.smooth(sv)), sleeve)
    # The clear front's sheen: a soft diagonal light.
    tp.over(c, (255, 255, 255), np.exp(-((x * 0.6 + y - 46) / 10) ** 2) * sleeve * 0.07)
    if lit:
        tp.over(c, LIME, np.clip(2.4 - np.abs(d - 1.4), 0, 1) * cov)
    return tp.to_image(c)


def blank_cover():
    """160x224: the sleeve of a game without box art, as RiftWii's made-up
    cover prints the name on it: a white band across the top with green
    swooshes sweeping up at its right, a fine green line under it, and a
    plain silver-grey below (light enough that RiftWii writes the name in
    dark ink; the name sits from row 50 to 190, badges along the foot)."""
    W, H = 160, 224
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    v = y / H
    # Light enough for the ID RiftWii prints under the name in its dimmer
    # dark ink, as well as for the name.
    grey = tp.mix((228, 230, 228), (210, 212, 210), tp.smooth(v))
    c[..., :3] = grey
    c[..., 3] = 1.0
    # A soft light down the middle, as printed card under clear plastic.
    tp.add_light(c, (255, 255, 255), 0.06 * np.exp(-((x - 70) / 50) ** 2))
    # The band is shallow over its left edge, which RiftWii stretches down
    # the box's spine (columns 3-15, the name from row 16), and deepens
    # towards the swooshes.
    band_h = 13.0 + 19.0 * tp.smooth((x - 26.0) / 52.0)
    band = np.clip(band_h - y + 0.5, 0, 1)
    tp.over(c, (252, 253, 252), band)
    # The swooshes: bands between curves that lean right as they rise,
    # dark green to lime to yellow-green, over a white-to-lime fade.
    lean = (32.0 - y) * 0.55
    bow = 6.0 * np.sin(np.clip(y / 32.0, 0, 1) * math.pi)
    xs = x - lean + bow
    tp.over(c, (196, 232, 150), np.clip((xs - 84) / 26, 0, 1) * band)
    for left, rgb in ((104, (16, 110, 26)), (110, (60, 156, 30)), (118, (20, 96, 22)),
                      (124, (96, 180, 34)), (133, (34, 130, 26)), (140, (150, 204, 40)),
                      (149, (196, 220, 60))):
        tp.over(c, rgb, np.clip(xs - left + 0.5, 0, 1) * band)
    tp.over(c, (60, 156, 30), np.clip(1.2 - np.abs(y - band_h - 0.6), 0, 1))
    return tp.to_image(c, opaque=True)


def blank_back():
    """160x224: the back of that game's box: the same silver-grey, the cover's
    crops set at the top by RiftWii (tp.BACK_PICTURES), and below them two
    rows of white feature tags with green and orange marks, lines of white
    small print, a warning label, a rating box and a barcode."""
    W, H = 160, 224
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    c[..., :3] = tp.mix((224, 226, 224), (204, 206, 204), tp.smooth(y / H))
    c[..., 3] = 1.0
    tp.add_light(c, (255, 255, 255), 0.05 * np.exp(-((x - 60) / 60) ** 2))
    # A faint frame where the pictures go, as printed borders.
    # The picture places, printed as soft grey panels with a white edge;
    # RiftWii covers them with box art, or the big one with the name.
    for x0, y0, x1, y1 in tp.BACK_PICTURES:
        tp.box(c, x0 - 1, y0 - 1, x1 + 1, y1 + 1, (244, 246, 244), 0.95)
        tp.box(c, x0, y0, x1, y1, (150, 154, 150))
        yy = np.clip((y - y0) / max(y1 - y0, 1), 0, 1)
        tp.over(c, (120, 170, 96), 0.35 * tp.smooth(yy) * ((x >= x0) & (x < x1) & (y >= y0) & (y < y1)))
    # Feature tags: white, each with a coloured line of "text".
    row1 = ((13, 30, (60, 156, 30)), (34, 54, (214, 92, 30)))
    row2 = ((13, 31, (60, 156, 30)), (34, 58, (60, 156, 30)), (61, 80, (60, 156, 30)),
            (83, 101, (214, 92, 30)), (104, 122, (214, 92, 30)), (125, 147, (60, 156, 30)))
    for row_y, row in ((153.0, row1), (161.0, row2)):
        for x0, x1, rgb in row:
            tp.box(c, x0, row_y, x1, row_y + 5.5, (250, 250, 250), r=0.8)
            tp.box(c, x0 + 2, row_y + 2.2, x1 - 2, row_y + 3.4, rgb)
    tp.text_lines(c, 13, 171, (134, 128, 131, 96), (246, 246, 246), a=0.85, gap=2.6, thick=0.9)
    tp.text_lines(c, 13, 183, (70, 66), (246, 246, 246), a=0.7, gap=2.6, thick=0.9)
    # The warning label: white, an orange tab with a dark triangle.
    tp.box(c, 13, 204, 64, 216, (255, 255, 255))
    tp.box(c, 13.5, 204.5, 26, 209.5, (236, 128, 28))
    tp.triangle(c, ((16, 208.6), (18.8, 205.2), (21.6, 208.6)), (40, 30, 20))
    tp.text_lines(c, 28, 206, (32, 34), (40, 40, 40), a=0.9, gap=3.2, thick=0.8)
    # The rating box: a white card in a black frame, a black block in it.
    tp.box(c, 68, 190, 92, 216, (20, 20, 20))
    tp.box(c, 69.2, 191.2, 90.8, 214.8, (255, 255, 255))
    tp.box(c, 71, 194, 86, 207, (20, 20, 20))
    tp.box(c, 71, 209, 89, 213, (20, 20, 20))
    tp.barcode(c, 98, 194, 147, 216, seed=360)
    return tp.to_image(c, opaque=True)


def tile9(lit=False, primary=False):
    return img(glass(64, 64, 8, 8, 48, 48, 11, lit=lit, primary=primary))


def pill(lit=False, primary=False):
    return img(glass(252, 60, 4, 4, 244, 52, 22, lit=lit, primary=primary, glow_size=2, shadow=0.35))


def home_button(lit=False):
    # Silver: RiftWii writes the HOME Menu's labels in dark ink.
    return img(glass(264, 88, 8, 8, 248, 72, 30, lit=lit, silver=True, glow_size=4, gloss=0.30))


def round_button(lit=False):
    return img(glass(80, 80, 3, 3, 74, 74, 37, lit=lit, circle=True, glow_size=2, shadow=0.35, bezel=5.5,
                     gloss=0.20))


def chip(on):
    c = glass(212, 36, 2, 2, 208, 30, 9, primary=on, alpha=0.85, shadow=0.25, gloss=0.10)
    return img(c)


def row_focus():
    """548x44: the lit row of a list: brighter green glass with a lime and
    white rim."""
    return img(glass(548, 44, 1.5, 1.5, 545, 41, 10, lit=True, shadow=0.0, glow_size=1))


def round_arrow(direction, lit):
    """44x44: a small silver orb with a green chevron."""
    c = glass(44, 44, 4, 4, 34, 34, 17, lit=lit, silver=True, circle=True, glow_size=2, shadow=0.3, gloss=0.25)
    tp.chevron(c, 22, 22, 12, direction, rgb=(30, 120, 20) if lit else (40, 96, 34), width=3.0)
    return img(c)


def switch(on):
    cw, ch = 68, 40
    x, y = tp.grid(cw, ch)
    c = tp.canvas(cw, ch)
    d, _, _ = tp.rrect(x, y, 4, 6, 60, 28, 14)
    cov = tp.coverage(d)
    tp.over(c, (0, 0, 0), tp.shadow_of(cov, 2, 0.35)[..., 3])
    v = np.clip((y - 6) / 28, 0, 1)
    body = tp.mix((150, 226, 80), (62, 160, 28), tp.smooth(v)) if on else \
        tp.mix((26, 70, 16), (12, 44, 8), tp.smooth(v))
    tp.over(c, body, cov)
    tp.over(c, (0, 0, 0), np.exp(-((d - 0.4) / 1.4) ** 2) * (1 - v) * cov * 0.35)
    tp.over(c, RIM, np.clip(1.0 - np.abs(d - 0.8) / 0.9, 0, 1) * cov * 0.7)
    kx = 50 if on else 18
    kd = tp.circle(x, y, kx, 20, 12)
    kc = tp.coverage(kd)
    tp.over(c, (0, 0, 0), tp.shadow_of(kc, 2, 0.4)[..., 3])
    kv = np.clip((y - 8) / 24, 0, 1)
    tp.over(c, tp.mix(SILVER_TOP, SILVER_FOOT, tp.smooth(kv)), kc)
    tp.over(c, (255, 255, 255), (1 - tp.smooth((kv - 0.35) / 0.1)) * tp.coverage(kd - 1.5) * 0.4)
    tp.over(c, (255, 255, 255), np.clip(1.0 - np.abs(kd - 0.8) / 0.9, 0, 1) * kc * 0.8)
    return tp.to_image(c)


def panel(h):
    return img(glass(580, h, 4, 4, 572, h - 8, 16, alpha=0.70, gloss=0.07, shadow=0.40))


def popup():
    def bands(v, yy):
        head = yy < 44
        foot = yy > 276 - 18
        rgb = np.where(head[..., None], np.asarray((48, 140, 26), np.float32),
                       np.asarray((14, 64, 8), np.float32))
        k = np.where(head | foot, 0.9, 0.0)
        line = np.abs(yy - 44) < 0.8
        rgb = np.where(line[..., None], np.asarray(LIME, np.float32), rgb)
        k = np.where(line, 0.85, k)
        return rgb, k

    return img(glass(580, 284, 4, 4, 572, 276, 16, alpha=0.95, gloss=0.08, shadow=0.45, bands=bands))


def banner_stripes():
    """640x192: over a game banner while it shows the game's colour: a green
    veil, darker at the foot, with soft gloss on its upper part and a lime
    line along the band's foot (plain across, so copies join)."""
    W, H = 640, 192
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    band = (y < 128).astype(np.float32)
    k = tp.smooth(y / 122)
    tp.over(c, tp.mix((60, 150, 30), (10, 50, 6), k), band * (0.22 + 0.30 * k))
    tp.over(c, (255, 255, 255), band * 0.10 * (1 - tp.smooth((y - 40) / 12)))
    tp.over(c, LIME, (np.abs(y - 122) < 1.0) * 0.8)
    return tp.to_image(c)


def page_arrow(direction, lit):
    """48x48: Home's page buttons: glossy silver triangles; lit, a lime glow."""
    c = tp.canvas(48, 48)
    pts = ((34, 8), (13, 24), (34, 40)) if direction == "left" else ((14, 8), (35, 24), (14, 40))
    if lit:
        g = tp.canvas(48, 48)
        tp.triangle(g, pts, GLOW, 1.0)
        g = tp.blur(g, 3)
        tp.over(c, GLOW, g[..., 3])
    sh = tp.canvas(48, 48)
    tp.triangle(sh, [(px, py + 2) for px, py in pts], (0, 0, 0), 0.5)
    sh = tp.blur(sh, 1)
    tp.over(c, (0, 0, 0), sh[..., 3])
    body = tp.canvas(48, 48)
    tp.triangle(body, pts, (255, 255, 255), 1.0)
    x, y = tp.grid(48, 48)
    shade = tp.mix((255, 255, 255) if lit else SILVER_TOP, (190, 200, 192) if lit else SILVER_FOOT,
                   tp.smooth((y - 8) / 32))
    tp.over(c, shade, body[..., 3])
    tp.over(c, (255, 255, 255), body[..., 3] * 0.35 * (y < 24))
    return tp.to_image(c)


def shelf():
    """256x64: a green glass shelf: its top (rows 0-47) lighter towards the
    front, its front edge (48-63) a bright rim over dark green. Plain across."""
    W, H = 256, 64
    x, y = tp.grid(W, H)
    c = tp.canvas(W, H)
    top = y < 48
    k = y / 48
    tp.over(c, tp.mix((20, 80, 10), (70, 166, 36), tp.smooth(k)), top * (0.45 + 0.4 * k))
    front = ~top
    fk = (y - 48) / 16
    tp.over(c, tp.mix((60, 150, 30), (8, 40, 4), tp.smooth(fk)), front * 0.95)
    tp.over(c, (255, 255, 255), (np.abs(y - 48.5) < 1.0) * 0.85)
    return tp.to_image(c)


# --- sound -----------------------------------------------------------------------

RATE = 32000


def blip(freq, dur, sweep=0.0, decay=0.05, vol=1.0):
    """A soft glassy tone: a sine and a quieter fifth, gliding by `sweep`
    (a fraction of its pitch) as it fades."""
    n = int(RATE * dur)
    t = np.arange(n) / RATE
    f = freq * (1.0 + sweep * t / dur)
    phase = TAU * np.cumsum(f) / RATE
    s = np.sin(phase) + 0.30 * np.sin(1.5 * phase) + 0.10 * np.sin(3 * phase)
    env = np.clip(t / 0.004, 0, 1) * np.exp(-t / decay)
    return vol * s * env


def sounds(out):
    hover = 0.22 * blip(1760, 0.07, sweep=0.08, decay=0.018)
    click = 0.30 * blip(1175, 0.20, sweep=-0.10, decay=0.06)
    click[640:640 + 4800] += 0.20 * blip(1760, 0.15, decay=0.05)[:4800]
    up = np.zeros(int(RATE * 0.6))
    down = np.zeros(int(RATE * 0.6))
    for k, f in enumerate((880, 1109, 1319, 1760)):
        note = 0.20 * blip(f, 0.35, decay=0.10)
        up[k * 1800:k * 1800 + note.size] += note[:up.size - k * 1800]
        g = (1760, 1319, 1109, 880)[k]
        note = 0.20 * blip(g, 0.35, decay=0.10)
        down[k * 1800:k * 1800 + note.size] += note[:down.size - k * 1800]
    for name, s in (("hover", hover), ("click", click), ("home_open", up), ("home_close", down)):
        tp.write_wav(out / f"{name}.wav", s, RATE)


def main():
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / "build-themes" / "Dashboard"
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
    unknown = sorted(only - set(pictures) - {"sounds"})
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
    if not only or "sounds" in only:
        sounds(out)
        print("sounds")
    if written:
        shrinker = str(HERE / "shrink_theme_pictures.py")
        subprocess.run([sys.executable, shrinker, "--"] + written, check=True)


if __name__ == "__main__":
    main()
