# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make synthetic installed-title records for Dolphin's d2x emulation.

Usage: python tools/dolphin/make_d2x_slot.py USER_FOLDER SLOT BASE
       [--major 11] [--revision 21011] [--release beta3]

These records contain no IOS program, keys or Nintendo signatures. Use an
isolated Dolphin user folder with emulation stopped, never a console NAND.
TMD layout: https://wiibrew.org/wiki/Title_metadata
"""

import argparse
import hashlib
from pathlib import Path
import struct


# Base IOS numbers accepted by Dolphin's VersionInfo.cpp (excluding MIOS).
KNOWN_BASES = (
    4, 9, 11, 12, 13, 14, 15, 17, 20, 21, 22, 28, 30, 31, 33, 34,
    35, 36, 37, 38, 40, 41, 43, 45, 46, 48, 50, 51, 52, 53, 55,
    56, 57, 58, 59, 60, 61, 62, 70, 80,
)


def bounded_number(minimum, maximum):
    def parse(text):
        try:
            value = int(text, 10)
        except ValueError as error:
            raise argparse.ArgumentTypeError("expected a decimal number") from error
        if not minimum <= value <= maximum:
            raise argparse.ArgumentTypeError(f"expected {minimum} through {maximum}")
        return value

    return parse


def release_string(text):
    # Leave a zero terminator in the parser's 16-byte field.
    if len(text) > 15 or any(ord(char) < 0x20 or ord(char) > 0x7E for char in text):
        raise argparse.ArgumentTypeError("release must be at most 15 printable ASCII characters")
    return text


def make_content(base, major, release):
    content = bytearray(0x40)
    struct.pack_into(">IIII", content, 0, 0x1EE7C105, 1, major, base)
    content[0x10:0x13] = b"d2x"
    encoded_release = release.encode("ascii")
    content[0x20:0x20 + len(encoded_release)] = encoded_release
    return bytes(content)


def make_tmd(title_id, revision, content):
    # One 0x24-byte content record follows the 0x1E4-byte header.
    tmd = bytearray(0x208)
    struct.pack_into(">I", tmd, 0, 0x00010001)
    issuer = b"Root-RiftWii-Test-CP"
    tmd[0x140:0x140 + len(issuer)] = issuer
    struct.pack_into(">Q", tmd, 0x18C, title_id)
    struct.pack_into(">I", tmd, 0x194, 1)
    struct.pack_into(">HHH", tmd, 0x1DC, revision, 1, 0)
    struct.pack_into(">IHHQ", tmd, 0x1E4, 0, 0, 1, len(content))
    tmd[0x1F4:0x208] = hashlib.sha1(content).digest()
    return bytes(tmd)


def make_ticket(title_id):
    ticket = bytearray(0x2A4)
    struct.pack_into(">I", ticket, 0, 0x00010001)
    issuer = b"Root-RiftWii-Test-XS"
    ticket[0x140:0x140 + len(issuer)] = issuer
    struct.pack_into(">Q", ticket, 0x1DC, title_id)
    # Content index zero is allowed. The key and signature remain zeros:
    # Dolphin reads the installed content directly, without decrypting it.
    ticket[0x222] = 0x80
    return bytes(ticket)


def write_slot(user_folder, slot, base, major, revision, release):
    title_id = 0x0000000100000000 | slot
    nand = user_folder / "Wii"
    content_dir = nand / "title" / "00000001" / f"{slot:08x}" / "content"
    content = make_content(base, major, release)
    records = {
        content_dir / "title.tmd": make_tmd(title_id, revision, content),
        content_dir / "00000000.app": content,
        nand / "ticket" / "00000001" / f"{slot:08x}.tik": make_ticket(title_id),
    }
    # Check all three before writing, so an existing installation survives
    # an accidental repeat with different settings. Identical repeats work.
    for path, data in records.items():
        if path.exists() and path.read_bytes() != data:
            raise FileExistsError(f"refusing to replace different data at {path}; choose an unused slot")
    for path, data in records.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("user_folder", type=Path)
    parser.add_argument("slot", type=bounded_number(200, 255))
    parser.add_argument("base", type=int, choices=KNOWN_BASES,
                        help="base IOS known to Dolphin, for example 56, 57 or 58")
    parser.add_argument("--major", type=bounded_number(1, 999), default=11)
    parser.add_argument("--revision", type=bounded_number(0, 65535), default=21011)
    parser.add_argument("--release", type=release_string, default="beta3")
    args = parser.parse_args()
    try:
        write_slot(args.user_folder, args.slot, args.base, args.major, args.revision, args.release)
    except OSError as error:
        parser.exit(1, f"Cannot create the test slot: {error}\n")
    print(f"Created synthetic d2x IOS{args.slot}, base {args.base}, revision {args.revision} "
          f"in {args.user_folder / 'Wii'}.")


if __name__ == "__main__":
    main()
