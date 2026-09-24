# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws hbc/icon.png, the Homebrew Channel banner (128x48).

Original art in the menu's look (wii/skin.cpp): a white rounded card, the
name in the menu font (wii/font/rounded.ttf, OFL) and the accent curve
of the home screen's bar. Needs Pillow:  python tools/make_hbc_icon.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INK = (46, 46, 54, 255)
ACCENT = (47, 182, 233, 255)
EDGE = (207, 207, 214, 255)
SCALE = 4  # drawn large, then scaled down for smooth edges
W, H = 128, 48


def main():
    s = SCALE
    img = Image.new("RGBA", (W * s, H * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((1 * s, 1 * s, (W - 1) * s - 1, (H - 1) * s - 1), radius=12 * s,
                        fill=(255, 255, 255, 255), outline=EDGE, width=int(1.5 * s))

    # The bar's bump, low across the bottom of the card.
    points = []
    for x in range(4 * s, (W - 4) * s):
        t = (x / s - 34) / 60
        bump = (1 - math.cos(t * 2 * math.pi)) / 2 if 0 < t < 1 else 0
        points.append((x, (41 - 6 * bump) * s))
    d.line(points, fill=ACCENT, width=int(2 * s))

    font = ImageFont.truetype(os.path.join(ROOT, "wii", "font", "rounded.ttf"), 23 * s)
    rift, wii = "Rift", "Wii"
    w_rift = d.textlength(rift, font=font)
    w_all = w_rift + d.textlength(wii, font=font)
    x = (W * s - w_all) / 2
    y = 5 * s
    d.text((x, y), rift, font=font, fill=INK)
    d.text((x + w_rift, y), wii, font=font, fill=ACCENT)

    out = os.path.join(ROOT, "hbc", "icon.png")
    img.resize((W, H), Image.LANCZOS).save(out, optimize=True)
    print(out)


if __name__ == "__main__":
    main()
