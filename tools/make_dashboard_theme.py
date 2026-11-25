# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draw Dashboard's silver wall, square tiles and controls from scratch.

    python tools/make_dashboard_theme.py [out_dir [picture ...]]

Needs Pillow and NumPy. The default output is themes/Dashboard. Picture
names (without .png) limit a run to those pictures. The generated folder can
go straight into the community themes repository or onto an SD card.
"""
import argparse
import pathlib
import subprocess
import sys

import numpy as np
from PIL import Image, ImageColor, ImageDraw

HERE = pathlib.Path(__file__).resolve().parent

COLORS = {
    "ink": "#202823",
    "ink_soft": "#28332C",
    "ink_dim": "#343F37",
    "clock": "#35443A",
    "accent": "#52B043",
    "accent_ink": "#183921",
    "text_on_accent": "#202823",
    "warn": "#812027",
    "card": "#F4F5F2",
    "card_edge": "#C2C9C2",
    "card_edge_strong": "#8C998E",
    "shadow": "#20282330",
    "glow": "#52B04330",
    "glyph": "#27342B",
    "chip_on": "#D5EDCF",
    "chip_off": "#E9ECE8",
    "chip_off_edge": "#BCC5BD",
    "switch_off": "#6C786E",
    "bar": "#DCE1DC",
    "backdrop": "#CCD1CC",
    "backdrop_stripe": "#CCD1CC",
    "banner_stripe": "#D2F4CA17",
    "banner_tint": "#26332A75",
    "divider": "#C6CEC6",
    "scroll_track": "#DEE4DE",
    "scroll_thumb": "#637568",
    "badge": "#E0E8DF",
    "shelf": "#707B73",
    "shelf_edge": "#303A33",
    "pointer1": "#195F99",
    "pointer2": "#A22C38",
    "pointer3": "#216D2C",
    "pointer4": "#875306",
}

INI = """[theme]
name = Dashboard
author = RiftWii contributors

[shape]
corners = 0
gloss = no
bar = dip
shelf_grain = no
home_menu = wii

[backdrop]
stripes = no

[colors]
""" + "".join(f"{key} = {color}\n" for key, color in COLORS.items())

LICENSE = """License: CC0-1.0
Author: RiftWii contributors

Dashboard, a theme for RiftWii: silver walls, square green and white tiles,
and flat charcoal frames.

To the extent possible under law, the author has waived all copyright and
related or neighboring rights to this theme (theme.ini and every PNG
picture in its folder), under Creative Commons CC0 1.0 Universal:
https://creativecommons.org/publicdomain/zero/1.0/

The pictures were drawn by RiftWii's tools/make_dashboard_theme.py
(https://github.com/KakarottoCake/Riftwii) from arithmetic alone: no
source images, textures or fonts. The theme keeps RiftWii's own sounds and
music.

Third-party parts: none.
"""

WHITE = "#FFFFFF"
CHARCOAL = "#2B2B2B"
PANEL_RAIL = "#3C3C3C"
DEEP_GREEN = "#107018"
FOCUS_GREEN = "#167922"
PRIMARY = "#59B84C"
PRIMARY_LIT = "#67C257"
LIGHT_LIT = "#FAFEF9"


def pixel_grid(width, height):
    return np.arange(width)[None, :] + 0.5, np.arange(height)[:, None] + 0.5


def smoothstep(fraction):
    fraction = np.clip(fraction, 0, 1)
    return fraction * fraction * (3 - 2 * fraction)


def dither_wall(rgb):
    """Dither at the shrinker's six-bit levels so the light stays smooth."""
    threshold = np.zeros((1, 1), dtype=int)
    for _ in range(3):
        threshold = np.block([[4 * threshold, 4 * threshold + 2],
                              [4 * threshold + 3, 4 * threshold + 1]])
    height, width = rgb.shape[:2]
    offsets = (threshold[np.arange(height)[:, None] % 8, np.arange(width) % 8] + 0.5) / 64
    levels = np.floor(np.clip(rgb, 0, 255) * (63 / 255) + offsets[:, :, None])
    return Image.fromarray(np.rint(levels * (255 / 63)).astype(np.uint8))


def ribbon(across, down, aspect, heights, reach, radius):
    """Return softly tapered halo and core masks along a cubic sweep."""
    left, right = reach
    phase = np.clip((across - left) / (right - left), 0, 1)
    remaining = 1 - phase
    start, first_control, second_control, end = heights
    center = (
        start * remaining ** 3 + 3 * first_control * remaining ** 2 * phase
        + 3 * second_control * remaining * phase ** 2 + end * phase ** 3
    )
    tangent = 3 * (
        (first_control - start) * remaining ** 2
        + 2 * (second_control - first_control) * remaining * phase
        + (end - second_control) * phase ** 2
    )
    slope = tangent / ((right - left) * aspect)
    distance = np.abs(down - center) / np.hypot(1, slope)

    taper = np.sin(np.pi * phase)
    half_width = radius * (0.19 + 0.81 * taper ** 0.65)
    strength = smoothstep(np.minimum(phase, remaining) / 0.24)
    halo = strength * (1 - smoothstep(distance / half_width))
    core = strength * (1 - smoothstep(distance / (half_width * 0.40)))
    return halo, core


def add_light(rgb, mask, tint):
    """Tint the glow without dimming any part of the wall behind the ink."""
    return rgb + mask[:, :, None] * np.maximum(np.array(tint) - rgb, 0)


def background(width):
    horizontal, vertical = pixel_grid(width, 480)
    across = horizontal / width
    down = vertical / 480
    center_light = np.sin(np.pi * across) ** 2
    wall_light = center_light * (0.30 + 0.53 * np.sin(np.pi * down) ** 2)
    wall_light += 0.17 * smoothstep(down / 0.65)
    rgb = np.array((181, 184, 184)) + wall_light[:, :, None] * (39, 38, 38)

    floor_pool = center_light * np.exp(-((down - 0.82) / 0.103) ** 2)
    floor_sheen = center_light ** 2 * np.exp(-((down - 0.792) / 0.028) ** 2)
    rgb = add_light(rgb, 0.66 * floor_pool + 0.15 * floor_sheen, (243, 249, 239))

    # Both main paths start low and cross once. All three die away before the mirror joins.
    aspect = width / 480
    lower_halo, lower_core = ribbon(
        across, down, aspect, (0.71, 0.80, 0.68, 0.47), (0.02, 0.98), radius=0.075
    )
    steep_halo, steep_core = ribbon(
        across, down, aspect, (0.80, 0.89, 0.62, 0.22), (0.06, 0.99), radius=0.082
    )
    high_halo, high_core = ribbon(
        across, down, aspect, (0.63, 0.48, 0.32, 0.24), (0.04, 0.90), radius=0.044
    )

    rgb = add_light(rgb, high_halo * 0.49, (217, 236, 205))
    rgb = add_light(rgb, high_core * 0.54, (248, 253, 244))
    rgb = add_light(rgb, np.maximum(lower_halo, steep_halo) * 0.95, (225, 244, 212))
    rgb = add_light(rgb, np.maximum(lower_core, steep_core) * 0.96, (255, 255, 251))
    return dither_wall(rgb)


def bar(width):
    """Match RiftWii's dip: 11-pixel shoulders, 62-pixel center."""
    horizontal, vertical = pixel_grid(width, 124)
    dip_width = width * 0.58
    dip_left = (width - dip_width) / 2
    phase = (horizontal - dip_left) / dip_width
    depth = np.minimum(smoothstep(phase / 0.3), smoothstep((1 - phase) / 0.3))
    distance = vertical - (11 + 51 * depth)
    coverage = np.clip(distance + 0.5, 0, 1)
    edge = np.clip(distance + 0.5, 0, 1) - np.clip(distance - 1.5, 0, 1)
    silver = np.array(ImageColor.getrgb(COLORS["bar"]))
    green = np.array(ImageColor.getrgb(COLORS["accent"]))
    rgb = silver + edge[:, :, None] * (green - silver)
    rgba = np.dstack((rgb, coverage * 255))
    return Image.fromarray(np.rint(rgba).astype(np.uint8))


def rectangle(draw, box, **style):
    """Pillow's right and bottom edges are inclusive; asset boxes aren't."""
    left, top, right, bottom = box
    draw.rectangle((left, top, right - 1, bottom - 1), **style)


def plate(size, box, fill, edge=None, border=1):
    picture = Image.new("RGBA", size)
    rectangle(ImageDraw.Draw(picture), box, fill=fill, outline=edge, width=border)
    return picture


def tile(size, box, *, over=False, primary=False):
    if not primary:
        fill = LIGHT_LIT if over else COLORS["card"]
        edge = FOCUS_GREEN if over else COLORS["card_edge"]
        return plate(size, box, fill, edge, 3 if over else 1)
    if not over:
        return plate(size, box, PRIMARY, DEEP_GREEN)

    # White inside a dark keyline still reads against either silver or green.
    picture = plate(size, box, PRIMARY_LIT, DEEP_GREEN)
    left, top, right, bottom = box
    inset = (left + 1, top + 1, right - 1, bottom - 1)
    rectangle(ImageDraw.Draw(picture), inset, fill=PRIMARY_LIT, outline=WHITE, width=3)
    return picture


def focused_button(size, box, primary):
    """Keep the focus frame readable when HOME shrinks Close to 62%."""
    fill = PRIMARY_LIT if primary else "#E8F4E3"
    frame_width = 5
    picture = plate(size, box, fill, DEEP_GREEN, frame_width)
    left, top, right, bottom = box
    inset = (left + frame_width, top + frame_width, right - frame_width, bottom - frame_width)
    inner_edge = WHITE if primary else COLORS["accent"]
    rectangle(ImageDraw.Draw(picture), inset, fill=fill, outline=inner_edge, width=2)
    return picture


def empty_tile():
    picture = plate((140, 100), (7, 7, 131, 91), "#DCE2DCCF", "#A7B2A8")
    draw = ImageDraw.Draw(picture)
    for left in (59, 72):
        for top in (38, 51):
            rectangle(draw, (left, top, left + 9, top + 9), fill="#A8B4A9")
    return picture


def panel(height, *, popup=False):
    # The engine draws the same dark ink on the wall and on a panel. Keep
    # the charcoal as a case around the light surface underneath the text.
    picture = plate((580, height), (4, 4, 576, height - 4), CHARCOAL)
    draw = ImageDraw.Draw(picture)
    rectangle(draw, (8, 8, 572, height - 9), fill=PANEL_RAIL)
    rectangle(draw, (11, 11, 569, height - 15), fill=COLORS["card"])
    rectangle(draw, (11, 10, 569, 12), fill=COLORS["accent"])
    if popup:
        rectangle(draw, (11, 12, 569, 48), fill=COLORS["bar"])
        rectangle(draw, (11, 47, 569, 48), fill=COLORS["divider"])
        rectangle(draw, (11, height - 22, 569, height - 15), fill=COLORS["divider"])
    return picture


def banner_stripes():
    horizontal, vertical = pixel_grid(640, 192)
    phase = (horizontal + vertical - 0.5) % 80
    diagonal = np.clip(2 - np.minimum(phase, 80 - phase), 0, 1)
    foot = np.clip((124 - vertical) / 4, 0, 1)
    red, green, blue, alpha = ImageColor.getcolor(COLORS["banner_stripe"], "RGBA")
    pixels = np.empty((192, 640, 4), dtype=np.uint8)
    pixels[:, :, :3] = (red, green, blue)
    pixels[:, :, 3] = np.rint(diagonal * foot * alpha)
    return Image.fromarray(pixels)


def shelf():
    picture = Image.new("RGB", (256, 64), COLORS["shelf"])
    draw = ImageDraw.Draw(picture)
    rectangle(draw, (0, 0, 256, 3), fill="#59645B")
    rectangle(draw, (0, 44, 256, 48), fill="#859187")
    rectangle(draw, (0, 48, 256, 64), fill=COLORS["shelf_edge"])
    rectangle(draw, (0, 48, 256, 50), fill=COLORS["accent"])
    return picture


def blank_cover():
    picture = Image.new("RGB", (160, 224), COLORS["card"])
    draw = ImageDraw.Draw(picture)
    rectangle(draw, (0, 0, 160, 37), fill=COLORS["accent"])
    rectangle(draw, (0, 188, 160, 224), fill="#DEE5DE")
    rectangle(draw, (0, 188, 160, 190), fill=COLORS["card_edge"])
    return picture


class VectorArt:
    """Draw fractional edges at four times size before resolving coverage."""

    SCALE = 4

    def __init__(self, size):
        self.size = size
        self.canvas = Image.new("RGBA", tuple(length * self.SCALE for length in size))
        self.draw = ImageDraw.Draw(self.canvas)

    def box(self, box, fill):
        rectangle(self.draw, tuple(round(value * self.SCALE) for value in box), fill=fill)

    def points(self, points):
        return [(round(horizontal * self.SCALE), round(vertical * self.SCALE))
                for horizontal, vertical in points]

    def polygon(self, points, fill):
        self.draw.polygon(self.points(points), fill=fill)

    def line(self, points, fill, width):
        self.draw.line(self.points(points), fill=fill, width=round(width * self.SCALE), joint="curve")

    def circle(self, center, radius, fill, *, outline=None, width=1):
        horizontal, vertical = center
        box = ((horizontal - radius, vertical - radius), (horizontal + radius, vertical + radius))
        self.draw.ellipse(self.points(box), fill=fill, outline=outline, width=round(width * self.SCALE))

    def finish(self):
        return self.canvas.resize(self.size, Image.Resampling.BOX)


def arrow(direction, *, over=False, page=False):
    size = 48 if page else 44
    margin = 7 if page else 4
    center = size / 2
    normal = plate((size, size), (margin, margin, size - margin, size - margin),
                   FOCUS_GREEN if over else CHARCOAL, WHITE if over else None, 3)
    art = VectorArt((size, size))
    art.polygon(((center - 4.5, center - 7), (center + 5.5, center),
                 (center - 4.5, center + 7)), WHITE)
    rotations = {"right": None, "left": Image.Transpose.FLIP_LEFT_RIGHT,
                 "up": Image.Transpose.ROTATE_90, "down": Image.Transpose.ROTATE_270}
    rotation = rotations[direction]
    glyph = art.finish()
    if rotation is not None:
        glyph = glyph.transpose(rotation)
    normal.alpha_composite(glyph)
    return normal


def switch(on):
    track = DEEP_GREEN if on else COLORS["switch_off"]
    picture = plate((68, 40), (3, 4, 63, 34), track, CHARCOAL)
    draw = ImageDraw.Draw(picture)
    knob_left = 36 if on else 6
    rectangle(draw, (knob_left, 7, knob_left + 24, 31), fill=WHITE)
    return picture


def pointer(color):
    # A flat cursor without an embedded logo; its tip is the hot spot.
    art = VectorArt((96, 96))
    outline = ((48, 48), (51, 78), (57, 71), (64, 83), (72, 78), (64, 66), (74, 63))
    fill = ((51, 53), (53, 72), (57, 67), (65, 79), (68, 77), (60, 64), (68, 62))
    shadow = [(horizontal + 1, vertical + 2) for horizontal, vertical in outline]
    art.polygon(shadow, "#20282342")
    art.polygon(outline, color)
    art.polygon(fill, WHITE)
    return art.finish()


def icon(name):
    # These icons are shared by green Home controls and light menu cards.
    # A deep-green square keeps the white glyph readable on both.
    art = VectorArt((28, 28))
    art.box((1, 1, 27, 27), DEEP_GREEN)
    if name == "drives":
        for top in (5, 15):
            art.box((5, top, 23, top + 8), WHITE)
            art.box((9, top + 2, 11, top + 5), DEEP_GREEN)
            art.box((15, top + 3, 20, top + 5), DEEP_GREEN)
    elif name == "gear":
        for turn in range(8):
            angle = turn * np.pi / 4
            reach = 9.8
            tip = (14 + reach * np.cos(angle), 14 + reach * np.sin(angle))
            art.line(((14, 14), tip), WHITE, 3.6)
        art.circle((14, 14), 7.75, WHITE)
        art.circle((14, 14), 3.4, DEEP_GREEN)
    elif name == "search":
        art.circle((12, 12), 5, None, outline=WHITE, width=2)
        art.line(((16, 16), (22, 22)), WHITE, 2.4)
    elif name == "play":
        art.polygon(((10, 6.5), (22, 14), (10, 21.5)), WHITE)
    elif name == "store":
        art.circle((14, 10), 4, None, outline=WHITE, width=1.75)
        art.box((6, 10, 22, 22), WHITE)
        art.box((11, 12, 17, 14), DEEP_GREEN)
    elif name == "picture":
        art.box((4, 6, 24, 20), WHITE)
        art.box((6, 8, 22, 18), DEEP_GREEN)
        art.box((9, 21, 19, 23), WHITE)
    elif name == "controllers":
        art.polygon(((6, 8), (22, 8), (25, 21), (21, 23), (17, 18),
                     (11, 18), (7, 23), (3, 21)), WHITE)
        art.box((7, 12, 13, 14), DEEP_GREEN)
        art.box((9, 10, 11, 16), DEEP_GREEN)
        art.circle((19, 13), 2, DEEP_GREEN)
    elif name == "home":
        art.polygon(((4, 12), (14, 4), (24, 12), (22, 12), (22, 23),
                     (16, 23), (16, 16), (12, 16), (12, 23), (6, 23), (6, 12)), WHITE)
    elif name == "sound":
        art.polygon(((4, 10), (9, 10), (14, 6), (14, 22), (9, 18), (4, 18)), WHITE)
        art.line(((17, 10), (19, 12), (19, 16), (17, 18)), WHITE, 1.5)
        art.line(((20, 7), (23, 10), (23, 18), (20, 21)), WHITE, 1.5)
    elif name == "online":
        art.circle((14, 14), 9, None, outline=WHITE, width=1.6)
        art.line(((5, 14), (23, 14)), WHITE, 1.6)
        art.line(((14, 5), (10, 10), (10, 18), (14, 23), (18, 18), (18, 10), (14, 5)), WHITE, 1.6)
    elif name == "help":
        art.circle((14, 14), 9, None, outline=WHITE, width=1.6)
        art.line(((11, 11), (11, 9), (16, 9), (17, 10), (17, 12), (14, 14), (14, 16)), WHITE, 1.5)
        art.box((13, 18, 15, 20), WHITE)
    elif name == "power":
        art.circle((14, 14.5), 8, None, outline=WHITE, width=2)
        art.box((10, 4, 18, 10), DEEP_GREEN)
        art.box((13, 4, 15, 14), WHITE)
    return art.finish()


def painters():
    pictures = {
        "background": lambda: background(640),
        "background_wide": lambda: background(856),
        "bar": lambda: bar(640),
        "bar_wide": lambda: bar(856),
        "tile_empty": empty_tile,
        "chip_off": lambda: plate((212, 36), (2, 2, 210, 32), COLORS["chip_off"], COLORS["chip_off_edge"]),
        "chip_on": lambda: plate((212, 36), (2, 2, 210, 32), COLORS["chip_on"], FOCUS_GREEN, 2),
        "row_focus": lambda: tile((548, 44), (1, 1, 547, 43), over=True),
        "switch_off": lambda: switch(False),
        "switch_on": lambda: switch(True),
        "panel_game": lambda: panel(240),
        "panel_settings": lambda: panel(284),
        "popup": lambda: panel(284, popup=True),
        "banner_stripes": banner_stripes,
        "shelf": shelf,
        "blank_cover": blank_cover,
    }

    tile_specs = (
        ("tile", (140, 100), (7, 7, 131, 91), False),
        ("cover_tile", (96, 128), (6, 6, 90, 122), False),
        ("tile9", (64, 64), (8, 8, 56, 56), False),
        ("tile9_primary", (64, 64), (8, 8, 56, 56), True),
        ("pill", (252, 60), (4, 4, 248, 56), False),
        ("pill_primary", (252, 60), (4, 4, 248, 56), True),
        ("home_button", (264, 88), (8, 8, 256, 80), False),
        ("round_button", (80, 80), (4, 4, 76, 76), True),
    )
    for name, size, box, primary in tile_specs:
        pictures[name] = lambda size=size, box=box, primary=primary: tile(size, box, primary=primary)
        if name in ("pill", "pill_primary", "home_button"):
            pictures[name + "_over"] = lambda size=size, box=box, primary=primary: focused_button(
                size, box, primary)
        else:
            lit_primary = primary or name == "tile"
            pictures[name + "_over"] = lambda size=size, box=box, primary=lit_primary: tile(
                size, box, over=True, primary=primary)

    arrow_specs = {"step_back": "left", "step_forward": "right", "arrow_left": "left",
                   "arrow_right": "right", "scroll_up": "up", "scroll_down": "down"}
    for name, direction in arrow_specs.items():
        page = name.startswith("arrow_")
        pictures[name] = lambda direction=direction, page=page: arrow(direction, page=page)
        pictures[name + "_over"] = lambda direction=direction, page=page: arrow(direction, over=True, page=page)

    for player in range(1, 5):
        name = f"pointer{player}"
        pictures[name] = lambda color=COLORS[name]: pointer(color)
    icons = ("drives", "gear", "search", "play", "store", "picture", "controllers",
             "home", "sound", "online", "help", "power")
    for name in icons:
        pictures[f"icon_{name}"] = lambda name=name: icon(name)
    return pictures


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("out_dir", nargs="?", type=pathlib.Path, default=HERE.parent / "themes" / "Dashboard")
    parser.add_argument("pictures", nargs="*", metavar="picture")
    args = parser.parse_args()

    pictures = painters()
    unknown = sorted(set(args.pictures) - pictures.keys())
    if unknown:
        parser.error("unknown picture(s): " + ", ".join(unknown))

    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "theme.ini").write_bytes(INI.replace("\n", "\r\n").encode("utf-8"))
    (args.out_dir / "LICENSE").write_text(LICENSE, encoding="utf-8", newline="\n")
    written = []
    for name, paint in pictures.items():
        if args.pictures and name not in args.pictures:
            continue
        path = args.out_dir / f"{name}.png"
        paint().save(path, optimize=True)
        print(path, flush=True)
        written.append(str(path))

    subprocess.run([sys.executable, str(HERE / "shrink_theme_pictures.py"), "--", *written], check=True)


if __name__ == "__main__":
    main()
