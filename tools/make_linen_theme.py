#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the Linen sample theme: themes/Linen/theme.ini and its
background.png, a woven linen cloth worked out thread by thread here (no
picture from anywhere else), with glossy buttons and a warm wood shelf.

    python tools/make_linen_theme.py
"""
import pathlib
import random
import struct
import zlib

OUT = pathlib.Path(__file__).resolve().parent.parent / "themes" / "Linen"
W, H = 640, 480

INI = """[theme]
name = Linen
author = RiftWii

[shape]
corners = 1.2
gloss = yes

[backdrop]
stripes = no

[colors]
# Text
ink = #2B2F36
ink_soft = #464B55
ink_dim = #5E6470
clock = #4F5562
accent = #3478D6
accent_ink = #1C55A8
text_on_accent = #FFFFFF
warn = #C0392B

# Tiles, buttons and panels
card = #FBFBFC
card_edge = #A9AFB9
card_edge_strong = #959CA8
shadow = #1A20304D
glow = #3478D660
glyph = #4F5562

# Option chips and switches
chip_on = #DCE9FA
chip_off = #F3F4F6
chip_off_edge = #A9AFB9
switch_off = #C9CDD4

# Background and bars
bar = #E4E7EC
backdrop = #C8CCD3
backdrop_stripe = #C8CCD3
banner_stripe = #FFFFFF1A

# Lists and badges
divider = #DADDE2
scroll_track = #D5D9DF
scroll_thumb = #8E95A1
badge = #EEF0F3
shelf = #C1915E
shelf_edge = #8C5F36

# Player pointers
pointer1 = #3478D6
pointer2 = #D64545
pointer3 = #3FA34D
pointer4 = #D9A21B
"""


def linen():
    """Threads across and down, each with its own shade and slubs (thick
    and thin stretches), crossing over and under in a plain weave."""
    rnd = random.Random(1206)
    base = (200, 204, 211)
    across = [rnd.gauss(0, 6) for _ in range(H)]
    down = [rnd.gauss(0, 6) for _ in range(W)]
    # Slubs: slow changes along each thread.
    def slubs(n, length):
        out = []
        for _ in range(n):
            v, row = 0.0, []
            for _ in range(length):
                v = v * 0.92 + rnd.gauss(0, 1.6)
                row.append(v)
            out.append(row)
        return out
    along_across = slubs(H, W)
    along_down = slubs(W, H)
    rows = []
    for y in range(H):
        row = bytearray()
        for x in range(W):
            # Which thread is on top here: the plain weave's checkerboard.
            if (x + y) & 1:
                d = across[y] + along_across[y][x] + 4
            else:
                d = down[x] + along_down[x][y] - 4
            d += rnd.gauss(0, 2.5)
            # A soft light from the top, darker towards the corners.
            dx, dy = (x - W / 2) / W, (y - H * 0.35) / H
            d -= 26 * (dx * dx + dy * dy)
            for c in base:
                row.append(max(0, min(255, int(c + d))))
        rows.append(bytes(row))
    return rows


def write_png(path, rows, w, h):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "theme.ini").write_bytes(INI.replace("\n", "\r\n").encode("utf-8"))
    write_png(OUT / "background.png", linen(), W, H)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
