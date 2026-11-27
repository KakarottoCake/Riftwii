#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Packs builds of one runtime blob into the one file the DOL embeds.

The file is each blob's size as a big-endian word, then one zstd frame of
the blobs one after another, in the order given (wii/core/packedblobs.cpp
unpacks it). Builds of one program share most of their code, so one frame
keeps a fraction of their MEM1: the Wii Remote logger's three take 10 KiB
instead of 23. The sizes stay outside the frame so the menu's memory budget can read
one without unpacking.

    pack_blobs.py OUT ZSTD BLOB...
"""
import os
import struct
import subprocess
import sys


def frame_content_size(frame):
    """The decompressed size a zstd frame's header records, or None."""
    if frame[:4] != b"\x28\xb5\x2f\xfd":
        return None
    descriptor = frame[4]
    single_segment = (descriptor >> 5) & 1
    size_bytes = [single_segment, 2, 4, 8][descriptor >> 6]
    at = 5 + (0 if single_segment else 1) + [0, 1, 2, 4][descriptor & 3]
    if size_bytes == 0:
        return None
    size = int.from_bytes(frame[at:at + size_bytes], "little")
    return size + 256 if size_bytes == 2 else size


def main():
    if len(sys.argv) < 4:
        raise SystemExit(__doc__.strip().splitlines()[-1].strip())
    out, zstd, paths = sys.argv[1], sys.argv[2], sys.argv[3:]
    blobs = [open(path, "rb").read() for path in paths]
    for path, blob in zip(paths, blobs):
        if len(blob) % 4:
            raise SystemExit("%s: %d bytes, not whole words; packedblobs.cpp reads each header in place"
                             % (path, len(blob)))
    # From a file, not a pipe: zstd records the content size in the frame
    # header only when it knows it, and packedblobs.cpp checks it.
    joined = out + ".blobs"
    with open(joined, "wb") as f:
        f.write(b"".join(blobs))
    frame = subprocess.run([zstd, "-q", "-19", "-c", joined], stdout=subprocess.PIPE, check=True).stdout
    os.remove(joined)
    total = sum(len(blob) for blob in blobs)
    if frame_content_size(frame) != total:
        raise SystemExit("pack_blobs: the zstd frame does not record its %d bytes; "
                         "packedblobs.cpp would refuse it" % total)
    sizes = struct.pack(">%dI" % len(blobs), *(len(blob) for blob in blobs))
    # Published whole or not at all: a truncated file at OUT would look current to make.
    tmp = out + ".tmp"
    try:
        with open(tmp, "wb") as f:
            f.write(sizes + frame)
        os.replace(tmp, out)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise
    print("%s: %d bytes in %d, packed" % (os.path.basename(out), total, len(sizes) + len(frame)))


if __name__ == "__main__":
    main()
