#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes a theme's PNG pictures smaller without a visible change.

    python tools/shrink_theme_pictures.py themes/Bookshelf [more folders or PNGs]
    python tools/shrink_theme_pictures.py --check themes/Bookshelf

Lossless recompression gains almost nothing on these pictures, so each is
made simpler to compress in a way nobody can see on a TV:

- A backdrop loop's frames (backdrop_NN.png) keep only the top 5, 6 and 5
  bits of red, green and blue: exactly what the menu keeps when it turns
  them into RGB565 textures (wii/menu/skin.cpp, to_gx_rgb565 truncates).
  The Wii shows the same pixels as before.
- Every other picture keeps 6 bits of each colour, every value rounded to
  the nearest of 64 levels (so 0 and 255 stay as they are), and its alpha
  untouched. The menu draws into an 8-bit framebuffer, so a value moves by
  2 of 255 at most, below what a TV shows; the Bookshelf walls come out
  about half the size.

A file is rewritten only when it gets smaller (through a temporary file,
so an interrupted run never leaves half a picture), and a picture already
shrunk stays the same, so running it again changes nothing. --check
reports the pictures that would still change and exits with 1 for them.
Needs Pillow.
"""
import argparse
import io
import os
import pathlib
import re
import sys

from PIL import Image

BACKDROP_FRAME = re.compile(r"^backdrop_\d\d\.png$")

# The top 6 bits, rounded to the nearest level and widened back so the
# level's value covers 0..255 (63 -> 255).
SIX_BITS = [round(round(value * 63 / 255) * 255 / 63) for value in range(256)]


def truncate_to(mask):
    return [value & mask for value in range(256)]


# RGB565 as to_gx_rgb565 makes it: the low bits dropped.
RGB565 = (truncate_to(0xF8), truncate_to(0xFC), truncate_to(0xF8))


def reduced(picture, is_backdrop_frame):
    """The picture with fewer colour bits, in the same mode it came in."""
    # A tRNS colour (RGB, grey or palette) is transparency too: it is kept
    # as an alpha channel.
    has_alpha = picture.mode in ("RGBA", "LA", "PA") or "transparency" in picture.info
    rgba = picture.convert("RGBA")
    red, green, blue, alpha = rgba.split()
    tables = RGB565 if is_backdrop_frame else (SIX_BITS, SIX_BITS, SIX_BITS)
    bands = [band.point(table) for band, table in zip((red, green, blue), tables)]
    if has_alpha:
        return Image.merge("RGBA", bands + [alpha])
    return Image.merge("RGB", bands)


def png_bytes(picture):
    buffer = io.BytesIO()
    picture.save(buffer, "PNG", optimize=True)
    return buffer.getvalue()


def shrink(path, check_only):
    """Returns (old size, new size); the new one is the old when kept."""
    original = path.read_bytes()
    with Image.open(io.BytesIO(original)) as picture:
        picture.load()
        is_backdrop_frame = BACKDROP_FRAME.match(path.name) is not None
        smaller = reduced(picture, is_backdrop_frame)
    # Pixels that were already reduced are still recompressed: the old file
    # may have come from an encoder that packs worse.
    data = png_bytes(smaller)
    if len(data) >= len(original):
        return len(original), len(original)
    if not check_only:
        temporary = path.with_name(path.name + ".shrinking")
        temporary.write_bytes(data)
        os.replace(temporary, path)
    return len(original), len(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("paths", nargs="+", type=pathlib.Path, help="theme folders or single PNGs")
    parser.add_argument("--check", action="store_true", help="change nothing; exit 1 if a picture would shrink")
    args = parser.parse_args()

    pictures = []
    for given in args.paths:
        if given.is_dir():
            pictures.extend(sorted(given.glob("*.png")))
        elif given.is_file() and given.suffix.lower() == ".png":
            pictures.append(given)
        else:
            # A mistyped folder must not pass --check by having nothing in it.
            print("%s is neither a folder nor a PNG" % given, file=sys.stderr)
            return 2

    before = after = 0
    changed = []
    for path in pictures:
        old, new = shrink(path, args.check)
        before += old
        after += new
        if new != old:
            changed.append(path)
            print("%-48s %8d -> %8d bytes" % (path.as_posix(), old, new))
    if args.check:
        if changed:
            print("%d picture(s) can still be shrunk: run tools/shrink_theme_pictures.py on them" % len(changed))
            return 1
        print("every picture is already shrunk")
        return 0
    saved = before - after
    percent = 100.0 * saved / before if before else 0.0
    print("%d picture(s) rewritten, %d -> %d bytes (%.1f%% smaller)" % (len(changed), before, after, percent))
    return 0


if __name__ == "__main__":
    sys.exit(main())
