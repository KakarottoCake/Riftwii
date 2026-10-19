#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Packs a DOL: its sections Zstandard-compressed, behind a stub that
unpacks them at start (runtime/dolpack, the format in dolpack.h).

    python3 tools/pack_dol.py in.dol build-runtime/dolpack.bin out.dol

The packed DOL has two sections: the stub at DOLPACK_BASE (0x80900000)
and, right under it, the table and one frame per section of in.dol. It
is a plain DOL to the Homebrew Channel and RiftWii's channel. Every frame
is decompressed again here and compared before out.dol is written.
The idea is FIX94's dolxz (NOTICE.md); the code and format are RiftWii's.
"""

import argparse
import struct
import subprocess
import sys

DOLPACK_BASE = 0x80900000
FIELDS_MAGIC = 0x52575043   # "RWPC", after the stub's "_arg" block
FIELDS_OFFSET = 32          # b, "_arg", six argv words
TABLE_MAGIC = 0x5257504B    # "RWPK"
TABLE_VERSION = 1
TABLE_HEADER_BYTES = 32
SECTION_BYTES = 16
# Under it: RiftWii's channel forwarders and their heaps (the vWii one
# ends near 0x801f0000); the packed bytes must stay above.
LOW_LIMIT = 0x80200000
# The unpacked DOL's sections must all lie above the stub, its BSS and
# the command line a loader puts after it.
COMMAND_LINE_ROOM = 0x1000
HEADER_BYTES = 0x100


def align(value, to=32):
    return (value + to - 1) & ~(to - 1)


def read_dol(data):
    if len(data) < HEADER_BYTES:
        sys.exit("pack_dol: the input is too short to be a DOL")
    words = struct.unpack(">%dI" % (HEADER_BYTES // 4), data[:HEADER_BYTES])
    offsets, addresses, sizes = words[0:18], words[18:36], words[36:54]
    bss_address, bss_size, entry = words[54], words[55], words[56]
    sections = []
    for i in range(18):
        if sizes[i] == 0:
            continue
        if offsets[i] + sizes[i] > len(data):
            sys.exit("pack_dol: section %d lies past the file's end" % i)
        sections.append((addresses[i], data[offsets[i]:offsets[i] + sizes[i]]))
    if addresses[0] == 0 or sizes[0] == 0:
        sys.exit("pack_dol: the DOL has no first text section")
    return sections, addresses[0], bss_address, bss_size, entry


def zstd(args, data, tool):
    try:
        done = subprocess.run([tool] + args, input=data, stdout=subprocess.PIPE, check=True)
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit("pack_dol: %s %s failed: %s" % (tool, " ".join(args), e))
    return done.stdout


def unpacked_sections(data, tool="zstd"):
    """The sections of a DOL this script packed, decompressed again, as
    [(address, bytes)]; None for a DOL that is not packed. For the release
    tools that look inside boot.dol (tools/make_update_pack.py)."""
    sections = read_dol(data)[0]
    table = next((body for _, body in sections
                  if len(body) >= TABLE_HEADER_BYTES and struct.unpack_from(">I", body)[0] == TABLE_MAGIC), None)
    if table is None:
        return None
    _, version, count = struct.unpack_from(">3I", table)
    if version != TABLE_VERSION:
        sys.exit("pack_dol: a packed DOL of table version %u, this script knows %u" % (version, TABLE_VERSION))
    out = []
    for i in range(count):
        address, size, at, frame_bytes = struct.unpack_from(">4I", table, TABLE_HEADER_BYTES + SECTION_BYTES * i)
        body = zstd(["-q", "-d", "-c"], table[at:at + frame_bytes], tool)
        if len(body) != size:
            sys.exit("pack_dol: the section at 0x%08x unpacks to %u bytes, not %u" % (address, len(body), size))
        out.append((address, body))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dol")
    parser.add_argument("stub")
    parser.add_argument("out")
    parser.add_argument("--zstd", default="zstd", help="the zstd command-line tool")
    parser.add_argument("--level", type=int, default=19)
    args = parser.parse_args()

    data = open(args.dol, "rb").read()
    sections, text0, bss_address, bss_size, entry = read_dol(data)
    stub = bytearray(open(args.stub, "rb").read())

    magic, _, stub_base, stub_image_end, stub_end = struct.unpack_from(">5I", stub, FIELDS_OFFSET)
    if magic != FIELDS_MAGIC or stub_base != DOLPACK_BASE or stub_image_end - stub_base != len(stub):
        sys.exit("pack_dol: %s is not the dolpack stub this script knows" % args.stub)
    lowest = min(address for address, _ in sections)
    if stub_end + COMMAND_LINE_ROOM > lowest:
        sys.exit("pack_dol: the DOL starts at 0x%08x, under the stub's end 0x%08x" % (lowest, stub_end))
    end = max([address + len(body) for address, body in sections] + [bss_address + bss_size])

    # The frames, each checked against its section.
    frames = []
    for address, body in sections:
        frame = zstd(["-q", "-%d" % args.level, "-c"], body, args.zstd)
        if zstd(["-q", "-d", "-c"], frame, args.zstd) != body:
            sys.exit("pack_dol: the section at 0x%08x does not come back the same from zstd" % address)
        frames.append(frame)

    table = bytearray(struct.pack(">8I", TABLE_MAGIC, TABLE_VERSION, len(sections), entry, text0,
                                  bss_address, bss_size, end))
    at = align(TABLE_HEADER_BYTES + SECTION_BYTES * len(sections), 4)
    entries = bytearray()
    body = bytearray()
    for (address, section), frame in zip(sections, frames):
        entries += struct.pack(">4I", address, len(section), at + len(body), len(frame))
        body += frame
        body += bytes(align(len(body), 4) - len(body))
    table += entries
    table += bytes(at - len(table))
    table += body
    table += bytes(align(len(table)) - len(table))

    table_address = DOLPACK_BASE - len(table)
    if table_address < LOW_LIMIT:
        sys.exit("pack_dol: %u packed bytes reach under 0x%08x" % (len(table), LOW_LIMIT))
    struct.pack_into(">I", stub, FIELDS_OFFSET + 4, table_address)
    stub += bytes(align(len(stub)) - len(stub))

    header = [0] * (HEADER_BYTES // 4)
    header[0] = HEADER_BYTES                   # text 0: the stub
    header[18] = DOLPACK_BASE
    header[36] = len(stub)
    header[7] = HEADER_BYTES + len(stub)       # data 0: the table and frames
    header[25] = table_address
    header[43] = len(table)
    header[54] = DOLPACK_BASE + len(stub)      # BSS: the stub's, with its stack
    header[55] = stub_end - header[54]
    header[56] = DOLPACK_BASE                  # entry
    out = struct.pack(">%dI" % len(header), *header) + bytes(stub) + bytes(table)
    with open(args.out, "wb") as f:
        f.write(out)
    print("pack_dol: %s, %u bytes -> %s, %u bytes (%u%%), %u sections at 0x%08x-0x%08x" %
          (args.dol, len(data), args.out, len(out), 100 * len(out) // len(data), len(sections), table_address,
           stub_end))


if __name__ == "__main__":
    main()
