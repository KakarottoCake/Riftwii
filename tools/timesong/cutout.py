#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Cuts the soyjak for themes/TheTime/soyjak.png out of the meme image:
#   cutout.py <image> <out.png> <preview.png>
import sys
from PIL import Image, ImageDraw, ImageFilter

src = Image.open(sys.argv[1]).convert('RGB')
X0, Y0, X1, Y1 = 180, 30, 372, 334
im = src.crop((X0, Y0, X1, Y1))
w, h = im.size
poly = [(22, 12), (27, 6), (36, 4), (46, 5), (60, 9), (71, 32), (69, 55), (80, 64), (116, 76), (128, 96), (133, 120),
        (138, 150), (151, 180), (166, 225), (183, 267), (191, 304), (49, 304), (49, 203), (30, 201), (12, 188),
        (8, 107), (25, 88), (40, 75), (29, 56), (25, 40)]
shape = Image.new('L', (w, h))
ImageDraw.Draw(shape).polygon(poly, fill=255)
sp = shape.load()
px = im.load()
mask = Image.new('L', (w, h))
mp = mask.load()
for y in range(h):
    for x in range(w):
        if not sp[x, y]:
            continue
        r, g, b = px[x, y]
        lum = 0.3 * r + 0.59 * g + 0.11 * b
        lavender = b > r + 16 and lum > 130
        navy = b > r + 22 and lum < 130 and b > 45
        sage = g > r + 12 and g > b + 4
        mp[x, y] = 0 if (lavender or navy or sage) else 255
# Holes: transparent pixels not reachable from outside the shape are filled.
from collections import deque
seen = [[False] * w for _ in range(h)]
q = deque([(0, 0)])
for y in range(h):
    q.append((0, y)); q.append((w - 1, y))
for x in range(w):
    q.append((x, 0))
while q:
    x, y = q.popleft()
    if x < 0 or y < 0 or x >= w or y >= h or seen[y][x] or mp[x, y]:
        continue
    seen[y][x] = True
    q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
for y in range(h):
    for x in range(w):
        if not seen[y][x]:
            mp[x, y] = 255
# Drop specks: an opening, then soften the edge.
mask = mask.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.MaxFilter(3))
mask = mask.filter(ImageFilter.GaussianBlur(0.6))
out = im.convert('RGBA')
out.putalpha(mask)
out.save(sys.argv[2])
prev = Image.new('RGBA', (w, h), (255, 0, 255, 255))
prev.alpha_composite(out)
prev.resize((w * 2, h * 2), Image.NEAREST).save(sys.argv[3])
