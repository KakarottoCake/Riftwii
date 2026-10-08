#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the Bookshelf sample theme: a light wooden bookcase. Every picture
is worked out here (wood grain from layered waves and noise, no photo or
picture from anywhere else):

- background.png: a wall of wooden planks with two shelves, one under each
  row of covers on Home, so the covers stand on them;
- cover_tile.png / cover_tile_over.png: a plain paper book standing on
  the shelf, hidden by the cover drawn over it (and a warm glow when lit);
- bar.png: Home's bottom bar as the bookcase's base, dipping under the
  clock as the Wii Menu's does, its curve edged in brass;
- shelf.png: the wood for the shelf view's plank;
- background_shelf.png: the same wall for the shelf view, with only the
  lower shelf (the boxes stand on it; the upper one would cross them);
- background_plain.png: the same wall with no shelves, behind every
  screen but Home (Settings' panels with nothing to stand on);
- background_channels.png: the same wall with a shelf under each of the
  Channels view's three rows, its animated channels standing on them;
- background_wide.png / bar_wide.png / background_shelf_wide.png /
  background_plain_wide.png / background_channels_wide.png: the same,
  856 across, for a widescreen menu;
- theme.ini: cream paper panels, dark brown ink, and the games' own
  colours mixed with walnut (their banners and plain spines).

    python tools/make_bookshelf_theme.py      (needs numpy and Pillow)
"""
import math
import pathlib

import numpy as np
from PIL import Image, ImageFilter

OUT = pathlib.Path(__file__).resolve().parent.parent / "themes" / "Bookshelf"
RNG = np.random.default_rng(1206)

# Where the random numbers stood before each wall (background()), so the
# shelf view's walls get the same planks.
WALL_STATE = {}

# Home's cover rows (wii/gui_gamegrid.cpp: covers 80x112, top 16, 14 apart).
COVER_ROW_BOTTOMS = (16 + 112, 16 + 112 + 14 + 112)
# Home's channels: three rows of 84 from y 24, 12 apart (wii/gui_gamegrid.cpp).
CHANNEL_ROW_BOTTOMS = (24 + 84, 24 + 84 + 12 + 84, 24 + 3 * 84 + 2 * 12)

INI = """[theme]
name = Bookshelf
author = RiftWii

[shape]
corners = 1
gloss = yes
# The HOME Menu as iOS 6 drew things (iBooks kept its books on a wooden shelf).
home_menu = ios6

[backdrop]
stripes = no

[colors]
# Text: dark brown ink, on paper and on the wood
ink = #33200F
ink_soft = #4F3520
ink_dim = #5E4127
clock = #3F2814
accent = #8E4A1C
accent_ink = #6E3510
text_on_accent = #FFF8EC
warn = #A8321E

# Paper panels and buttons
card = #FBF6EC
card_edge = #B89E7C
card_edge_strong = #9E8462
shadow = #2A16064D
glow = #E2A24E70
glyph = #4F3520

# Option chips and switches
chip_on = #F3DFC0
chip_off = #F5EEE1
chip_off_edge = #C2AA88
switch_off = #D5C6AE

# Background and bars
bar = #B88D5A
backdrop = #C9A06A
backdrop_stripe = #C9A06A
banner_stripe = #FFF4E014
# The games' own colours, half walnut: cloth bindings on the shelf
banner_tint = #8A5A2E73

# Lists and badges
divider = #E6DCCB
scroll_track = #E3D6C1
scroll_thumb = #A88B66
badge = #F5EEE1
shelf = #B9874F
shelf_edge = #8A5A2E

# Player pointers
pointer1 = #3B8FD6
pointer2 = #D64545
pointer3 = #3FA34D
pointer4 = #D9A21B
"""


def wood(w, h, light, dark, phase=0.0, ring=0.085, wave=7.0):
    """Horizontal wood grain, w x h, as float RGB in 0..255: broad soft
    bands, fine lines along them, and dark pores here and there."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    # The grain wanders gently along the board.
    g = y + 0.35 * wave * np.sin(x * 0.006 + phase) + 0.2 * wave * np.sin(x * 0.021 + y * 0.04 + 2 * phase)
    bands = 0.5 + 0.5 * np.sin(2 * math.pi * (g * ring * 0.55 + 0.35 * np.sin(x * 0.0021 + phase)))
    lines = 0.5 + 0.5 * np.sin(2 * math.pi * g * 0.42 + 1.3 * np.sin(x * 0.013 + phase))
    t = 0.42 * bands ** 1.6 + 0.16 * lines ** 4
    # Fibres: each row a little lighter or darker, drifting along it.
    rows = np.repeat(RNG.normal(0, 1, (h, 1)), w, axis=1)
    drift = np.cumsum(RNG.normal(0, 0.08, (h, w)), axis=1)
    drift -= drift.mean(axis=1, keepdims=True)
    t += 0.05 * rows + 0.03 * drift
    # Pores: short dark dashes along the grain.
    pores = (RNG.random((h, w)) < 0.004).astype(np.float64)
    pores = np.maximum(pores, np.roll(pores, 1, axis=1))
    pores = np.maximum(pores, np.roll(pores, 2, axis=1))
    t += 0.35 * pores
    t = np.clip(t + RNG.normal(0, 0.02, (h, w)), 0, 1)[..., None]
    light = np.array(light, dtype=np.float64)
    dark = np.array(dark, dtype=np.float64)
    return light * (1 - t) + dark * t


def shade(img, y0, y1, top, bottom):
    """Multiplies rows y0..y1 by a factor going from `top` to `bottom`."""
    n = y1 - y0
    if n <= 0:
        return
    f = np.linspace(top, bottom, n)[:, None, None]
    img[y0:y1] *= f


def background(W=640, shelves=COVER_ROW_BOTTOMS):
    H = 480
    img = np.zeros((H, W, 3))
    # The wall: planks 60 high, each its own cut of the wood.
    y = 0
    while y < H:
        ph = min(60, H - y)
        tone = RNG.uniform(-10, 10)
        plank = wood(W, ph, (214 + tone, 176 + tone, 122 + tone), (176 + tone, 132 + tone, 84 + tone),
                     phase=RNG.uniform(0, 6))
        img[y:y + ph] = plank
        # The seam: a dark groove with a lit edge under it.
        img[y:y + 1] *= 0.62
        if y + 1 < H:
            img[y + 1:y + 2] *= 1.08
        y += ph
    # Light from above, a little darker at the sides.
    yy, xx = np.mgrid[0:H, 0:W]
    vign = 1.0 - 0.18 * ((xx - W / 2) / (W / 2)) ** 2 - 0.10 * (yy / H)
    img *= vign[..., None]
    # The shelves: a top the covers stand on, seen from a little above,
    # a front edge filling the gap to the next row, and a deep shadow.
    for bottom in shelves:
        top_face, lip = 8, 10
        y0 = bottom - top_face + 4
        img[y0:y0 + top_face] = wood(W, top_face, (232, 196, 142), (204, 162, 108), phase=bottom * 0.1, wave=2)
        shade(img, y0, y0 + top_face, 0.80, 1.02)
        ly = y0 + top_face
        img[ly:ly + lip] = wood(W, lip, (176, 128, 78), (146, 100, 56), phase=bottom * 0.2, wave=2)
        img[ly:ly + 1] = np.minimum(img[ly:ly + 1] * 1.25, 255)   # the lit front corner
        shade(img, ly + 1, ly + lip, 1.0, 0.78)
        img[ly + lip - 1:ly + lip] *= 0.6
        sh = 26
        f = 1.0 - 0.55 * (1 - np.linspace(0, 1, sh)) ** 1.8
        img[ly + lip:ly + lip + sh] *= f[:, None, None]
    return np.clip(img, 0, 255).astype(np.uint8)


def cover_tile(over):
    """96x128: a plain paper book (80x112 at (7, 7)) standing on the shelf,
    with its shadow. A cover is drawn over the book and hides it, so only a
    game without a cover shows the paper, its name written on it."""
    shadow = Image.new("L", (96, 128), 0)
    sp = shadow.load()
    for yy in range(11, 121):
        for xx in range(10, 90):
            sp[xx, yy] = 150
    shadow = np.array(shadow.filter(ImageFilter.GaussianBlur(3.2))).astype(np.float64) / 255
    rgba = np.zeros((128, 96, 4))
    rgba[..., 0:3] = (40, 22, 8)
    rgba[..., 3] = shadow * 255
    # The book: cream paper, a faint darker edge, a spine shade on the left.
    book = np.zeros((112, 80, 4))
    book[..., 0:3] = (246, 238, 222)
    book[..., 3] = 255
    book[:, 0:5, 0:3] *= np.linspace(0.86, 1.0, 5)[None, :, None]
    book[0, :, 0:3] *= 0.85
    book[-1, :, 0:3] *= 0.8
    book[:, -1, 0:3] *= 0.85
    rgba[7:119, 7:87] = book
    if over:
        # A warm glow around the lit book.
        g = Image.new("L", (96, 128), 0)
        gp = g.load()
        for yy in range(4, 123):
            for xx in range(4, 91):
                gp[xx, yy] = 255
        g = np.array(g.filter(ImageFilter.GaussianBlur(2.5))).astype(np.float64) / 255
        a_g = (np.clip(g * 1.6, 0, 1) * 0.8)[..., None]
        a_s = rgba[..., 3:4] / 255.0
        out_a = a_s + a_g * (1 - a_s)
        out_rgb = (rgba[..., 0:3] * a_s + np.array((236, 170, 80)) * a_g * (1 - a_s)) / np.maximum(out_a, 1e-6)
        rgba = np.concatenate([out_rgb, out_a * 255], axis=2)
    return rgba.clip(0, 255).astype(np.uint8)


def bar(W=640):
    """W x 124: Home's bottom bar, the same curve RiftWii paints (src/skinpaint.cpp):
    high at the sides, dipping over the middle 58% where the clock sits."""
    H = 124
    top = np.zeros(W)
    dip = W * 0.58
    dip_left = W / 2 - dip / 2

    def ease(v):
        v = min(max(v, 0.0), 1.0)
        return v * v * (3 - 2 * v)

    for x in range(W):
        t = (x + 0.5 - dip_left) / dip
        d = min(ease(t / 0.3), ease((1 - t) / 0.3)) if 0 < t < 1 else 0.0
        top[x] = 11.0 + 51.0 * d
    body = wood(W, H, (190, 142, 90), (158, 112, 66), phase=2.0, wave=4)
    yy = np.arange(H)[:, None].astype(np.float64)
    inside = np.clip(yy - top[None, :] + 0.5, 0, 1)            # anti-aliased curve
    rgba = np.zeros((H, W, 4))
    rgba[..., 0:3] = body * (1.0 - 0.12 * (yy / H))[..., None]
    rgba[..., 3] = inside * 255
    # A brass edge along the curve, and a soft shadow just above it.
    d = yy - top[None, :]
    brass = np.clip(1.3 - np.abs(d - 1.2), 0, 1)
    rgba[..., 0:3] = rgba[..., 0:3] * (1 - brass[..., None]) + np.array((214, 168, 92)) * brass[..., None]
    above = np.clip(1 - (-d) / 5.0, 0, 1) * (d < 0)
    rgba[..., 3] = np.maximum(rgba[..., 3], above * 40)
    return rgba.clip(0, 255).astype(np.uint8)


def shelf_picture():
    """256x64 for the shelf view: the top (rows 0-47), then the front edge."""
    # 32 more across than kept: the extra wood is faded in over the first 32
    # columns, so column 0 carries on from column 255 and the copies along
    # the plank join with no seam.
    top = wood(288, 48, (226, 188, 132), (196, 152, 98), phase=1.0, wave=3)
    shade_rows = np.linspace(0.8, 1.0, 48)[:, None, None]
    top *= shade_rows
    edge = wood(288, 16, (186, 140, 88), (150, 104, 60), phase=2.5, wave=2)
    edge[0] *= 1.18
    edge *= np.linspace(1.0, 0.8, 16)[:, None, None]
    img = np.concatenate([top, edge], axis=0)
    ramp = (np.arange(32) / 32.0)[None, :, None]
    out = img[:, :256].copy()
    out[:, :32] = img[:, :32] * ramp + img[:, 256:288] * (1 - ramp)
    return np.clip(out, 0, 255).astype(np.uint8)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "theme.ini").write_bytes(INI.replace("\n", "\r\n").encode("utf-8"))
    WALL_STATE[640] = RNG.bit_generator.state
    Image.fromarray(background()).save(OUT / "background.png", optimize=True)
    Image.fromarray(cover_tile(False)).save(OUT / "cover_tile.png", optimize=True)
    Image.fromarray(cover_tile(True)).save(OUT / "cover_tile_over.png", optimize=True)
    Image.fromarray(bar()).save(OUT / "bar.png", optimize=True)
    Image.fromarray(shelf_picture()).save(OUT / "shelf.png", optimize=True)
    # Last, so the pictures above come out as before.
    WALL_STATE[856] = RNG.bit_generator.state
    Image.fromarray(background(856)).save(OUT / "background_wide.png", optimize=True)
    Image.fromarray(bar(856)).save(OUT / "bar_wide.png", optimize=True)
    # The shelf view's walls: the same planks (the generator back where it
    # was for each wall), only the lower shelf.
    for name, w in (("background_shelf.png", 640), ("background_shelf_wide.png", 856)):
        RNG.bit_generator.state = WALL_STATE[w]
        Image.fromarray(background(w, shelves=COVER_ROW_BOTTOMS[1:])).save(OUT / name, optimize=True)
    # And with none, for the other screens.
    for name, w in (("background_plain.png", 640), ("background_plain_wide.png", 856)):
        RNG.bit_generator.state = WALL_STATE[w]
        Image.fromarray(background(w, shelves=())).save(OUT / name, optimize=True)
    for name, w in (("background_channels.png", 640), ("background_channels_wide.png", 856)):
        RNG.bit_generator.state = WALL_STATE[w]
        Image.fromarray(background(w, shelves=CHANNEL_ROW_BOTTOMS)).save(OUT / name, optimize=True)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
