#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared drawing helpers for RiftWii's theme generators (numpy + Pillow).

Every shape is worked out from arithmetic: signed distances, anti-aliased
edges, gradients with a little dither so 6-bit colour shows no bands. The
generators (tools/make_wave_theme.py, tools/make_plaza_theme.py) build their
pictures from these.

A canvas is a float32 array of shape (h, w, 4): straight RGB in 0..255 and
alpha in 0..1. `over` composites one RGBA layer on another.
"""
import math
import subprocess
import wave as wavefile

import numpy as np
from PIL import Image


def canvas(w, h):
    return np.zeros((h, w, 4), np.float32)


def grid(w, h):
    """Pixel centres: x and y arrays of shape (h, w)."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float32)
    return x + 0.5, y + 0.5


def smooth(v):
    v = np.clip(v, 0.0, 1.0)
    return v * v * (3 - 2 * v)


def mix(a, b, k):
    """a + (b - a) k for colours (3,) or arrays (..., 3), k scalar or (...)."""
    a = np.asarray(a, np.float32)
    b = np.asarray(b, np.float32)
    k = np.asarray(k, np.float32)
    if k.ndim:
        k = k[..., None]
    return a + (b - a) * k


def over(dst, rgb, a):
    """Composites colour `rgb` ((3,) or (h, w, 3)) with alpha `a` ((h, w))
    over `dst` in place."""
    rgb = np.broadcast_to(np.asarray(rgb, np.float32), dst[..., :3].shape)
    a = np.clip(np.asarray(a, np.float32), 0.0, 1.0)
    da = dst[..., 3]
    oa = a + da * (1 - a)
    safe = np.where(oa > 0, oa, 1.0)
    for i in range(3):
        dst[..., i] = np.where(oa > 0, (rgb[..., i] * a + dst[..., i] * da * (1 - a)) / safe, 0.0)
    dst[..., 3] = oa
    return dst


def add_light(dst, rgb, a):
    """Adds light (screen blend) on an opaque or translucent canvas: the
    colour brightens what is there without hiding it."""
    rgb = np.asarray(rgb, np.float32)
    a = np.clip(np.asarray(a, np.float32), 0.0, 1.0)[..., None]
    base = dst[..., :3] / 255.0
    lit = 1 - (1 - base) * (1 - (rgb / 255.0) * a)
    dst[..., :3] = lit * 255.0
    return dst


def rrect(x, y, x0, y0, w, h, r):
    """Signed distance to a rounded rectangle, positive inside, and the
    outward normal (nx, ny) of its nearest edge."""
    r = min(r, w / 2, h / 2)
    cx = np.clip(x, x0 + r, x0 + w - r)
    cy = np.clip(y, y0 + r, y0 + h - r)
    dx, dy = x - cx, y - cy
    dist = np.hypot(dx, dy)
    corner = dist > 0
    d = np.where(corner, r - dist, 0.0)
    nx = np.where(corner, dx / np.where(corner, dist, 1), 0.0)
    ny = np.where(corner, dy / np.where(corner, dist, 1), 0.0)
    # In the straight core: the nearest edge.
    e = np.stack([x - x0, x0 + w - x, y - y0, y0 + h - y])
    k = np.argmin(e, axis=0)
    emin = np.min(e, axis=0)
    core = ~corner
    d = np.where(core, emin, d)
    nx = np.where(core, np.choose(k, [-1.0, 1.0, 0.0, 0.0]), nx)
    ny = np.where(core, np.choose(k, [0.0, 0.0, -1.0, 1.0]), ny)
    return d.astype(np.float32), nx.astype(np.float32), ny.astype(np.float32)


def circle(x, y, cx, cy, r):
    return (r - np.hypot(x - cx, y - cy)).astype(np.float32)


def coverage(d):
    """Anti-aliased coverage of a shape from its signed distance."""
    return np.clip(d + 0.5, 0.0, 1.0)


def dither(h, w, seed=7, amount=0.9):
    """Blue-ish noise in -amount..amount (0..255 units) so gradients quantised
    to 6 bits show no bands."""
    rng = np.random.default_rng(seed)
    n = rng.random((h, w), dtype=np.float32) + rng.random((h, w), dtype=np.float32) - 1.0
    return n * amount


def to_image(c, dith=True, seed=7, opaque=False):
    rgb = c[..., :3].copy()
    if dith:
        n = dither(c.shape[0], c.shape[1], seed, 2.2)
        rgb += n[..., None]
    a = c[..., 3] if not opaque else np.ones(c.shape[:2], np.float32)
    out = np.dstack([np.clip(rgb, 0, 255), np.clip(a * 255.0, 0, 255)]).round().astype(np.uint8)
    img = Image.fromarray(out, "RGBA")
    return img.convert("RGB") if opaque else img


def supersample(paint, w, h, s=3):
    """Paints at s times the size and averages down (premultiplied), for
    shapes whose edges are not simple distances (curves, many lines)."""
    big = paint(w * s, h * s, s)
    pm = big[..., :3] * big[..., 3:4]
    pm = pm.reshape(h, s, w, s, 3).mean(axis=(1, 3))
    a = big[..., 3].reshape(h, s, w, s).mean(axis=(1, 3))
    rgb = np.where(a[..., None] > 0, pm / np.maximum(a[..., None], 1e-6), 0.0)
    return np.dstack([rgb, a]).astype(np.float32)


def blur(c, radius):
    """A cheap separable box blur, three passes (close to Gaussian), on a
    premultiplied canvas."""
    if radius <= 0:
        return c
    pm = np.dstack([c[..., :3] * c[..., 3:4], c[..., 3:4]])
    r = int(radius)
    for _ in range(3):
        for axis in (0, 1):
            pad = [(0, 0)] * 3
            pad[axis] = (r, r)
            p = np.pad(pm, pad, mode="edge")
            cs = np.cumsum(p, axis=axis, dtype=np.float64)
            zero = np.zeros_like(np.take(cs, [0], axis=axis))
            cs = np.concatenate([zero, cs], axis=axis)
            n = pm.shape[axis]
            hi = np.take(cs, np.arange(2 * r + 1, 2 * r + 1 + n), axis=axis)
            lo = np.take(cs, np.arange(0, n), axis=axis)
            pm = ((hi - lo) / (2 * r + 1)).astype(np.float32)
    a = pm[..., 3]
    rgb = np.where(a[..., None] > 1e-6, pm[..., :3] / np.maximum(a[..., None], 1e-6), 0.0)
    return np.dstack([rgb, a]).astype(np.float32)


def shadow_of(shape_alpha, radius, strength, dy=2):
    """A soft drop shadow (black) from an alpha mask, moved down by dy."""
    h, w = shape_alpha.shape
    c = np.zeros((h, w, 4), np.float32)
    c[..., 3] = np.roll(shape_alpha, dy, axis=0) * strength
    if dy > 0:
        c[:dy, :, 3] = 0
    return blur(c, radius)


def bar_tops(W, dip_frac=0.58, high=11.0, low=62.0):
    """Home's bottom bar: the y of its top edge in each column, as RiftWii
    paints the dip (high at the sides, sinking over the middle dip_frac)."""
    dip = W * dip_frac
    left = W / 2 - dip / 2
    xs = np.arange(W, dtype=np.float32) + 0.5
    t = (xs - left) / dip
    inside = (t > 0) & (t < 1)
    d = np.where(inside, np.minimum(smooth(t / 0.3), smooth((1 - t) / 0.3)), 0.0)
    return high + (low - high) * d


def save(img, path):
    img.save(path, optimize=True)
    return str(path)


def chevron(c, cx, cy, size, direction, rgb=(240, 246, 255), width=2.4, a=1.0):
    """An anti-aliased chevron (left, right, up, down) centred on cx, cy."""
    h, w = c.shape[:2]
    x, y = grid(w, h)
    s = size / 2
    pts = {"left": ((cx + s * 0.45, cy - s), (cx - s * 0.45, cy), (cx + s * 0.45, cy + s)),
           "right": ((cx - s * 0.45, cy - s), (cx + s * 0.45, cy), (cx - s * 0.45, cy + s)),
           "up": ((cx - s, cy + s * 0.45), (cx, cy - s * 0.45), (cx + s, cy + s * 0.45)),
           "down": ((cx - s, cy - s * 0.45), (cx, cy + s * 0.45), (cx + s, cy - s * 0.45))}[direction]
    dist = np.full((h, w), 1e9, np.float32)
    for (ax, ay), (bx, by) in ((pts[0], pts[1]), (pts[1], pts[2])):
        vx, vy = bx - ax, by - ay
        tt = np.clip(((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy), 0, 1)
        dist = np.minimum(dist, np.hypot(x - ax - vx * tt, y - ay - vy * tt))
    over(c, rgb, np.clip(width / 2 + 0.5 - dist, 0, 1) * a)


def triangle(c, pts, rgb, a=1.0, ss=4):
    """A filled triangle, anti-aliased by supersampling."""
    h, w = c.shape[:2]
    x, y = grid(w * ss, h * ss)
    x, y = x / ss, y / ss
    (ax, ay), (bx, by), (qx, qy) = pts

    def side(px, py, ex, ey, fx, fy):
        return (fx - ex) * (py - ey) - (fy - ey) * (px - ex)

    s1 = side(x, y, ax, ay, bx, by)
    s2 = side(x, y, bx, by, qx, qy)
    s3 = side(x, y, qx, qy, ax, ay)
    ins = ((s1 >= 0) & (s2 >= 0) & (s3 >= 0)) | ((s1 <= 0) & (s2 <= 0) & (s3 <= 0))
    cov = ins.astype(np.float32).reshape(h, ss, w, ss).mean(axis=(1, 3))
    over(c, rgb, cov * a)


# --- sound -------------------------------------------------------------------

def write_wav(path, samples, rate=32000, stereo=None):
    """16-bit PCM WAV from float samples in -1..1 (mono, or `stereo` the
    right channel)."""
    left = np.clip(samples, -1, 1)
    right = left if stereo is None else np.clip(stereo, -1, 1)
    data = np.empty(left.size * 2, np.int16)
    data[0::2] = (left * 32000).astype(np.int16)
    data[1::2] = (right * 32000).astype(np.int16)
    with wavefile.open(str(path), "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(data.tobytes())


def envelope(n, attack, decay, rate):
    t = np.arange(n) / rate
    a = np.clip(t / max(attack, 1e-4), 0, 1)
    return a * np.exp(-t / decay)


def to_ogg(wav_path, ogg_path, quality=3):
    """Encodes a WAV as Ogg Vorbis at 48000 Hz: RiftWii's music player takes
    44100 or 48000 Hz only, so a tune synthesised at a lower rate is
    resampled here."""
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(wav_path), "-ar", "48000",
                    "-c:a", "libvorbis", "-q:a", str(quality), str(ogg_path)], check=True)
