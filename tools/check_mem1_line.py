#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fails the Wii build unless 0x81000000 lies well inside Home's scan stack.

libogc's SYS_Init copies 128 bytes of DSP code to 0x81000000 at start-up
(wii/core/mem1tail.h). 2611-239 died on every console because libogc's
list of reset handlers had moved there. Makefile.wii links the stack
last so it covers the line; this checks that it does, with room on both
sides, and names what is there instead when it does not.

Usage: check_mem1_line.py riftwii.elf
"""
import argparse
import struct
import sys

LINE = 0x81000000
# Room the stack must leave on each side of the line. The DSP code takes
# 128 bytes from the line up; this is a wide margin.
MARGIN = 0x1000
STACK = "riftwii_home_scan_stack"


def symbols(path):
    """(address, size, name) of every sized data or code symbol of an ELF32 big-endian file."""
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 2:
        raise SystemExit("%s: not a 32-bit big-endian ELF file" % path)
    shoff, = struct.unpack_from(">I", data, 0x20)
    shentsize, shnum = struct.unpack_from(">HH", data, 0x2E)
    sections = [struct.unpack_from(">IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    found = []
    for sec in sections:
        if sec[1] != 2:  # SHT_SYMTAB
            continue
        strtab = sections[sec[6]]  # sh_link
        str_offset = strtab[4]
        offset, size, entsize = sec[4], sec[5], sec[9]
        for i in range(size // entsize):
            name, value, sym_size, info, _other, _shndx = struct.unpack_from(">IIIBBH", data, offset + i * entsize)
            kind = info & 0xF
            if sym_size == 0 or kind not in (1, 2):  # STT_OBJECT, STT_FUNC
                continue
            end = data.index(b"\0", str_offset + name)
            found.append((value, sym_size, data[str_offset + name:end].decode("ascii", "replace")))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("elf")
    args = parser.parse_args()

    syms = symbols(args.elf)
    stack = [s for s in syms if s[2] == STACK]
    if not stack:
        print("check_mem1_line: %s has no %s" % (args.elf, STACK), file=sys.stderr)
        return 1
    start, size, _ = stack[0]
    low, high = LINE - MARGIN, LINE + MARGIN
    if start <= low and start + size >= high:
        print("MEM1 line: 0x%08x is 0x%x bytes into Home's scan stack (0x%08x-0x%08x)"
              % (LINE, LINE - start, start, start + size))
        return 0
    near = sorted(s for s in syms if s[0] < high and s[0] + s[1] > low)
    print("check_mem1_line: 0x%08x must be at least 0x%x bytes inside %s (0x%08x-0x%08x), "
          "or libogc's start-up data lands where its DSP boot code is copied (wii/core/mem1tail.h). "
          "There now:" % (LINE, MARGIN, STACK, start, start + size), file=sys.stderr)
    for address, sym_size, name in near:
        print("  0x%08x %6d %s" % (address, sym_size, name), file=sys.stderr)
    if start > low:
        print("The .bss below the stack has grown past the line: make the stack's file bigger "
              "or link more of RiftWii's start-up-free buffers after the libraries.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
