#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Relocation table for a resident runtime blob that is not position
independent on its own (the RVZ blob: Zstandard's constant tables).

    rtreloc.py <blob.elf linked at 0 with --emit-relocs> <blob.bin> <alt.bin> <alt base> <out.rel>

Writes the blob's absolute references as 12-byte big-endian records
{blob offset, type, target offset}, sorted by offset, which the loader
applies for the address it installs the blob at (hook.cpp:
apply_resident_relocs). Checked here: applying them for <alt base> to
<blob.bin> must give <alt.bin>, the same objects linked there.
"""
import struct
import sys

R_PPC_ADDR32 = 1
R_PPC_ADDR16_LO = 4
R_PPC_ADDR16_HI = 5
R_PPC_ADDR16_HA = 6
ABSOLUTE = (R_PPC_ADDR32, R_PPC_ADDR16_LO, R_PPC_ADDR16_HI, R_PPC_ADDR16_HA)
# Relative to the place: the same at every base.
RELATIVE = {
    10,  # R_PPC_REL24
    11,  # R_PPC_REL14
    12,  # R_PPC_REL14_BRTAKEN
    13,  # R_PPC_REL14_BRNTAKEN
    26,  # R_PPC_REL32
    249,  # R_PPC_REL16
    250,  # R_PPC_REL16_LO
    251,  # R_PPC_REL16_HI
    252,  # R_PPC_REL16_HA
}


def fail(message):
    sys.stderr.write('rtreloc: %s\n' % message)
    sys.exit(1)


def sections(elf):
    if elf[:4] != b'\x7fELF' or elf[4] != 1 or elf[5] != 2:
        fail('not a 32-bit big-endian ELF')
    shoff, = struct.unpack_from('>I', elf, 0x20)
    shentsize, shnum, shstrndx = struct.unpack_from('>HHH', elf, 0x2E)
    out = []
    for i in range(shnum):
        out.append(struct.unpack_from('>IIIIIIIIII', elf, shoff + i * shentsize))
    names = out[shstrndx]
    result = []
    for s in out:
        end = elf.index(b'\0', names[4] + s[0])
        result.append((elf[names[4] + s[0]:end].decode(), s))
    return result


def apply(blob, relocs, base):
    out = bytearray(blob)
    for offset, kind, target in relocs:
        address = (base + target) & 0xFFFFFFFF
        if kind == R_PPC_ADDR32:
            struct.pack_into('>I', out, offset, address)
        elif kind == R_PPC_ADDR16_LO:
            struct.pack_into('>H', out, offset, address & 0xFFFF)
        elif kind == R_PPC_ADDR16_HI:
            struct.pack_into('>H', out, offset, address >> 16)
        else:
            struct.pack_into('>H', out, offset, ((address + 0x8000) >> 16) & 0xFFFF)
    return bytes(out)


def main():
    if len(sys.argv) != 6:
        fail('usage: rtreloc.py <elf> <blob.bin> <alt.bin> <alt base> <out.rel>')
    elf = open(sys.argv[1], 'rb').read()
    blob = open(sys.argv[2], 'rb').read()
    alt = open(sys.argv[3], 'rb').read()
    alt_base = int(sys.argv[4], 0)
    table = sections(elf)
    text = [s for name, s in table if name == '.text']
    if len(text) != 1 or text[0][3] != 0:
        fail('the ELF must be linked at 0 with one .text')
    symtab = [s for name, s in table if s[1] == 2]
    if len(symtab) != 1:
        fail('no symbol table')
    symtab = symtab[0]
    relocs = []
    for name, s in table:
        if s[1] == 9:
            fail('REL sections are not expected (%s)' % name)
        if s[1] != 4 or table[s[7]][0] != '.text':
            continue  # only relocations applying to .text (the blob)
        for i in range(s[5] // 12):
            r_offset, r_info, r_addend = struct.unpack_from('>IIi', elf, s[4] + i * 12)
            kind = r_info & 0xFF
            if kind in RELATIVE or kind == 0:
                continue
            if kind not in ABSOLUTE:
                fail('relocation type %d at 0x%x cannot be applied by the loader' % (kind, r_offset))
            sym = r_info >> 8
            value, = struct.unpack_from('>I', elf, symtab[4] + sym * 16 + 4)
            shndx, = struct.unpack_from('>H', elf, symtab[4] + sym * 16 + 14)
            if shndx == 0xFFF1:
                continue  # an absolute symbol stays where it is
            target = (value + r_addend) & 0xFFFFFFFF
            place = r_offset  # the field itself, for the 16-bit types too
            if place + (4 if kind == R_PPC_ADDR32 else 2) > len(blob):
                fail('relocation at 0x%x lies outside the blob' % r_offset)
            relocs.append((place, kind, target))
    relocs.sort()
    if apply(blob, relocs, 0) != blob:
        fail('the relocations do not describe the blob as linked')
    if len(alt) != len(blob) or apply(blob, relocs, alt_base) != alt:
        fail('the relocations do not reproduce the blob linked at 0x%x' % alt_base)
    with open(sys.argv[5], 'wb') as f:
        for r in relocs:
            f.write(struct.pack('>III', *r))
    print('runtime relocations: %d' % len(relocs))


main()
