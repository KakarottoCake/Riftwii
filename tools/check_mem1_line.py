#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fails the Wii build unless 0x81000000 lies where libogc's start leaves nothing.

libogc's SYS_Init, which crt0 runs before main, boots the DSP by copying
128 bytes of DSP code to 0x81000000 and copies the old bytes back
afterwards. On a real Wii that code was still there when libogc next used
the memory (Dolphin never shows it). 2611-239 died on every console: new
code had moved libogc's list of reset handlers to 0x81000014, and
SYS_RegisterResetFunc followed the DSP code's sixth word as a pointer.

Home's scan stack was linked last, after every library, so that it covered
the line. Since the program ends far below it, the line is in the heap,
which is as safe: before the DSP code is copied, libogc puts only the
command line (__CheckARGV) and its 64 KiB kernel workspace
(__lwp_wkspace_init) at the heap's start, __Arena1Lo; nothing else there
is written until main's first malloc, which takes the memory as it finds
it. Checked with the workspace, a command line of up to ARGV_ROOM and the
margin below the line.

Should the program ever grow back that far, a buffer that nothing uses
before main can cover the line instead, as Home's scan stack did: name it
riftwii_mem1_line_cover and link it after every library (git history:
wii/core/mem1tail.c). It passes when the line is MARGIN inside it.

It names what is at the line when this does not hold.

Usage: check_mem1_line.py riftwii.elf
"""
import argparse
import struct
import sys

LINE = 0x81000000
# Room left below the line, and on both sides inside a covering buffer.
# The DSP code takes 128 bytes from the line up; this is a wide margin.
MARGIN = 0x1000
COVER = "riftwii_mem1_line_cover"
# What libogc writes from __Arena1Lo before SYS_Init boots the DSP: the
# command line and argv[] (libogc's argv.c, far below this from any
# loader), then the kernel workspace (system.c's KERNEL_HEAP).
ARGV_ROOM = 0x10000
KERNEL_WORKSPACE = 0x10000
ARENA = "__Arena1Lo"


def symbols(path, sized=True):
    """(address, size, name) of every data or code symbol of an ELF32 big-endian file:
    the sized ones, or with sized=False every named one (linker symbols included)."""
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
            if sized and (sym_size == 0 or kind not in (1, 2)):  # STT_OBJECT, STT_FUNC
                continue
            if name == 0:
                continue
            end = data.index(b"\0", str_offset + name)
            found.append((value, sym_size, data[str_offset + name:end].decode("ascii", "replace")))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("elf")
    args = parser.parse_args()

    syms = symbols(args.elf)
    low, high = LINE - MARGIN, LINE + MARGIN
    for start, size, name in syms:
        if name == COVER and start <= low and start + size >= high:
            print("MEM1 line: 0x%08x is 0x%x bytes into %s (0x%08x-0x%08x)"
                  % (LINE, LINE - start, COVER, start, start + size))
            return 0
    arena = [s for s in symbols(args.elf, sized=False) if s[2] == ARENA]
    if not arena:
        print("check_mem1_line: %s has no %s" % (args.elf, ARENA), file=sys.stderr)
        return 1
    arena_lo = (arena[0][0] + 31) & ~31
    untouched = arena_lo + ARGV_ROOM + KERNEL_WORKSPACE
    if untouched <= low:
        print("MEM1 line: 0x%08x is in the heap, 0x%x bytes past what libogc's start writes "
              "(the program ends at 0x%08x)" % (LINE, low + MARGIN - untouched, arena_lo))
        return 0
    near = sorted(s for s in syms if s[0] < high and s[0] + s[1] > low)
    print("check_mem1_line: the program ends at 0x%08x, too close to 0x%08x: libogc's start-up "
          "writes 0x%x bytes from there, and its DSP boot code is copied to the line. There now:"
          % (arena_lo, LINE, ARGV_ROOM + KERNEL_WORKSPACE), file=sys.stderr)
    for address, sym_size, name in near:
        print("  0x%08x %6d %s" % (address, sym_size, name), file=sys.stderr)
    print("Free MEM1, or link a buffer unused before main, named %s, last so that it covers "
          "the line with 0x%x bytes to spare on each side (this script's notes)." % (COVER, MARGIN),
          file=sys.stderr)
    return 1

if __name__ == "__main__":
    sys.exit(main())
