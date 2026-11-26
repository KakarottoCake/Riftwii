#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fails the Wii build unless 0x81000000 lies where libogc's start leaves nothing.

libogc's SYS_Init copies 128 bytes of DSP code to 0x81000000 at start-up
(wii/core/mem1tail.h). 2611-239 died on every console because libogc's
list of reset handlers had moved there. Two places are safe:

- Home's scan stack, which Makefile.wii links last so that, while the
  program reaches past the line, it covers it. Checked with room on both
  sides.
- The heap, once the program ends far enough below the line. Before the
  DSP code is copied, libogc puts only the command line (__CheckARGV) and
  its 64 KiB kernel workspace (__lwp_wkspace_init) at the heap's start,
  __Arena1Lo; nothing else there is written until main's first malloc,
  which takes the memory as it finds it. Checked with the workspace, a
  command line of up to ARGV_ROOM and the margin below the line.

It names what is at the line when neither holds.

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
    arena = [s for s in symbols(args.elf, sized=False) if s[2] == ARENA]
    if arena:
        arena_lo = (arena[0][0] + 31) & ~31
        untouched = arena_lo + ARGV_ROOM + KERNEL_WORKSPACE
        if start + size <= arena_lo and untouched <= low:
            print("MEM1 line: 0x%08x is in the heap, 0x%x bytes past what libogc's start writes "
                  "(the program ends at 0x%08x)" % (LINE, low + MARGIN - untouched, arena_lo))
            return 0
    near = sorted(s for s in syms if s[0] < high and s[0] + s[1] > low)
    print("check_mem1_line: 0x%08x must be at least 0x%x bytes inside %s (0x%08x-0x%08x), "
          "or libogc's start-up data lands where its DSP boot code is copied (wii/core/mem1tail.h). "
          "There now:" % (LINE, MARGIN, STACK, start, start + size), file=sys.stderr)
    for address, sym_size, name in near:
        print("  0x%08x %6d %s" % (address, sym_size, name), file=sys.stderr)
    if start > low:
        print("The program ends too close to the line: below it by less than libogc's start-up "
              "writes (0x%x bytes) and the margin, or above it with the stack not covering it. "
              "Make the stack's file bigger or link more of RiftWii's start-up-free buffers "
              "after the libraries." % (ARGV_ROOM + KERNEL_WORKSPACE), file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
