# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Felk Dolphin script: saves the drawn frame as a PNG at set frame counts.
# FRAMESHOT_OUT names the file stem (stem-<frame>.png); FRAMESHOT_AT is a
# comma-separated list of frame counts (default 1500,3000: about 25 and 50
# seconds). For unattended runs (wiiware_baseline.py).
import os
import struct
import zlib

from dolphin import event

OUT = os.environ.get("FRAMESHOT_OUT", "frameshot")
AT = sorted(int(n) for n in os.environ.get("FRAMESHOT_AT", "1500,3000").split(","))
_frames = 0


def _png(path, width, height, rgb):
    rows = b"".join(b"\x00" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 6)) +
                chunk(b"IEND", b""))


def _on_framedrawn(width, height, data):
    global _frames
    _frames += 1
    if _frames in AT:
        # A failure inside Dolphin's callback is silent: write it beside the shots.
        try:
            _png(f"{OUT}-{_frames}.png", width, height, bytes(data))
        except Exception as error:
            with open(f"{OUT}-error.txt", "a") as f:
                f.write(f"frame {_frames}: {type(error).__name__}: {error}\n")


event.on_framedrawn(_on_framedrawn)
print(f"[frameshot] waiting for frames {AT}, writing {OUT}-<frame>.png")
