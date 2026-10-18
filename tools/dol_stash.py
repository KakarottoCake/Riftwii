#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Adds RiftWii's start-up stash to a DOL as one more data section.

    python3 tools/dol_stash.py riftwii.dol --version 2610-460 \\
        --limit 0x81200000 menufont.zst=build_wii/menufont.zst ...

The stash holds data the menu needs only now and then (its font, its
translations, the GPL's text). The section is placed right after the
DOL's BSS, 32-byte aligned: where libogc starts RiftWii's heap. crt0
copies it to MEM2 before anything else runs (wii/core/stash.cpp), so
those bytes of MEM1 become heap. The format is in
include/riftwii/stashimage.hpp.

--limit is the highest address the section may end at: the build fails
past it. RiftWii's heap stops at the game's apploader (0x81200000), and
the Homebrew Channel refused a DOL section at 0x81300000 on a real Wii.

--image-out writes the stash image alone too (for tests/fixtures).
"""

import argparse
import struct
import sys

HEADER_BYTES = 64
ENTRY_BYTES = 32
NAME_BYTES = 24
VERSION_BYTES = 44
MAX_BYTES = 2 << 20
MAX_ENTRIES = 64
MAGIC = b"RWSTASH1"

TEXT_SECTIONS = 7
SECTION_COUNT = 18
OFFSET_TABLE = 0x00
ADDRESS_TABLE = 0x48
SIZE_TABLE = 0x90
BSS_ADDRESS = 0xD8
BSS_SIZE = 0xDC
DOL_HEADER_BYTES = 0x100


def align(value, to):
    return (value + to - 1) & ~(to - 1)


def fnv1a(data):
    value = 2166136261
    for byte in data:
        value ^= byte
        value = (value * 16777619) & 0xFFFFFFFF
    return value


def build_image(version, items):
    """items: [(name, bytes)] in order. Returns the stash image."""
    version_bytes = version.encode("ascii")
    if len(version_bytes) > VERSION_BYTES:
        raise ValueError("version %r is longer than %d bytes" % (version, VERSION_BYTES))
    if len(items) > MAX_ENTRIES:
        raise ValueError("%d entries, at most %d" % (len(items), MAX_ENTRIES))
    directory_end = HEADER_BYTES + ENTRY_BYTES * len(items)
    offset = align(directory_end, 32)
    entries = b""
    placed = []
    for name, data in items:
        name_bytes = name.encode("ascii")
        if not name_bytes or len(name_bytes) >= NAME_BYTES:
            raise ValueError("entry name %r must be 1 to %d characters" % (name, NAME_BYTES - 1))
        entries += name_bytes.ljust(NAME_BYTES, b"\0") + struct.pack(">II", offset, len(data))
        placed.append((offset, data))
        offset = align(offset + len(data), 32)
    total = offset
    if total > MAX_BYTES:
        raise ValueError("stash of %d bytes, at most %d" % (total, MAX_BYTES))
    image = bytearray(total)
    image[16:20] = struct.pack(">I", len(items))
    image[20:20 + VERSION_BYTES] = version_bytes.ljust(VERSION_BYTES, b"\0")
    image[HEADER_BYTES:directory_end] = entries
    for at, data in placed:
        image[at:at + len(data)] = data
    image[0:8] = MAGIC
    image[8:12] = struct.pack(">I", total)
    image[12:16] = struct.pack(">I", fnv1a(image[16:]))
    return bytes(image)


def add_to_dol(dol, image, limit):
    """Returns (new DOL, section address)."""
    if len(dol) < DOL_HEADER_BYTES:
        raise ValueError("not a DOL: %d bytes" % len(dol))
    header = bytearray(dol[:DOL_HEADER_BYTES])
    offsets = list(struct.unpack_from(">18I", header, OFFSET_TABLE))
    addresses = list(struct.unpack_from(">18I", header, ADDRESS_TABLE))
    sizes = list(struct.unpack_from(">18I", header, SIZE_TABLE))
    bss_address, bss_size = struct.unpack_from(">II", header, BSS_ADDRESS)
    # libogc's __Arena1Lo is the BSS's end rounded up to 32 bytes; the
    # stash starts there, and wii/core/stash.cpp looks for it there.
    address = align(bss_address + bss_size, 32)
    highest = max([addresses[i] + sizes[i] for i in range(SECTION_COUNT) if sizes[i]] + [0])
    if highest > address:
        raise ValueError("a section already ends above the BSS (0x%08x): stash added twice?" % highest)
    end = address + len(image)
    if end > limit:
        raise ValueError("the stash would end at 0x%08x, past the limit 0x%08x (%d KiB too much)"
                         % (end, limit, (end - limit + 1023) // 1024))
    free = [i for i in range(TEXT_SECTIONS, SECTION_COUNT) if sizes[i] == 0]
    if not free:
        raise ValueError("no free data section in the DOL")
    slot = free[0]
    file_offset = align(len(dol), 32)
    offsets[slot] = file_offset
    addresses[slot] = address
    sizes[slot] = len(image)
    struct.pack_into(">18I", header, OFFSET_TABLE, *offsets)
    struct.pack_into(">18I", header, ADDRESS_TABLE, *addresses)
    struct.pack_into(">18I", header, SIZE_TABLE, *sizes)
    body = bytes(header) + dol[DOL_HEADER_BYTES:]
    body = body.ljust(file_offset, b"\0") + image
    return body, address


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dol", help="the DOL, rewritten in place")
    parser.add_argument("--version", required=True, help="RiftWii's version, as RIFTWII_VERSION")
    parser.add_argument("--limit", required=True, type=lambda text: int(text, 0),
                        help="the highest address the stash may end at")
    parser.add_argument("--image-out", help="also write the stash image alone here")
    parser.add_argument("items", nargs="+", help="name=path, in order")
    args = parser.parse_args()

    items = []
    for item in args.items:
        name, separator, path = item.partition("=")
        if not separator:
            parser.error("%r is not name=path" % item)
        with open(path, "rb") as f:
            items.append((name, f.read()))
    try:
        image = build_image(args.version, items)
        with open(args.dol, "rb") as f:
            dol = f.read()
        new_dol, address = add_to_dol(dol, image, args.limit)
    except ValueError as error:
        print("dol_stash.py: %s" % error, file=sys.stderr)
        return 1
    with open(args.dol, "wb") as f:
        f.write(new_dol)
    if args.image_out:
        with open(args.image_out, "wb") as f:
            f.write(image)
    print("start-up stash: %d items, %d KiB at 0x%08x-0x%08x (limit 0x%08x)"
          % (len(items), len(image) // 1024, address, address + len(image), args.limit))
    return 0


if __name__ == "__main__":
    sys.exit(main())
