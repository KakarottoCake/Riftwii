#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws the face of Home's "CISO DETECTED" popup: a blue round face
trying not to laugh (cheeks puffed out, eyes squinting to one side,
eyebrows up, a white gloved hand over its mouth, a few sweat drops).

RiftWii's own drawing, made only from the shapes below: no emoji set,
picture from the web or Nintendo asset went into it. It is painted four
times larger than it is kept and scaled down, so the edges come out smooth.

Needs Pillow (pip install pillow). The menu embeds the result
(Makefile.wii's DATA folder wii/assets), so run it again only to change
the picture:

    python3 tools/make_ciso_face.py wii/assets/ciso_face.png
"""

import math
import sys

from PIL import Image, ImageDraw, ImageFilter

SIZE = 128  # what the popup shows (a multiple of 4: GX's RGBA8 tiles)
SCALE = 4  # painted at 512, kept at 128
CANVAS = SIZE * SCALE

# The face's blues, light (where the light falls) to dark (its rim).
FACE_LIGHT = (150, 214, 255)
FACE_MID = (52, 140, 236)
FACE_DARK = (18, 74, 178)
OUTLINE = (12, 46, 120, 255)
INK = (16, 34, 82, 255)
GLOVE = (252, 252, 255, 255)
GLOVE_SHADE = (196, 206, 226, 255)
GLOVE_LINE = (110, 124, 158, 255)
BLUSH = (255, 120, 160, 110)
DROP = (170, 225, 255, 255)
DROP_LINE = (40, 120, 210, 255)


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def shaded_ball(size, light_at):
    """A sphere's shading over a `size` square: light near `light_at`
    (a fraction of the size from the top left), darker towards the rim."""
    ball = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    pixels = ball.load()
    centre = (size - 1) / 2
    radius = size / 2
    light_x = light_at[0] * size
    light_y = light_at[1] * size
    reach = size * 0.95
    for y in range(size):
        for x in range(size):
            if math.hypot(x - centre, y - centre) > radius:
                continue
            t = math.hypot(x - light_x, y - light_y) / reach
            colour = mix(FACE_LIGHT, FACE_MID, t * 1.6) if t < 0.62 else mix(FACE_MID, FACE_DARK, (t - 0.62) / 0.5)
            pixels[x, y] = colour + (255,)
    return ball


def paste_ball(canvas, box, light_at, outline_width):
    """A shaded ellipse in `box` (left, top, right, bottom) with an outline."""
    left, top, right, bottom = box
    width = right - left
    height = bottom - top
    ball = shaded_ball(max(width, height), light_at).resize((width, height), Image.LANCZOS)
    mask = Image.new("L", (width, height), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, width - 1, height - 1), fill=255)
    canvas.paste(ball, (left, top), mask)
    ImageDraw.Draw(canvas).ellipse(box, outline=OUTLINE, width=outline_width)


def s(value):
    """A length on the 128 picture, on the larger canvas."""
    return int(round(value * SCALE))


def draw_face(canvas):
    draw = ImageDraw.Draw(canvas)

    # The puffed cheeks first, so the head overlaps their inner halves:
    # two bulges low on each side, as if holding in a laugh.
    paste_ball(canvas, (s(2), s(66), s(48), s(112)), (0.35, 0.3), s(2.2))
    paste_ball(canvas, (s(80), s(66), s(126), s(112)), (0.35, 0.3), s(2.2))

    # The head, lit from the top left.
    paste_ball(canvas, (s(14), s(14), s(114), s(116)), (0.33, 0.27), s(2.6))

    # Blush on both cheeks.
    blush = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    blush_draw = ImageDraw.Draw(blush)
    blush_draw.ellipse((s(16), s(78), s(38), s(92)), fill=BLUSH)
    blush_draw.ellipse((s(94), s(78), s(114), s(92)), fill=BLUSH)
    blush = blush.filter(ImageFilter.GaussianBlur(s(2.5)))
    canvas.alpha_composite(blush)

    # A soft shine at the top left of the head.
    shine = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    ImageDraw.Draw(shine).ellipse((s(32), s(24), s(56), s(38)), fill=(255, 255, 255, 120))
    shine = shine.filter(ImageFilter.GaussianBlur(s(3)))
    canvas.alpha_composite(shine)

    # Eyebrows raised high and arched, the inner ends a little higher.
    draw.arc((s(34), s(30), s(60), s(48)), start=200, end=330, fill=INK, width=s(3.2))
    draw.arc((s(68), s(30), s(94), s(48)), start=210, end=340, fill=INK, width=s(3.2))

    # Eyes squinted half shut and glancing to the right: the white below
    # a heavy upper lid, the pupil pushed into the right corner.
    for left in (s(36), s(70)):
        eye = (left, s(48), left + s(22), s(66))
        draw.ellipse(eye, fill=(255, 255, 255, 255), outline=INK, width=s(1.6))
        pupil_left = left + s(12)
        draw.ellipse((pupil_left, s(55), pupil_left + s(9), s(65)), fill=INK)
        # The lid: the face's colour over the eye's top half, with its line.
        lid_colour = mix(FACE_LIGHT, FACE_MID, 0.75) + (255,)
        draw.pieslice((left - s(1.5), s(46), left + s(23.5), s(68)), start=180, end=360, fill=lid_colour)
        draw.line((left - s(1), s(57), left + s(23), s(56)), fill=INK, width=s(2.6))
        # A crease under the eye: squinting.
        draw.arc((left + s(3), s(62), left + s(19), s(72)), start=20, end=160, fill=INK, width=s(1.4))

    # The gloved hand over the mouth, reaching across from the right: the
    # back of the hand with its stitching, four round fingers pointing
    # left (the middle one longest), the cuff off to the lower right.
    cuff = (s(88), s(100), s(116), s(120))
    draw.rounded_rectangle(cuff, radius=s(7), fill=GLOVE_SHADE, outline=GLOVE_LINE, width=s(1.6))
    hand = (s(58), s(74), s(100), s(112))
    draw.ellipse(hand, fill=GLOVE, outline=GLOVE_LINE, width=s(1.8))
    finger_lefts = (40, 34, 36, 42)  # index, middle, ring, little finger
    for index, finger_left in enumerate(finger_lefts):
        top = 74 + index * 8.2
        finger = (s(finger_left), s(top), s(82), s(top + 10))
        draw.rounded_rectangle(finger, radius=s(5), fill=GLOVE, outline=GLOVE_LINE, width=s(1.6))
    # Where the fingers meet the hand, under the knuckles: hidden by it.
    draw.pieslice((s(64), s(74), s(100), s(112)), start=-80, end=80, fill=GLOVE)
    draw.arc(hand, start=-80, end=80, fill=GLOVE_LINE, width=s(1.8))
    for index in range(3):
        x = s(84 + index * 4.5)
        draw.line((x, s(82), x - s(1.5), s(97)), fill=GLOVE_SHADE, width=s(1.5))

    # Sweat drops and sparkles beside the head: holding it in is hard.
    for x, y, r in ((s(106), s(24), s(6)), (s(117), s(44), s(4))):
        drop = []
        for step in range(48):
            t = 2 * math.pi * step / 48
            drop.append((x + r * math.sin(t) * math.sin(t / 2), y - r * 1.6 * math.cos(t)))
        draw.polygon(drop, fill=DROP, outline=DROP_LINE, width=s(1.2))
        draw.ellipse((x - r * 0.45, y + r * 0.1, x - r * 0.05, y + r * 0.6), fill=(255, 255, 255, 230))
    for x, y, r in ((s(14), s(22), s(6)), (s(6), s(40), s(3.5))):
        star = []
        for point in range(8):
            angle = math.pi / 4 * point
            reach = r if point % 2 == 0 else r * 0.28
            star.append((x + reach * math.sin(angle), y - reach * math.cos(angle)))
        draw.polygon(star, fill=(255, 214, 80, 255))


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make_ciso_face.py <out.png>")
    canvas = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    draw_face(canvas)
    picture = canvas.resize((SIZE, SIZE), Image.LANCZOS)
    # The DOL carries the PNG as it is: 256 colours with their alpha (a
    # palette and tRNS, which the menu's PNG reader takes) keep it near
    # 5 KB, against 20 KB in full colour, with no difference at this size.
    picture = picture.quantize(256, method=Image.Quantize.FASTOCTREE)
    picture.save(sys.argv[1], optimize=True)


if __name__ == "__main__":
    main()
