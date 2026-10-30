#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Plays the RiftWii channel's banner and icon off the Wii: renders the
same panes and animations make_channel.py writes, to video. Needs Pillow,
numpy and ffmpeg.

    preview_channel.py <art dir> <out dir>

writes banner.mp4 (banner_start, then banner_loop twice; 810x456, as a
16:9 Wii Menu shows it) and icon.mp4 (the tile, three times its size, 4:3).
"""
import math
import os
import subprocess
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_channel as mc  # noqa: E402


def matrix(node, values, narrow=1.0):
    """The node's own transform (scale, then turn, then move), 3x3.
    `narrow` scales it across, as a 16:9 Wii Menu does to a pane that keeps
    its shape (its position stays)."""
    x = values.get("x", node.pos[0])
    y = values.get("y", node.pos[1])
    r = math.radians(values.get("rot", node.rot))
    sx = values.get("sx", node.scale[0]) * narrow
    sy = values.get("sy", node.scale[1])
    c, s = math.cos(r), math.sin(r)
    return np.array([[c * sx, -s * sy, x], [s * sx, c * sy, y], [0, 0, 1]])


class Renderer:
    # `wide_x` 0.75 draws as a 16:9 Wii Menu does: the layout stretched
    # across by 4/3, the panes flagged to keep their shape narrowed back
    # first (once, at the topmost one), as riftwii/brlyt.hpp's layout_quads.
    def __init__(self, textures, width, height, zoom=1.0, wide_x=1.0):
        self.w, self.h, self.zoom, self.wide_x = width, height, zoom, wide_x
        self.tex = {}
        for name, img in textures.items():
            a = np.array(img.pixels, np.float32).reshape(img.height, img.width, 4)
            self.tex[name] = a
        self.tint_cache = {}

    def tinted(self, node):
        key = (node.name, node.tex, tuple(node.colors))
        if key not in self.tint_cache:
            t = self.tex[node.tex]
            th, tw = t.shape[:2]
            corners = np.array(node.colors, np.float32).reshape(2, 2, 4)
            u = np.linspace(0, 1, tw)[None, :, None]
            v = np.linspace(0, 1, th)[:, None, None]
            top = corners[0, 0] * (1 - u) + corners[0, 1] * u
            bottom = corners[1, 0] * (1 - u) + corners[1, 1] * u
            grid = top * (1 - v) + bottom * v
            out = t * grid / 255.0
            self.tint_cache[key] = out
        return self.tint_cache[key]

    def frame(self, root, tracks, f):
        values = {}
        for (pane, target), keys in tracks.items():
            values.setdefault(pane, {})[target] = mc.hermite_at(keys, f)
        canvas = np.zeros((self.h, self.w, 3), np.float32)
        screen = np.array([[self.zoom / self.wide_x, 0, self.w / 2], [0, -self.zoom, self.h / 2], [0, 0, 1]])

        def draw(node, parent, alpha, narrowed=False):
            v = values.get(node.name, {})
            narrow = node.keep_aspect and not narrowed
            m = parent @ matrix(node, v, self.wide_x if narrow else 1.0)
            a = alpha * v.get("alpha", node.alpha) / 255.0
            if node.tex and a > 0.002 and abs(np.linalg.det(m)) > 1e-9:
                t = self.tinted(node)
                th, tw = t.shape[:2]
                w, h = node.size
                tex_to_local = np.array([[w / tw, 0, -w / 2], [0, -h / th, h / 2], [0, 0, 1]])
                full = screen @ m @ tex_to_local
                inv = np.linalg.inv(full)
                src = Image.fromarray(np.clip(t, 0, 255).astype(np.uint8))
                warped = src.transform((self.w, self.h), Image.AFFINE, tuple(inv[:2].ravel()), Image.BILINEAR)
                p = np.asarray(warped, np.float32)
                pa = p[..., 3:4] / 255.0 * a
                canvas[:] = canvas * (1 - pa) + p[..., :3] * pa
            for c in node.children:
                draw(c, m, a, narrowed or narrow)

        for c in root.children:
            draw(c, np.eye(3), 1.0)
        return np.clip(canvas, 0, 255).astype(np.uint8)


def video(path, frames, w, h):
    p = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgb24",
                          "-s", f"{w}x{h}", "-r", "60", "-i", "-", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                          "-crf", "16", path], stdin=subprocess.PIPE)
    for fr in frames:
        p.stdin.write(fr.tobytes())
    p.stdin.close()
    p.wait()


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    art, textures, images = mc.load_art(argv[1])
    out = argv[2]
    os.makedirs(out, exist_ok=True)

    root, start, loop = mc.scene_banner(art)
    r = Renderer(images, 810, 456, wide_x=0.75)
    frames = [r.frame(root, start, f) for f in range(mc.START)]
    frames += [r.frame(root, loop, f) for f in range(mc.LOOP)] * 2
    for f in (40, 70, mc.START - 1):
        Image.fromarray(frames[f]).save(os.path.join(out, f"banner_{f:03d}.png"))
    Image.fromarray(frames[mc.START + 280]).save(os.path.join(out, "banner_loop.png"))
    video(os.path.join(out, "banner.mp4"), frames, 810, 456)

    root, anim = mc.scene_icon(art)
    r = Renderer(images, 384, 288, zoom=3.0)
    frames = [r.frame(root, anim, f) for f in range(mc.LOOP)] * 2
    Image.fromarray(frames[320]).save(os.path.join(out, "icon.png"))
    video(os.path.join(out, "icon.mp4"), frames, 384, 288)
    print("preview written to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
