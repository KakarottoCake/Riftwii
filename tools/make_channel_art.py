#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws the RiftWii channel's art into channel/art/ (committed, so the
build itself needs only the standard library). Needs Pillow and numpy.

    make_channel_art.py <out dir>

also draws the Homebrew Channel icons (hbc/icon.png for RiftWii,
channel/installer/hbc/icon.png for the installer) and the forwarder's
start screen (channel/forwarder/data).

Everything is drawn here from scratch; the letters use RiftWii's own menu
font (wii/font/rounded.ttf, SIL OFL). Most textures are white with an
alpha shape, so the banner can tint them with vertex colours.
"""
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(HERE, "..", "wii", "font", "rounded.ttf")
WORD = "RiftWii"
LETTER_PX = 104          # glyph size the letters are drawn at
GLOW_PAD = 20            # room around a letter for its glow


def save_alpha(path, alpha):
    """White with the given alpha (0..1 float array)."""
    a = (np.clip(alpha, 0, 1) * 255 + 0.5).astype(np.uint8)
    rgba = np.zeros(a.shape + (4,), np.uint8)
    rgba[..., :3] = 255
    rgba[..., 3] = a
    Image.fromarray(rgba).save(path)


def grid(w, h):
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    return (x + 0.5) / w * 2 - 1, (y + 0.5) / h * 2 - 1


def smooth_noise(w, h, scale, seed):
    """Value noise, several octaves, tileable horizontally."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w))
    amp, total = 1.0, 0.0
    for octave in range(5):
        cells = scale * (2 ** octave)
        base = rng.random((cells + 1, cells + 1))
        base[:, -1] = base[:, 0]
        img = Image.fromarray((base * 255).astype(np.uint8)).resize((w, h), Image.BICUBIC)
        out += amp * np.asarray(img, np.float64) / 255
        total += amp
        amp *= 0.5
    return out / total


def glow(size):
    x, y = grid(size, size)
    d = np.clip(np.sqrt(x * x + y * y), 0, 1)
    return (1 - d) ** 2 * (1 + 2 * d)


def sparkle(size):
    """A four-point star with a soft core."""
    x, y = grid(size, size)
    d = np.sqrt(x * x + y * y)
    core = np.exp(-(d / 0.18) ** 2)
    arms = np.exp(-(np.abs(x) / 0.05)) * np.clip(1 - np.abs(y), 0, 1) ** 2
    arms += np.exp(-(np.abs(y) / 0.05)) * np.clip(1 - np.abs(x), 0, 1) ** 2
    return np.clip(core + arms * 0.9, 0, 1)


def ring(size, seed):
    """A thin energy ring broken into uneven arcs, so its spin shows."""
    x, y = grid(size, size)
    d = np.sqrt(x * x + y * y)
    ang = np.arctan2(y, x)
    band = np.exp(-((d - 0.86) / 0.035) ** 2) + 0.35 * np.exp(-((d - 0.86) / 0.11) ** 2)
    rng = np.random.default_rng(seed)
    arcs = np.zeros_like(ang)
    for _ in range(5):
        c, w = rng.uniform(-math.pi, math.pi), rng.uniform(0.35, 0.9)
        delta = np.angle(np.exp(1j * (ang - c)))
        arcs = np.maximum(arcs, np.clip(1 - np.abs(delta) / w, 0, 1) ** 0.6)
    return np.clip(band * (0.25 + 0.75 * arcs), 0, 1)


def rift(w, h, seed):
    """A jagged vertical tear: a bright core line with a glow, thinning to
    points at both ends."""
    rng = np.random.default_rng(seed)
    ys = np.linspace(0, 1, 24)
    xs = np.concatenate([[0], rng.uniform(-0.14, 0.14, 22), [0]])
    img = Image.new("L", (w * 4, h * 4), 0)
    draw = ImageDraw.Draw(img)
    pts = [((0.5 + xs[i] * 0.5) * w * 4, ys[i] * h * 4) for i in range(24)]
    for i in range(23):
        t = ys[i]
        width = max(2, int(22 * math.sin(math.pi * t) ** 0.7))
        draw.line([pts[i], pts[i + 1]], fill=255, width=width)
    core = np.asarray(img.resize((w, h), Image.LANCZOS), np.float64) / 255
    soft = np.asarray(img.filter(ImageFilter.GaussianBlur(28)).resize((w, h), Image.LANCZOS), np.float64) / 255
    x, y = grid(w, h)
    edge = np.clip((1 - np.abs(x)) * 3, 0, 1) * np.clip((1 - np.abs(y)) * 8, 0, 1)
    return np.clip(core + soft * 2.2, 0, 1) * edge


def streak(w, h):
    x, y = grid(w, h)
    return np.exp(-(x / 0.35) ** 2) * np.clip(1 - np.abs(y), 0, 1) ** 1.5


def nebula(w, h):
    """The backdrop: deep indigo, a violet cloud and a cyan haze, darker at
    the edges. RGB."""
    x, y = grid(w, h)
    n1 = smooth_noise(w, h, 4, 11)
    n2 = smooth_noise(w, h, 3, 23)
    t = (y + 1) / 2
    base = np.stack([10 + 12 * (1 - t), 12 + 10 * (1 - t), 34 + 30 * (1 - t)], -1)
    violet = np.clip((n1 - 0.45) * 2.4, 0, 1)[..., None] * np.array([70, 26, 110])
    cyan = np.clip((n2 - 0.5) * 2.4, 0, 1)[..., None] * np.array([10, 70, 110])
    centre = np.exp(-(x * x * 1.6 + y * y * 2.2))[..., None] * np.array([18, 40, 70])
    vignette = (1 - 0.55 * np.clip(x * x * 0.7 + y * y, 0, 1))[..., None]
    rgb = np.clip((base + violet + cyan + centre) * vignette, 0, 255).astype(np.uint8)
    return Image.fromarray(rgb)


def letters(out):
    font = ImageFont.truetype(FONT, LETTER_PX)
    info = {"letters": [], "height": 0}
    _, word_top, _, word_bottom = font.getbbox(WORD)
    height = int(word_bottom - word_top) + 2 * GLOW_PAD
    height += -height % 4
    x = 0.0
    for ch in WORD:
        left, top, right, bottom = font.getbbox(ch)
        w = int(right - left) + 2 * GLOW_PAD
        w += -w % 4
        img = Image.new("L", (w, height), 0)
        ImageDraw.Draw(img).text((GLOW_PAD - left, GLOW_PAD - word_top), ch, font=font, fill=255)
        a = np.asarray(img, np.float64) / 255
        name = f"letter_{ch}{'u' if ch.isupper() else 'l'}"
        if not os.path.exists(os.path.join(out, name + ".png")):
            save_alpha(os.path.join(out, name + ".png"), a)
            blur = np.asarray(img.filter(ImageFilter.GaussianBlur(9)), np.float64) / 255
            save_alpha(os.path.join(out, name + "_glow.png"), np.clip(blur * 2.6, 0, 1))
        advance = font.getlength(ch)
        info["letters"].append({"char": ch, "tex": name, "w": w, "h": height,
                                "x": x + left - GLOW_PAD, "advance": advance})
        x += advance
    info["width"] = x
    info["height"] = height
    # the whole word, for the icon
    font_s = ImageFont.truetype(FONT, 44)
    l, t, r, b = font_s.getbbox(WORD)
    w, h = int(r - l) + 16, int(b - t) + 16
    w += -w % 4
    h += -h % 4
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((8 - l, 8 - t), WORD, font=font_s, fill=255)
    save_alpha(os.path.join(out, "word.png"), np.asarray(img, np.float64) / 255)
    blur = np.asarray(img.filter(ImageFilter.GaussianBlur(4)), np.float64) / 255
    save_alpha(os.path.join(out, "word_glow.png"), np.clip(blur * 2.4, 0, 1))
    # where "Wii" starts in the icon word, to colour it apart
    info["word_split"] = (font_s.getlength("Rift") + 8 - l) / w
    font_t = ImageFont.truetype(FONT, 26)
    tag = "M O D   L O A D E R"
    l, t, r, b = font_t.getbbox(tag)
    w, h = int(r - l) + 8, int(b - t) + 8
    w += -w % 4
    h += -h % 4
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((4 - l, 4 - t), tag, font=font_t, fill=255)
    save_alpha(os.path.join(out, "tagline.png"), np.asarray(img, np.float64) / 255)
    return info


def splash(out):
    """The forwarder's start screen (channel/forwarder/data): the backdrop
    (320x240 RGB, drawn twice its size), the word with its glow (RGBA,
    split at the seam so the rift shows between "Rift" and "Wii") and the
    rift (alpha only)."""
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "splash_bg.rgb"), "wb").write(nebula(320, 240).tobytes())
    k = 2
    font = ImageFont.truetype(FONT, 92 * k)
    gap = 22 * k
    l, t, r, b = font.getbbox(WORD)
    split = font.getlength("Rift")
    w = int(r - l + gap) + 60 * k
    h = int(b - t) + 60 * k
    x0, y0 = 30 * k - l, 30 * k - t
    mask = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(mask)
    d.text((x0, y0), "Rift", font=font, fill=255)
    d.text((x0 + split + gap, y0), "Wii", font=font, fill=255)
    a = np.asarray(mask, np.float64) / 255
    glow = np.asarray(mask.filter(ImageFilter.GaussianBlur(14 * k)), np.float64) / 255 * 1.6
    y = np.linspace(0, 1, h)[:, None, None]
    xs = np.arange(w)[None, :, None]
    seam = x0 + split + gap / 2
    fill = np.where(xs < seam, (1 - y) * np.array([255, 255, 255]) + y * np.array([200, 228, 255]),
                    (1 - y) * np.array([150, 232, 255]) + y * np.array([30, 150, 235]))
    halo = np.where(xs < seam, np.array([47, 182, 233]), np.array([150, 90, 255])) * np.ones((h, 1, 1))
    ga = np.clip(glow, 0, 1)[..., None] * 0.8
    alpha = a[..., None] + ga * (1 - a[..., None])
    rgb = (fill * a[..., None] + halo * ga * (1 - a[..., None])) / np.maximum(alpha, 1e-6)
    img = Image.fromarray(np.concatenate([np.clip(rgb, 0, 255), alpha * 255], -1).astype(np.uint8))
    w2, h2 = w // k, h // k
    w2 -= w2 % 2
    word = img.resize((w2, h2), Image.LANCZOS)
    open(os.path.join(out, "splash_word.rgba"), "wb").write(word.tobytes())
    tear = (np.clip(rift(48, 300, 42), 0, 1) * 255 + 0.5).astype(np.uint8)
    open(os.path.join(out, "splash_rift.a"), "wb").write(tear.tobytes())
    with open(os.path.join(out, "splash.h"), "w") as f:
        f.write("// Written by tools/make_channel_art.py.\n#pragma once\n")
        f.write("#define kBgWidth 320\n#define kBgHeight 240\n")
        f.write(f"#define kWordWidth {w2}\n#define kWordHeight {h2}\n#define kWordSeam {int(seam / k)}\n")
        f.write("#define kRiftWidth 48\n#define kRiftHeight 300\n")


def hbc_icon(path, tag):
    """A Homebrew Channel icon (128x48): the word over the nebula, with
    the rift through it and `tag` underneath."""
    w, h, k = 128, 48, 4
    sky = nebula(w * k, h * k).convert("RGBA")
    font = ImageFont.truetype(FONT, 25 * k)
    l, t, r, b = font.getbbox(WORD)
    x0 = (w * k - (r - l)) // 2 - l
    y0 = 3 * k - t
    split = x0 + font.getlength("Rift")
    mask = Image.new("L", sky.size, 0)
    ImageDraw.Draw(mask).text((x0, y0), WORD, font=font, fill=255)
    halo = mask.filter(ImageFilter.GaussianBlur(4 * k))
    cyan = Image.new("RGBA", sky.size, (47, 182, 233, 255))
    sky = Image.composite(cyan, sky, halo.point(lambda v: v * 3 // 4))
    rift = Image.new("L", sky.size, 0)
    ImageDraw.Draw(rift).line([(split + 2 * k, 2 * k), (split - 2 * k, 14 * k), (split + 3 * k, 24 * k),
                               (split - 1 * k, 34 * k), (split + 1 * k, 46 * k)], fill=255, width=2 * k)
    sky = Image.composite(Image.new("RGBA", sky.size, (205, 238, 255, 255)), sky,
                          Image.eval(rift.filter(ImageFilter.GaussianBlur(3 * k)), lambda v: min(255, v * 3)))
    y = np.linspace(0, 1, h * k)[:, None, None]
    xs = np.arange(w * k)[None, :, None]
    fill = np.where(xs < split, (1 - y) * np.array([255, 255, 255]) + y * np.array([200, 228, 255]),
                    (1 - y) * np.array([150, 232, 255]) + y * np.array([30, 150, 235]))
    letters = Image.fromarray(np.concatenate([fill, np.full((h * k, w * k, 1), 255.0)], -1).astype(np.uint8))
    outline = Image.new("L", sky.size, 0)
    ImageDraw.Draw(outline).text((x0, y0), WORD, font=font, fill=255, stroke_width=k * 3 // 2, stroke_fill=255)
    sky = Image.composite(Image.new("RGBA", sky.size, (8, 14, 40, 255)), sky, outline)
    sky = Image.composite(letters, sky, mask)
    small = ImageFont.truetype(FONT, 8 * k)
    l, t, r, b = small.getbbox(tag)
    ImageDraw.Draw(sky).text(((w * k - (r - l)) // 2 - l, 36 * k - t), tag, font=small, fill=(150, 196, 240, 255))
    sky = sky.resize((w, h), Image.LANCZOS)
    corner = Image.new("L", (w * k, h * k), 0)
    ImageDraw.Draw(corner).rounded_rectangle([0, 0, w * k - 1, h * k - 1], radius=8 * k, fill=255)
    sky.putalpha(corner.resize((w, h), Image.LANCZOS))
    sky.save(path)


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    out = argv[1]
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".png"):
            os.remove(os.path.join(out, f))
    nebula(256, 144).save(os.path.join(out, "nebula.png"))
    save_alpha(os.path.join(out, "cloud.png"), np.clip((smooth_noise(128, 128, 3, 5) - 0.4) * 2.2, 0, 1) * glow(128))
    save_alpha(os.path.join(out, "glow.png"), glow(64))
    save_alpha(os.path.join(out, "sparkle.png"), sparkle(32))
    save_alpha(os.path.join(out, "ring_a.png"), ring(128, 3))
    save_alpha(os.path.join(out, "ring_b.png"), ring(128, 8))
    save_alpha(os.path.join(out, "rift.png"), rift(64, 128, 42))
    save_alpha(os.path.join(out, "streak.png"), streak(32, 128))
    info = letters(out)
    hbc_icon(os.path.join(HERE, "..", "channel", "installer", "hbc", "icon.png"), "C H A N N E L")
    hbc_icon(os.path.join(HERE, "..", "hbc", "icon.png"), "M O D   L O A D E R")
    splash(os.path.join(HERE, "..", "channel", "forwarder", "data"))
    json.dump(info, open(os.path.join(out, "letters.json"), "w"), indent=1)
    print("art written to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
