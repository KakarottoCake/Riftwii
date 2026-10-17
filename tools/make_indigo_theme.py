#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes Indigo, a RiftWii menu theme: indigo and violet glass on a fine
dark grid, with gold for whatever is focused.

Every picture is worked out here from arithmetic alone (no photo, texture,
screenshot or picture from anywhere else):

- background.png / background_wide.png: a deep purple-black wall with a
  fine grid and a soft vignette, a faint violet glow at its top;
- bar.png / bar_wide.png: Home's bottom bar as a purple glass strip with a
  light top edge, dipping under the clock (the curve RiftWii paints);
- tile / tile_over: the names view's tiles as bevelled glass blocks, the
  lit one gold glass ringed in gold;
- tile_empty: an empty place, a small blue glass cube in a sunken slot;
- cover_tile / cover_tile_over: a glass card behind each cover;
- tile9 (and _over, _primary, _primary_over): the game page's and
  Settings' tiles (and the prompts' cards), small glass blocks;
- pill (and _over, _primary, _primary_over): the wide buttons, dim
  silver-violet glass, amber-gold when lit;
- home_button / home_button_over: the HOME Menu's buttons, pale silver,
  bright gold when lit;
- round_button / round_button_over: Home's two round buttons;
- panel_game / panel_settings: glass panels, purple to blue, a thin light
  rim and soft rainbow edges;
- popup: the same, with a deeper band for the title and a purple info strip
  with a light top edge along the bottom;
- banner_stripes: over the game page's band and the launch screen's, the
  info strip's look (a purple gradient, a light line near its foot);
- theme.ini and LICENSE.

    python3 tools/make_indigo_theme.py [out_dir [picture ...]]     (needs Pillow only)

The default out_dir is themes/Indigo in the repository. The same folder
goes to the community themes repository unchanged. Naming pictures
repaints only those.
"""
import colorsys
import math
import pathlib
import sys

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent

# The palette.
GOLD = (245, 196, 49)
GOLD_DEEP = (190, 132, 20)
RIM = (214, 206, 255)        # the thin light rim

# What every lit tile is filled with: gold-tinted glass, bronze enough that
# the engine's white names (and the gold of Play's, accent_ink) stay readable
# on it, since the engine draws no other text colour on a lit tile.
GOLD_GLASS = dict(body_tl=(180, 128, 28), body_br=(110, 68, 12), sheen=0.20, reflection=0.10, rainbow_edge=0.0)
GOLD_GLASS_PRIMARY = dict(body_tl=(136, 90, 16), body_br=(78, 46, 8), sheen=0.20, reflection=0.10, rainbow_edge=0.0)

INI = """[theme]
name = Indigo
author = RiftWii contributors

[shape]
# Crisper corners than the default, as bevelled glass blocks.
corners = 0.6
gloss = yes
bar = dip
shelf_grain = no
home_menu = wii

[backdrop]
# background.png (a fine grid with a vignette) replaces the backdrop.
stripes = no

[colors]
# Text: white and pale lavender on the dark glass and the dark wall
ink = #F5F2FF
ink_soft = #D9D3F7
ink_dim = #A9A1D8
clock = #CFC8F4
# Gold marks what is focused
accent = #F5C431
accent_ink = #FFD966
text_on_accent = #2A1E04
warn = #FF7A7A

# Glass tiles, buttons and panels
card = #2C2463
card_edge = #7C72C8
card_edge_strong = #A39BE0
shadow = #00000088
glow = #F5C43170
glyph = #DCD6FA

# Option chips and switches
chip_on = #3E2F74
chip_off = #231C50
chip_off_edge = #554C98
switch_off = #3A3270

# Background and bars
bar = #2A2068
backdrop = #0C0919
backdrop_stripe = #0C0919
banner_stripe = #FFFFFF14
# The games' own colours, a little toward indigo
banner_tint = #1C145059

# Lists and badges
divider = #3C3478
scroll_track = #1C1742
scroll_thumb = #7068B8
badge = #3A3274
shelf = #2E2766
shelf_edge = #1C1746

# Player pointers
pointer1 = #3B8FD6
pointer2 = #E05252
pointer3 = #48B858
pointer4 = #E5AC24
"""

LICENSE = """License: CC0-1.0
Author: RiftWii contributors

Indigo, a theme for RiftWii: indigo and violet glass on a fine dark grid,
gold for what is focused.

To the extent possible under law, the author has waived all copyright and
related or neighboring rights to this theme (theme.ini and every PNG
picture in its folder), under Creative Commons CC0 1.0 Universal:
https://creativecommons.org/publicdomain/zero/1.0/

The pictures were drawn by RiftWii's tools/make_indigo_theme.py
(https://github.com/KakarottoCake/Riftwii) from arithmetic alone: no
source images, textures or fonts. The theme keeps RiftWii's own sounds and
music.

Third-party parts: none.
"""


def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def mix(a, b, k):
    return tuple(a[i] + (b[i] - a[i]) * k for i in range(3))


def smooth(v):
    v = clamp(v)
    return v * v * (3 - 2 * v)


class Layer:
    """A float RGBA picture, each shape composited "over" what is there."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[0.0, 0.0, 0.0, 0.0] for _ in range(w * h)]

    def over(self, x, y, rgb, a):
        if a <= 0.0:
            return
        p = self.px[y * self.w + x]
        da = p[3]
        oa = a + da * (1 - a)
        if oa <= 0:
            return
        for i in range(3):
            p[i] = (rgb[i] * a + p[i] * da * (1 - a)) / oa
        p[3] = oa

    def image(self):
        out = Image.new("RGBA", (self.w, self.h))
        out.putdata([(int(round(clamp(p[0], 0, 255))), int(round(clamp(p[1], 0, 255))),
                      int(round(clamp(p[2], 0, 255))), int(round(clamp(p[3]) * 255))) for p in self.px])
        return out


def rr(px, py, x0, y0, w, h, r):
    """A rounded rectangle's signed distance at (px, py), positive inside,
    and the outward normal of its nearest edge."""
    r = min(r, w / 2, h / 2)
    cx0, cy0, cx1, cy1 = x0 + r, y0 + r, x0 + w - r, y0 + h - r
    qx = clamp(px, cx0, cx1)
    qy = clamp(py, cy0, cy1)
    dx, dy = px - qx, py - qy
    d = math.hypot(dx, dy)
    if d > 0:
        return r - d, dx / d, dy / d
    # In the core: the nearest straight edge.
    edges = ((px - x0, -1.0, 0.0), (x0 + w - px, 1.0, 0.0), (py - y0, 0.0, -1.0), (y0 + h - py, 0.0, 1.0))
    return min(edges)


def rainbow(angle):
    """A soft hue for the glass's edge: pastel, turning with the angle."""
    r, g, b = colorsys.hsv_to_rgb((angle / (2 * math.pi)) % 1.0, 0.42, 1.0)
    return (r * 255, g * 255, b * 255)


def glass(w, h, margin, radius, *, body_tl, body_br, over=False, alpha=0.94, bevel=5.0, glow=GOLD,
          ring=None, ring_width=0.0, lit_width=2.6, rainbow_edge=0.75, sheen=0.13, shadow=0.55, bands=None, canvas=None,
          reflection=0.07):
    """A bevelled glass shape `w` x `h` at (`margin`, `margin`), in a canvas
    with that margin all round (rounded up to 4 for the Wii's textures, or
    `canvas` (w, h)):

    - a soft shadow under it;
    - its body, `body_tl` at the top left to `body_br` at the bottom right;
    - a chamfer `bevel` wide around it, lit from the top left;
    - a broad sheen across its top left, a thin light rim inside the edge
      and a pastel rainbow along the edge itself;
    - lit (`over`): the `glow` colour's halo around it and a gold ring
      `lit_width` wide;
    - `ring`: a ring of that colour (`ring_width`) whether lit or not;
    - `bands`: a function (u, v, y) -> (rgb, k) mixing a band's colour in.
    """
    cw = (w + 2 * margin + 3) & ~3 if canvas is None else canvas[0]
    ch = (h + 2 * margin + 3) & ~3 if canvas is None else canvas[1]
    layer = Layer(cw, ch)
    x0, y0 = float(margin), float(margin)
    lx, ly = -0.55, -0.83  # towards the light
    for y in range(ch):
        for x in range(cw):
            px, py = x + 0.5, y + 0.5
            d, nx, ny = rr(px, py, x0, y0, w, h, radius)
            # Halo and shadow outside the shape.
            if d < 0.5:
                if over:
                    g = math.exp(-max(0.0, -d) / (margin * 0.38 + 0.4)) * 0.85
                    layer.over(x, y, glow, g * clamp(1.0 - (-d) / (margin + 0.01)))
                if shadow > 0:
                    sd, _, _ = rr(px, py - 2.5, x0, y0, w, h, radius)
                    s = math.exp(-max(0.0, -sd) / 2.2) * shadow
                    layer.over(x, y, (0, 0, 0), s * clamp(1.0 - (-sd) / (margin + 0.01)) * clamp(0.5 - d))
            cover = clamp(d + 0.5)
            if cover <= 0:
                continue
            u = clamp((px - x0) / w)
            v = clamp((py - y0) / h)
            rgb = mix(body_tl, body_br, clamp(u * 0.45 + v * 0.55))
            if bands:
                band_rgb, k = bands(u, v, py - y0)
                if k > 0:
                    rgb = mix(rgb, band_rgb, k)
            # The chamfer: each facet lit by how it faces the light.
            if d < bevel:
                facet = nx * lx + ny * ly
                k = 0.62 * facet
                edge_fade = clamp((bevel - d) / 1.2)
                if k > 0:
                    rgb = mix(rgb, (255, 255, 255), k * edge_fade)
                else:
                    rgb = mix(rgb, (0, 0, 0), -k * edge_fade)
                # The chamfer's inner crease: a fine dark line.
                if bevel - 1.0 < d:
                    rgb = mix(rgb, (0, 0, 0), 0.18)
            # A sheen across the top left, as light on the glass.
            rgb = mix(rgb, (255, 255, 255), sheen * smooth(1.0 - (u * 0.8 + v * 1.25) / 0.95))
            # The face's reflection: its upper part a shade lighter, ending
            # in a soft line, as a pane of glass catches a window.
            if d >= bevel and reflection > 0:
                face_v = (py - y0 - bevel) / max(1.0, h - 2 * bevel)
                rgb = mix(rgb, (255, 255, 255), reflection * (1.0 - smooth((face_v - 0.30) / 0.10)))
            a = alpha
            # The thin light rim just inside the edge.
            if 0.8 < d < 2.2:
                rgb = mix(rgb, RIM, clamp(0.55 * (1.0 - abs(d - 1.5) / 0.7)))
                a = max(a, 0.98)
            # The rainbow along the edge itself.
            if d < 1.7 and rainbow_edge > 0:
                ang = math.atan2(py - (y0 + h / 2), px - (x0 + w / 2)) + 0.6
                rgb = mix(rgb, rainbow(ang), rainbow_edge * clamp((1.7 - d) / 1.2))
                a = max(a, 0.9)
            # Rings: gold when lit, else the shape's own if it has one.
            width = lit_width if over else ring_width
            colour = GOLD if over else ring
            if colour is not None and width > 0 and d < width + 0.5:
                k = clamp(width + 0.5 - d)
                rgb = mix(rgb, colour, k)
                a = max(a, k)
                if over and width - 0.6 < d < width + 0.6:
                    rgb = mix(rgb, (255, 245, 200), 0.35)
            layer.over(x, y, rgb, a * cover)
    return layer.image()


def cube(layer, cx, cy, s, tint=(70, 120, 230), alpha=0.9):
    """A small glass cube seen from a little above, centred on (cx, cy),
    `s` across: three faces lit differently, light edges, a pastel hint of
    rainbow along its top edges. Sixteen samples to a pixel (anti-aliased)."""
    hx = s / 2
    top_h = s * 0.27
    face_h = s * 0.62
    # The top face is a rhombus; the two front faces hang under it.
    T = (cx, cy - (top_h + face_h) / 2)
    L = (cx - hx, T[1] + top_h / 2)
    R = (cx + hx, T[1] + top_h / 2)
    F = (cx, T[1] + top_h)
    Lb = (L[0], L[1] + face_h)
    Rb = (R[0], R[1] + face_h)
    Fb = (F[0], F[1] + face_h)

    def inside(p, poly):
        sign = 0
        for i in range(len(poly)):
            ax, ay = poly[i]
            bx, by = poly[(i + 1) % len(poly)]
            c = (bx - ax) * (p[1] - ay) - (by - ay) * (p[0] - ax)
            if c != 0:
                if sign == 0:
                    sign = 1 if c > 0 else -1
                elif (c > 0) != (sign > 0):
                    return False
        return True

    def seg_dist(p, a, b):
        ax, ay = a
        bx, by = b
        vx, vy = bx - ax, by - ay
        t = clamp(((p[0] - ax) * vx + (p[1] - ay) * vy) / (vx * vx + vy * vy))
        return math.hypot(p[0] - ax - vx * t, p[1] - ay - vy * t)

    faces = (
        ((T, R, F, L), mix(tint, (255, 255, 255), 0.45)),
        ((L, F, Fb, Lb), mix(tint, (255, 255, 255), 0.12)),
        ((F, R, Rb, Fb), mix(tint, (0, 0, 0), 0.30)),
    )
    edges = ((T, R), (R, F), (F, L), (L, T), (F, Fb), (L, Lb), (R, Rb), (Lb, Fb), (Fb, Rb))
    for y in range(max(0, int(T[1] - 2)), min(layer.h, int(Fb[1] + 3))):
        for x in range(max(0, int(cx - hx - 2)), min(layer.w, int(cx + hx + 3))):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(4):
                for sx in range(4):
                    p = (x + (sx + 0.5) / 4, y + (sy + 0.5) / 4)
                    rgb, a = None, 0.0
                    for poly, colour in faces:
                        if inside(p, poly):
                            # Each face a little darker towards the bottom.
                            k = clamp((p[1] - T[1]) / (Fb[1] - T[1]))
                            rgb = mix(colour, mix(colour, (10, 10, 40), 0.35), k)
                            a = alpha
                            break
                    near = min(seg_dist(p, a_, b_) for a_, b_ in edges)
                    if near < 0.9:
                        top_edge = p[1] < F[1] + 0.5
                        edge_rgb = rainbow(math.atan2(p[1] - cy, p[0] - cx) * 2) if top_edge else (225, 232, 255)
                        edge_rgb = mix(edge_rgb, (255, 255, 255), 0.35)
                        rgb = edge_rgb if rgb is None else mix(rgb, edge_rgb, 0.85)
                        a = max(a, 0.95)
                    if rgb is not None:
                        acc[0] += rgb[0] * a
                        acc[1] += rgb[1] * a
                        acc[2] += rgb[2] * a
                        acc[3] += a
            if acc[3] > 0:
                layer.over(x, y, (acc[0] / acc[3], acc[1] / acc[3], acc[2] / acc[3]), acc[3] / 16)


def background(W):
    """W x 480: a deep purple-black wall, a fine grid on it (lines every 16,
    a brighter one every 64, counted from the middle so the 4:3 and 16:9
    walls match), a faint violet glow at the top and a vignette."""
    H = 480
    img = Image.new("RGB", (W, H))
    data = []
    cx = W // 2
    top = (22, 16, 48)
    bottom = (9, 7, 20)
    for y in range(H):
        base = mix(top, bottom, smooth(y / (H - 1) * 1.1))
        gy = (y - 240) % 16 == 0
        major_y = (y - 240) % 64 == 0
        for x in range(W):
            u = (x - cx) / 320.0
            # The glow: an ellipse high in the middle.
            g = math.exp(-((u / 1.1) ** 2 + ((y - 70) / 210.0) ** 2) * 1.6)
            rgb = mix(base, (64, 40, 132), 0.42 * g)
            # The grid: thin lines, a little brighter where they cross.
            gx = (x - cx) % 16 == 0
            if gx or gy:
                major = (gx and (x - cx) % 64 == 0) or (gy and major_y)
                k = 0.09 + (0.06 if major else 0.0) + (0.03 if gx and gy else 0.0)
                rgb = mix(rgb, (150, 132, 235), k)
            # The vignette: darker towards the corners.
            r2 = ((x - cx) / (W * 0.62)) ** 2 + ((y - 230) / 330.0) ** 2
            rgb = mix(rgb, (0, 0, 0), 0.62 * smooth(r2 * 0.95 - 0.12))
            data.append(tuple(int(clamp(c, 0, 255)) for c in rgb))
    img.putdata(data)
    return img


def bar(W):
    """W x 124: Home's bottom bar, the dip RiftWii paints (high at the
    sides, sinking over the middle 58% where the clock sits), as a purple
    glass strip: a light top edge, a sheen under it, darker lower down."""
    H = 124
    dip = W * 0.58
    dip_left = W / 2 - dip / 2
    tops = []
    for x in range(W):
        t = (x + 0.5 - dip_left) / dip
        d = min(smooth(t / 0.3), smooth((1 - t) / 0.3)) if 0 < t < 1 else 0.0
        tops.append(11.0 + 51.0 * d)
    layer = Layer(W, H)
    for y in range(H):
        for x in range(W):
            d = y + 0.5 - tops[x]
            if d < 0:
                # A soft shadow just above the line.
                layer.over(x, y, (0, 0, 0), 0.30 * math.exp(d / 3.0))
                if d < -0.5:
                    continue
            k = clamp((y - 11) / (H - 11))
            rgb = mix((58, 42, 138), (20, 15, 52), smooth(k * 1.05))
            # Faint fine lines across.
            if y % 3 == 0:
                rgb = mix(rgb, (0, 0, 0), 0.06)
            # The sheen right under the edge, then the light edge itself.
            rgb = mix(rgb, (255, 255, 255), 0.16 * math.exp(-max(d, 0.0) / 6.0))
            if d < 2.2:
                rgb = mix(rgb, RIM, clamp(1.0 - abs(d - 1.0) / 1.2) * 0.95)
            layer.over(x, y, rgb, clamp(d + 0.5))
    return layer.image()


def tile_empty():
    """140x100: an empty place, as a save slot: a sunken dark slot with a
    fine edge, a small blue glass cube in its middle."""
    layer = Layer(140, 100)
    x0, y0, w, h, r = 7.0, 7.0, 124.0, 84.0, 9.0
    for y in range(100):
        for x in range(140):
            d, nx, ny = rr(x + 0.5, y + 0.5, x0, y0, w, h, r)
            cover = clamp(d + 0.5)
            if cover <= 0:
                continue
            rgb = mix((14, 11, 34), (24, 19, 56), clamp((y - y0) / h))
            a = 0.72
            # Sunk in: the top and left sides in shade, the bottom lit.
            if d < 3.0:
                facet = nx * 0.55 + ny * 0.83
                if facet > 0:
                    rgb = mix(rgb, (150, 140, 230), facet * 0.35 * clamp(3.0 - d))
                else:
                    rgb = mix(rgb, (0, 0, 0), -facet * 0.5 * clamp(3.0 - d))
                a = 0.85
            if d < 1.3:
                rgb = mix(rgb, (110, 100, 190), 0.6 * clamp(1.3 - d))
            layer.over(x, y, rgb, a * cover)
    cube(layer, 70, 49, 38, tint=(66, 112, 228), alpha=0.82)
    return layer.image()


def popup_bands(header, footer, h):
    """The popup's title band and its info strip, as a `bands` function."""

    def bands(u, v, y):
        if y < header:
            return mix((44, 28, 104), (30, 26, 92), u), 0.85
        if y > h - footer:
            # The info strip: purple, a light top edge.
            if y < h - footer + 1.6:
                return RIM, 0.75
            return mix((74, 50, 156), (44, 30, 106), (y - (h - footer)) / footer), 0.9
        if y < header + 1.4:
            return RIM, 0.55
        if y < header + 2.8:
            return (0, 0, 0), 0.25
        return (0, 0, 0), 0.0

    return bands


def banner_stripes():
    """640x192: laid over the game banner (the game page's, and the launch
    screen's behind "Starting" and the game's name) while it shows the game's
    colour: the popup's info-strip look, a purple glass gradient deepening
    downwards with a light line near the band's foot. Plain across, so the
    copies the engine lays side by side join. The band is rows 8 to 128 of it
    (it starts 8 above the screen); the rest is clear."""
    img = Image.new("RGBA", (640, 192), (0, 0, 0, 0))
    foot = 122  # the light line, just above the band's gold edge
    px = img.load()
    for y in range(0, 128):
        k = smooth(y / foot)
        rgb = mix((52, 34, 128), (92, 62, 178), k)
        a = 0.22 + 0.40 * k
        if foot - 1.0 <= y < foot + 1.0:
            rgb, a = RIM, 0.70
        elif y > foot:
            a = 0.0
        for x in range(640):
            px[x, y] = (int(rgb[0]), int(rgb[1]), int(rgb[2]), int(round(a * 255)))
    return img


def main():
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parent / "themes" / "Indigo"
    out.mkdir(parents=True, exist_ok=True)
    (out / "theme.ini").write_bytes(INI.replace("\n", "\r\n").encode("utf-8"))
    (out / "LICENSE").write_bytes(LICENSE.encode("utf-8"))

    panel = dict(body_tl=(58, 40, 136), body_br=(26, 42, 118), alpha=0.95, bevel=4.0, reflection=0.03)
    tile = dict(body_tl=(70, 54, 160), body_br=(30, 40, 116), alpha=0.92, bevel=5.0)
    primary = dict(tile, body_tl=(96, 62, 178), body_br=(48, 38, 128))
    silver = dict(body_tl=(112, 108, 150), body_br=(56, 52, 96), alpha=0.97, bevel=4.0, rainbow_edge=0.35)
    amber = dict(body_tl=(176, 126, 26), body_br=(104, 66, 10), alpha=1.0, bevel=4.0, rainbow_edge=0.0,
                 sheen=0.10)
    pictures = {
        "background": lambda: background(640),
        "background_wide": lambda: background(856),
        "bar": lambda: bar(640),
        "bar_wide": lambda: bar(856),
        "tile": lambda: glass(124, 84, 7, 9, **tile),
        "tile_over": lambda: glass(124, 84, 7, 9, over=True, **dict(tile, **GOLD_GLASS)),
        "tile_empty": tile_empty,
        "cover_tile": lambda: glass(80, 112, 7, 6, **tile),
        # Only a rim of it shows round the cover, so its ring is wide.
        "cover_tile_over": lambda: glass(84, 116, 5, 8, over=True, canvas=(96, 128), lit_width=4.4,
                                         **dict(tile, bevel=6.0, **GOLD_GLASS)),
        "tile9": lambda: glass(48, 48, 8, 10, **tile),
        "tile9_over": lambda: glass(48, 48, 8, 10, over=True, **dict(tile, **GOLD_GLASS)),
        "tile9_primary": lambda: glass(48, 48, 8, 10, ring=GOLD_DEEP, ring_width=1.8, **primary),
        "tile9_primary_over": lambda: glass(48, 48, 8, 10, over=True, **dict(primary, **GOLD_GLASS_PRIMARY)),
        "pill": lambda: glass(244, 52, 4, 26, shadow=0.45, **silver),
        "pill_over": lambda: glass(244, 52, 4, 26, over=True, shadow=0.45, **amber),
        "pill_primary": lambda: glass(244, 52, 4, 26, shadow=0.45, ring=GOLD, ring_width=2.0,
                                      **dict(primary, alpha=0.97)),
        "pill_primary_over": lambda: glass(244, 52, 4, 26, over=True, shadow=0.45, **amber),
        "home_button": lambda: glass(248, 72, 8, 36, body_tl=(236, 236, 246), body_br=(160, 160, 182),
                                     alpha=1.0, bevel=5.0, rainbow_edge=0.3, sheen=0.2),
        "home_button_over": lambda: glass(248, 72, 8, 36, over=True, body_tl=(255, 226, 112),
                                          body_br=(226, 160, 28), alpha=1.0, bevel=5.0, rainbow_edge=0.0,
                                          sheen=0.2, glow=(255, 214, 90)),
        "round_button": lambda: glass(74, 74, 3, 37, canvas=(80, 80), shadow=0.4, ring=(190, 186, 222),
                                      ring_width=2.2, **tile),
        "round_button_over": lambda: glass(74, 74, 3, 37, canvas=(80, 80), over=True, shadow=0.4, **tile),
        "panel_game": lambda: glass(572, 232, 4, 12, **panel),
        "panel_settings": lambda: glass(572, 276, 4, 12, **panel),
        "banner_stripes": banner_stripes,
        "popup": lambda: glass(572, 276, 4, 12, bands=popup_bands(44, 18, 276), **dict(panel, reflection=0.0)),
    }
    only = set(sys.argv[2:])
    for name, paint in pictures.items():
        if only and name not in only:
            continue
        path = out / (name + ".png")
        paint().save(path, optimize=True)
        print(path)


if __name__ == "__main__":
    main()
