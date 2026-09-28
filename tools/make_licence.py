#!/usr/bin/env python3
"""Packs LICENSE (the GNU GPL version 3) into wii/assets/licence.bin.

RiftWii shows the licence in full under Settings > Credits and licence,
as GPLv3 section 5(d) asks of a program with an interactive interface.
The text is embedded brotli-compressed, in the same layout as the menu
font (a big-endian 32-bit unpacked size, then the brotli stream).

Run it again whenever LICENSE changes; --check only verifies that the
packed file still matches LICENSE. Needs brotli (pip install brotli).
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LICENCE = os.path.join(ROOT, "LICENSE")
OUT = os.path.join(ROOT, "wii", "assets", "licence.bin")


def text():
    # Plain LF line ends, whatever the checkout did to the file.
    return open(LICENCE, "rb").read().replace(b"\r\n", b"\n")


def main():
    import brotli

    data = text()
    if "--check" in sys.argv[1:]:
        packed = open(OUT, "rb").read()
        size = struct.unpack(">I", packed[:4])[0]
        ok = size == len(data) and brotli.decompress(packed[4:]) == data
        print("licence.bin matches LICENSE" if ok else "licence.bin is out of date: run tools/make_licence.py")
        return 0 if ok else 1
    packed = brotli.compress(data, quality=11, lgwin=22)
    assert brotli.decompress(packed) == data
    with open(OUT, "wb") as f:
        f.write(struct.pack(">I", len(data)))
        f.write(packed)
    print("LICENSE: %d bytes packed to %d in %s" % (len(data), len(packed) + 4, OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
