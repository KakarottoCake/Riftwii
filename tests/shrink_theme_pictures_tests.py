# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with python3 tests/shrink_theme_pictures_tests.py from any directory
(needs Pillow)."""
import importlib.util
import io
from pathlib import Path
import random
import tempfile
import unittest

from PIL import Image

REPO = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("shrink", REPO / "tools/shrink_theme_pictures.py")
shrink = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(shrink)


def noisy(mode, size=(64, 64)):
    """A gradient with faint noise, like the generated wood: 6-bit colour
    removes the noise, so the picture gets much smaller."""
    generator = random.Random(7)
    picture = Image.new(mode, size)
    channels = len(mode)
    pixels = []
    for y in range(size[1]):
        for x in range(size[0]):
            values = [min(255, 40 + x * 2 + y + generator.randrange(3)) for _ in range(channels)]
            pixels.append(tuple(values) if channels > 1 else values[0])
    picture.putdata(pixels)
    return picture


def alpha_of(data):
    return Image.open(io.BytesIO(data)).convert("RGBA").getchannel("A").tobytes()


class TransparentColourTests(unittest.TestCase):
    def shrunk(self, picture, transparency):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "picture.png"
            picture.save(path, "PNG", transparency=transparency)
            original = path.read_bytes()
            old, new = shrink.shrink(path, check_only=False)
            self.assertLess(new, old, "the picture should get smaller")
            rewritten = path.read_bytes()
            self.assertNotEqual(rewritten, original)
            self.assertFalse(any(Path(folder).glob("*.shrinking")), "no temporary file is left")
            return original, rewritten

    def test_rgb_trns_colour_stays_transparent(self):
        picture = noisy("RGB")
        key = (0, 0, 0)
        for x in range(16):
            picture.putpixel((x, 0), key)
        original, rewritten = self.shrunk(picture, key)
        self.assertEqual(alpha_of(rewritten), alpha_of(original))
        self.assertEqual(alpha_of(rewritten)[0], 0)

    def test_grey_trns_colour_stays_transparent(self):
        picture = noisy("L")
        for x in range(16):
            picture.putpixel((x, 0), 0)
        original, rewritten = self.shrunk(picture, 0)
        self.assertEqual(alpha_of(rewritten), alpha_of(original))
        self.assertEqual(alpha_of(rewritten)[0], 0)


class StableTests(unittest.TestCase):
    def test_second_run_changes_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "backdrop_00.png"
            noisy("RGB").save(path, "PNG")
            shrink.shrink(path, check_only=False)
            once = path.read_bytes()
            old, new = shrink.shrink(path, check_only=False)
            self.assertEqual(old, new)
            self.assertEqual(path.read_bytes(), once)


if __name__ == "__main__":
    unittest.main()
