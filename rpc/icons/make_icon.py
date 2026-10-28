#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""RiftWii-RPC's square icon, drawn from RiftWii's banner: the night sky,
the glowing ring and the rift through it, with Discord's green "online" dot
in the corner. Needs Pillow.

    python3 rpc/icons/make_icon.py

writes, next to this script:
  riftwii-rpc.png       1024x1024, full bleed: the Discord application's
                        icon and the rich presence's small picture
  riftwii-rpc-mac.png   1024x1024 on Apple's icon grid (a margin round it)
  riftwii-rpc.ico       Windows, 16 to 256 pixels
  riftwii-rpc.icns      macOS (only where iconutil is, on a Mac)
"""
import math
import os
import random
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
SIZE = 1024
# Drawn at twice the size and shrunk, for smooth edges.
SCALE = 2


def radial(size, center, radius, inner, outer):
    """A circular gradient from `inner` at `center` to `outer` at `radius`."""
    w, h = size
    cx, cy = center
    img = Image.new("RGBA", size)
    px = img.load()
    for y in range(h):
        for x in range(w):
            t = min(1.0, math.hypot(x - cx, y - cy) / radius)
            t = t * t * (3 - 2 * t)  # smoothstep: no hard edge
            px[x, y] = tuple(int(inner[i] + (outer[i] - inner[i]) * t) for i in range(4))
    return img


def glow(layer, radius, strength=1):
    """`layer` blurred and added onto itself `strength` times: a soft halo."""
    halo = layer.filter(ImageFilter.GaussianBlur(radius))
    out = Image.new("RGBA", layer.size)
    for _ in range(strength):
        out = Image.alpha_composite(out, halo)
    return out


def rift_points(s, top, bottom, seed):
    """The rift: a jagged line down the middle, as on the banner."""
    rng = random.Random(seed)
    points = []
    steps = 9
    for i in range(steps + 1):
        y = top + (bottom - top) * i / steps
        # Widest in the middle, where the ring crosses it.
        swing = 46 * s * math.sin(math.pi * i / steps) + 8 * s
        # Left and right in turn, like lightning.
        side = 1 if i % 2 else -1
        x = SIZE * s / 2 + (side * rng.uniform(0.5, 1) * swing if 0 < i < steps else 0)
        points.append((x, y))
    return points


def artwork():
    s = SCALE
    n = SIZE * s
    # Night sky: indigo in the middle, deep navy at the edges.
    img = radial((n // 8, n // 8), (n // 16, n // 18), n // 11, (70, 48, 160, 255), (6, 9, 34, 255))
    img = img.resize((n, n), Image.BICUBIC)

    # Nebula clouds, purple and blue, soft.
    clouds = Image.new("RGBA", (n, n))
    d = ImageDraw.Draw(clouds)
    for (x, y, rx, ry, colour) in [
        (0.25, 0.30, 0.30, 0.16, (120, 70, 230, 90)),
        (0.78, 0.64, 0.30, 0.18, (40, 110, 230, 90)),
        (0.55, 0.85, 0.40, 0.12, (110, 60, 200, 70)),
        (0.30, 0.72, 0.22, 0.10, (60, 140, 255, 60)),
    ]:
        d.ellipse([(x - rx) * n, (y - ry) * n, (x + rx) * n, (y + ry) * n], fill=colour)
    img = Image.alpha_composite(img, clouds.filter(ImageFilter.GaussianBlur(70 * s)))

    # Stars.
    stars = Image.new("RGBA", (n, n))
    d = ImageDraw.Draw(stars)
    rng = random.Random(7)
    for _ in range(70):
        x, y = rng.uniform(0.06, 0.94) * n, rng.uniform(0.06, 0.94) * n
        r = rng.choice([1.2, 1.6, 2.2, 3.0]) * s
        a = rng.randint(120, 255)
        d.ellipse([x - r, y - r, x + r, y + r], fill=(220, 235, 255, a))
    img = Image.alpha_composite(img, glow(stars, 3 * s, 2))
    img = Image.alpha_composite(img, stars)

    # The light behind the rift.
    core = radial((n // 4, n // 4), (n // 8, n // 8), n // 8, (140, 220, 255, 150), (140, 220, 255, 0))
    img = Image.alpha_composite(img, core.resize((n, n), Image.BICUBIC))

    # The ring: a tilted ellipse, cyan, glowing.
    ring = Image.new("RGBA", (n, n))
    d = ImageDraw.Draw(ring)
    rw, rh, width = 0.39 * n, 0.13 * n, int(34 * s)
    c = n / 2
    d.ellipse([c - rw, c - rh, c + rw, c + rh], outline=(80, 200, 255, 255), width=width)
    inner = Image.new("RGBA", (n, n))
    ImageDraw.Draw(inner).ellipse([c - rw, c - rh, c + rw, c + rh], outline=(210, 245, 255, 255),
                                  width=max(2, width // 3))
    ring = Image.alpha_composite(ring, inner)
    ring = ring.rotate(14, resample=Image.BICUBIC, center=(c, c))
    img = Image.alpha_composite(img, glow(ring, 26 * s, 4))
    img = Image.alpha_composite(img, ring)

    # The rift.
    bolt = Image.new("RGBA", (n, n))
    d = ImageDraw.Draw(bolt)
    points = rift_points(s, 0.10 * n, 0.90 * n, seed=3)
    d.line(points, fill=(120, 210, 255, 255), width=int(44 * s), joint="curve")
    img = Image.alpha_composite(img, glow(bolt, 34 * s, 4))
    core_line = Image.new("RGBA", (n, n))
    ImageDraw.Draw(core_line).line(points, fill=(255, 255, 255, 255), width=int(20 * s), joint="curve")
    img = Image.alpha_composite(img, glow(core_line, 6 * s, 1))
    img = Image.alpha_composite(img, core_line)

    # Discord's "online" dot, with a ring of the sky's colour round it so
    # it reads at 16 pixels too.
    dot = Image.new("RGBA", (n, n))
    d = ImageDraw.Draw(dot)
    cx, cy, r, edge = 0.80 * n, 0.80 * n, 0.088 * n, 0.03 * n
    d.ellipse([cx - r - edge, cy - r - edge, cx + r + edge, cy + r + edge], fill=(8, 10, 34, 255))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(35, 165, 90, 255))
    img = Image.alpha_composite(img, dot)
    return img


def rounded(img, radius_fraction):
    n = img.size[0]
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, n - 1, n - 1], radius=int(n * radius_fraction), fill=255)
    out = Image.new("RGBA", img.size)
    out.paste(img, (0, 0), ImageChops.multiply(mask, img.getchannel("A")))
    return out


def main():
    art = artwork()
    full = rounded(art, 0.22).resize((SIZE, SIZE), Image.LANCZOS)
    full.save(os.path.join(HERE, "riftwii-rpc.png"))

    # Apple's grid: the shape is 824 of 1024 pixels, with a little shadow.
    body = rounded(art, 0.225).resize((824, 824), Image.LANCZOS)
    mac = Image.new("RGBA", (SIZE, SIZE))
    shadow = Image.new("RGBA", (SIZE, SIZE))
    shadow.paste((0, 0, 0, 110), (100, 112), body.getchannel("A"))
    mac = Image.alpha_composite(mac, shadow.filter(ImageFilter.GaussianBlur(14)))
    mac.alpha_composite(body, (100, 100))
    mac.save(os.path.join(HERE, "riftwii-rpc-mac.png"))

    full.save(os.path.join(HERE, "riftwii-rpc.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64),
                                                            (128, 128), (256, 256)])

    if shutil.which("iconutil"):
        with tempfile.TemporaryDirectory() as tmp:
            iconset = os.path.join(tmp, "riftwii-rpc.iconset")
            os.mkdir(iconset)
            for size in (16, 32, 128, 256, 512):
                for factor in (1, 2):
                    name = f"icon_{size}x{size}{'@2x' if factor == 2 else ''}.png"
                    mac.resize((size * factor, size * factor), Image.LANCZOS).save(os.path.join(iconset, name))
            subprocess.run(["iconutil", "-c", "icns", iconset, "-o", os.path.join(HERE, "riftwii-rpc.icns")],
                           check=True)
    else:
        print("iconutil not found (not a Mac): riftwii-rpc.icns left as it is", file=sys.stderr)


if __name__ == "__main__":
    main()
