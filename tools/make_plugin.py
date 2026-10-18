#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""RiftWii plugin files (.rwp, docs/PLUGINS.md): built, checked and, for
tests, varied.

    make_plugin.py build --elf <plugin.elf> --alt-elf <alt.elf> --alt-base 0x12340000
                         --name <name> --stamp <stamp> --out <file.rwp>
        The plugin linked at 0 (with --emit-relocs) and the same objects
        linked at the other base become one .rwp: the 128-byte header
        (include/riftwii/pluginfile.hpp has the layout), the image and its
        relocations. Refused, with the reason, when the link has an
        undefined symbol, a section a plugin may not have (writable data,
        constructors, exception tables, small data, a GOT), a relocation the
        loader cannot apply, an entry point missing, or more memory than
        the plugin arena (RWP_ARENA_BYTES); and when the relocations applied
        for the other base do not give exactly the other link (the two-base
        check of Makefile.runtime, with tools/rtreloc.py's relocation list).

    make_plugin.py check --dol <riftwii.dol> [--stamp <stamp>] [--version <version>] [--commit <hash>] <file.rwp>...
        The version match (the DOL's build and CI): every file is named
        <name>.<version>.rwp, whole (its SHA-256), built for this
        RWP_API_VERSION and with the DOL's build stamp. Without --stamp the
        stamp is read from the DOL; --commit also requires it to name that
        commit (CI passes the checkout's).

    make_plugin.py variant <in.rwp> <out.rwp> [--name N] [--api N] [--stamp S] [--damage]
        A copy with one thing changed and the hash written again, so only
        the intended check fails: for the loader's refusal tests (Dolphin
        runs, docs/DEVELOPING.md). --damage changes one image byte and keeps
        the old hash.

    make_plugin.py info <file.rwp>
        The header, as text.
"""
import argparse
import hashlib
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
API_HEADER = os.path.join(ROOT, "include", "riftwii", "plugin_api.h")

HEADER_BYTES = 128
HEADER_VERSION = 1
NAME_BYTES = 16
STAMP_BYTES = 32
HASH_OFFSET = 96
NAME = re.compile(r"^[A-Za-z0-9_-]{1,16}$")
STAMP = re.compile(r"^[A-Za-z0-9_-]{1,32}$")
# The marker the loader keeps in the DOL (wii/core/plugins.cpp), followed
# by the build stamp and a NUL.
DOL_MARKER = b"RiftWii plugin build stamp: "

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
# Allocated sections a plugin may have. .data must be empty: writable data
# lives in the state the plugin asks for or in .bss.
ALLOWED = {".text", ".rodata", ".data", ".bss"}
# Where plugins/plugin.ld keeps what a plugin may not have, so it is
# refused here by name instead of dropped by the link without a word.
KEPT_TO_REFUSE = {
    ".rwp_constructors": "static constructors or destructors (.ctors, .dtors, .init_array, .fini_array): "
                         "nothing runs them, so a global with a constructor would never be built; "
                         "make the global constant, or set it up in rwp_init",
    ".rwp_exceptions": "exception or unwind tables (.eh_frame, .gcc_except_table): plugins have no unwinder, "
                       "so a throw would end RiftWii; build with -fno-exceptions -fno-asynchronous-unwind-tables",
}
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_RELA = 4
SHT_NOBITS = 8
SHT_REL = 9
SHF_ALLOC = 2
SHN_UNDEF = 0
SHN_ABS = 0xFFF1


def fail(message):
    sys.exit("make_plugin: " + message)


def api_numbers():
    """RWP_API_VERSION and RWP_ARENA_BYTES from the one header the loader
    is built with, so the two cannot drift."""
    text = open(API_HEADER, encoding="utf-8").read()
    version = re.search(r"#define RWP_API_VERSION (\d+)", text)
    arena = re.search(r"#define RWP_ARENA_BYTES \((\d+)u \* (\d+)u\)", text)
    if not version or not arena:
        fail("cannot read RWP_API_VERSION and RWP_ARENA_BYTES from " + API_HEADER)
    return int(version.group(1)), int(arena.group(1)) * int(arena.group(2))


class Elf:
    def __init__(self, path):
        self.path = path
        self.data = open(path, "rb").read()
        d = self.data
        if d[:4] != b"\x7fELF" or d[4] != 1 or d[5] != 2:
            fail("%s: not a 32-bit big-endian ELF" % path)
        shoff, = struct.unpack_from(">I", d, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from(">HHH", d, 0x2E)
        raw = [struct.unpack_from(">IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
        names = raw[shstrndx]
        self.sections = []
        for s in raw:
            start = names[4] + s[0]
            self.sections.append((d[start:d.index(b"\0", start)].decode(), s))

    def symbols(self):
        """(name, value, size, info, section index) of every symbol."""
        out = []
        for _, s in self.sections:
            if s[1] != SHT_SYMTAB:
                continue
            strtab = self.sections[s[6]][1]
            for i in range(s[5] // 16):
                name, value, size, info, _, shndx = struct.unpack_from(">IIIBBH", self.data, s[4] + i * 16)
                start = strtab[4] + name
                text = self.data[start:self.data.index(b"\0", start)].decode()
                out.append((text, value, size, info, shndx))
        return out

    def symbol(self, wanted):
        for name, value, _, info, shndx in self.symbols():
            if name == wanted and shndx not in (SHN_UNDEF,):
                return value
        return None

    def layout(self, base):
        """The image (every allocated PROGBITS section at its address less
        `base`) and the .bss size after it."""
        image_end = 0
        bss_end = 0
        progbits = []
        for name, s in self.sections:
            sh_type, flags, addr, offset, size = s[1], s[2], s[3], s[4], s[5]
            if not flags & SHF_ALLOC or size == 0:
                continue
            if name in KEPT_TO_REFUSE:
                fail("%s: %d bytes of %s; docs/PLUGINS.md" % (self.path, size, KEPT_TO_REFUSE[name]))
            if name not in ALLOWED:
                fail("%s: section %s is not allowed in a plugin (no constructors, exception tables, "
                     "small data or GOT; docs/PLUGINS.md)" % (self.path, name))
            if name == ".data":
                fail("%s: .data holds %d bytes; a plugin keeps writable data in .bss or its state" % (self.path, size))
            if addr < base:
                fail("%s: section %s at 0x%x is below the link base 0x%x" % (self.path, name, addr, base))
            if sh_type == SHT_NOBITS:
                bss_end = max(bss_end, addr - base + size)
            elif sh_type == SHT_PROGBITS:
                progbits.append((addr - base, self.data[offset:offset + size]))
                image_end = max(image_end, addr - base + size)
        image_bytes = (image_end + 31) & ~31
        image = bytearray(image_bytes)
        for at, chunk in progbits:
            image[at:at + len(chunk)] = chunk
        bss_bytes = max(0, bss_end - image_bytes)
        return bytes(image), bss_bytes

    def relocations(self, image_bytes):
        symtab = [s for _, s in self.sections if s[1] == SHT_SYMTAB]
        if len(symtab) != 1:
            fail("%s: no symbol table" % self.path)
        symtab = symtab[0]
        out = []
        for name, s in self.sections:
            if s[1] == SHT_REL:
                fail("%s: REL sections are not expected (%s)" % (self.path, name))
            if s[1] != SHT_RELA:
                continue
            target_name, target = self.sections[s[7]]
            if not target[2] & SHF_ALLOC:
                continue  # relocations of debug sections
            for i in range(s[5] // 12):
                r_offset, r_info, r_addend = struct.unpack_from(">IIi", self.data, s[4] + i * 12)
                kind = r_info & 0xFF
                if kind in RELATIVE or kind == 0:
                    continue
                if kind not in ABSOLUTE:
                    fail("%s: relocation type %d at 0x%x (%s) cannot be applied by the loader"
                         % (self.path, kind, r_offset, target_name))
                sym = r_info >> 8
                value, = struct.unpack_from(">I", self.data, symtab[4] + sym * 16 + 4)
                shndx, = struct.unpack_from(">H", self.data, symtab[4] + sym * 16 + 14)
                if shndx == SHN_ABS:
                    continue  # an absolute symbol stays where it is
                if shndx == SHN_UNDEF:
                    fail("%s: relocation at 0x%x refers to an undefined symbol" % (self.path, r_offset))
                width = 4 if kind == R_PPC_ADDR32 else 2
                if r_offset + width > image_bytes:
                    fail("%s: relocation at 0x%x lies outside the image" % (self.path, r_offset))
                out.append((r_offset, kind, (value + r_addend) & 0xFFFFFFFF))
        out.sort()
        return out


def apply(image, relocs, base):
    out = bytearray(image)
    for offset, kind, target in relocs:
        address = (base + target) & 0xFFFFFFFF
        if kind == R_PPC_ADDR32:
            struct.pack_into(">I", out, offset, address)
        elif kind == R_PPC_ADDR16_LO:
            struct.pack_into(">H", out, offset, address & 0xFFFF)
        elif kind == R_PPC_ADDR16_HI:
            struct.pack_into(">H", out, offset, address >> 16)
        else:
            struct.pack_into(">H", out, offset, ((address + 0x8000) >> 16) & 0xFFFF)
    return bytes(out)


def align32(value):
    return (value + 31) & ~31


def pack_header(fields):
    header = bytearray(HEADER_BYTES)
    header[0:4] = b"RWPL"
    header[4] = HEADER_VERSION
    header[8:8 + len(fields["name"])] = fields["name"].encode()
    struct.pack_into(">II", header, 24, fields["plugin_version"], fields["api_version"])
    header[32:32 + len(fields["stamp"])] = fields["stamp"].encode()
    struct.pack_into(">IIIIIIII", header, 64, fields["init"], fields["shutdown"], fields["table"],
                     fields["image_bytes"], fields["bss_bytes"], fields["reloc_count"], fields["state_bytes"],
                     fields["flags"])
    return header


def seal(header, rest):
    """The header with its SHA-256 written: of the whole file, this field zero."""
    header = bytearray(header)
    header[HASH_OFFSET:HASH_OFFSET + 32] = bytes(32)
    header[HASH_OFFSET:HASH_OFFSET + 32] = hashlib.sha256(bytes(header) + rest).digest()
    return bytes(header)


def parse_header(data, path):
    if len(data) < HEADER_BYTES or data[:4] != b"RWPL" or data[4] != HEADER_VERSION:
        fail("%s: not a plugin file" % path)
    fields = {
        "name": data[8:8 + NAME_BYTES].rstrip(b"\0").decode("ascii", "replace"),
        "stamp": data[32:32 + STAMP_BYTES].rstrip(b"\0").decode("ascii", "replace"),
    }
    fields["plugin_version"], fields["api_version"] = struct.unpack_from(">II", data, 24)
    (fields["init"], fields["shutdown"], fields["table"], fields["image_bytes"], fields["bss_bytes"],
     fields["reloc_count"], fields["state_bytes"], fields["flags"]) = struct.unpack_from(">IIIIIIII", data, 64)
    fields["hash"] = data[HASH_OFFSET:HASH_OFFSET + 32]
    return fields


def hash_ok(data):
    return seal(data[:HEADER_BYTES], data[HEADER_BYTES:])[HASH_OFFSET:HASH_OFFSET + 32] == \
        data[HASH_OFFSET:HASH_OFFSET + 32]


def build(args):
    api_version, arena_bytes = api_numbers()
    if not NAME.match(args.name):
        fail("plugin name %r: 1 to 16 of A-Za-z0-9_-" % args.name)
    if not STAMP.match(args.stamp):
        fail("build stamp %r: 1 to 32 of A-Za-z0-9_-" % args.stamp)
    elf = Elf(args.elf)
    alt = Elf(args.alt_elf)
    alt_base = int(args.alt_base, 0)

    undefined = sorted({name for name, _, _, _, shndx in elf.symbols() if shndx == SHN_UNDEF and name})
    if undefined:
        fail("%s: undefined symbols (a plugin calls RiftWii only through rwp_host): %s"
             % (args.elf, ", ".join(undefined)))

    image, bss_bytes = elf.layout(0)
    alt_image, alt_bss = alt.layout(alt_base)
    relocs = elf.relocations(len(image))
    if apply(image, relocs, 0) != image:
        fail("the relocations do not describe the plugin as linked")
    if len(alt_image) != len(image) or alt_bss != bss_bytes or apply(image, relocs, alt_base) != alt_image:
        fail("the relocations do not reproduce the plugin linked at 0x%x: it is not relocatable" % alt_base)

    entries = {}
    for symbol in ("rwp_init", "rwp_exports"):
        value = elf.symbol(symbol)
        if value is None:
            fail("%s: no %s (every plugin has rwp_init and rwp_exports)" % (args.elf, symbol))
        entries[symbol] = value
    shutdown_symbol = elf.symbol("rwp_shutdown")
    shutdown = shutdown_symbol or 0

    # Offset 0 of the image is reserved (plugins/plugin.ld puts a zero word
    # there): the header's shutdown offset 0 means "none", and as linked at
    # 0 a table pointer to offset 0 reads as NULL, so code there could not
    # be named. The loader would refuse the plugin at every load.
    if entries["rwp_init"] == 0:
        fail("%s: rwp_init is at image offset 0, which is reserved; link with plugins/plugin.ld" % args.elf)
    if shutdown_symbol == 0:
        fail("%s: rwp_shutdown is at image offset 0, which the header reads as no shutdown; "
             "link with plugins/plugin.ld, which reserves that word" % args.elf)

    # The plugin's own table, as linked at 0: size, API version, version,
    # flags, probe, shutdown, launch.
    table = entries["rwp_exports"]
    size, table_api, plugin_version, flags = struct.unpack_from(">IIII", image, table)
    if table_api != api_version:
        fail("rwp_exports says API %d, include/riftwii/plugin_api.h says %d: rebuild" % (table_api, api_version))
    table_shutdown, = struct.unpack_from(">I", image, table + 20)
    if size >= 24 and table_shutdown != shutdown:
        fail("rwp_exports.shutdown (0x%x) is not rwp_shutdown (0x%x)" % (table_shutdown, shutdown))
    # A pointer in the table is a relocated word; one that holds 0 as linked
    # at 0 points at offset 0 (a NULL has no relocation).
    pointed = {offset for offset, _, _ in relocs}
    for name, at in (("probe", 16), ("shutdown", 20), ("launch", 24)):
        if at + 4 > size:
            continue
        value, = struct.unpack_from(">I", image, table + at)
        if value == 0 and table + at in pointed:
            fail("%s: rwp_exports.%s points at image offset 0, which is reserved and never code; "
                 "link with plugins/plugin.ld" % (args.elf, name))

    state_bytes = int(args.state, 0)
    memory = align32(len(image)) + align32(bss_bytes) + align32(state_bytes)
    if memory > arena_bytes:
        fail("%s needs %d bytes (image %d, .bss %d, state %d), more than the plugin arena's %d"
             % (args.name, memory, len(image), bss_bytes, state_bytes, arena_bytes))

    fields = {
        "name": args.name, "plugin_version": plugin_version, "api_version": api_version, "stamp": args.stamp,
        "init": entries["rwp_init"], "shutdown": shutdown, "table": table, "image_bytes": len(image),
        "bss_bytes": bss_bytes, "reloc_count": len(relocs), "state_bytes": state_bytes, "flags": flags,
    }
    rest = image + b"".join(struct.pack(">III", *r) for r in relocs)
    data = seal(pack_header(fields), rest) + rest
    with open(args.out, "wb") as out:
        out.write(data)
    print("plugin %s: %d bytes (image %d, .bss %d, %d relocations), %d of the arena's %d bytes, API %d, %s"
          % (args.name, len(data), len(image), bss_bytes, len(relocs), memory, arena_bytes, api_version, args.stamp))


def check(args):
    api_version, arena_bytes = api_numbers()
    dol = open(args.dol, "rb").read()
    found = re.search(re.escape(DOL_MARKER) + rb"([A-Za-z0-9_-]+)\0", dol)
    dol_stamp = found.group(1).decode() if found else None
    if dol_stamp is None:
        fail("%s carries no plugin build stamp (wii/core/plugins.cpp's marker)" % args.dol)
    # Without --stamp (CI): the DOL's own, which must then name this commit.
    if args.stamp is None:
        args.stamp = dol_stamp
    if dol_stamp != args.stamp:
        fail("%s carries build stamp %s, not %s" % (args.dol, dol_stamp, args.stamp))
    if args.commit is not None and not args.stamp.endswith("-g" + args.commit):
        fail("build stamp %s does not name commit %s" % (args.stamp, args.commit))
    if args.version is None:
        args.version = args.stamp.rsplit("-g", 1)[0]
    if not args.files:
        fail("no plugin files to check")
    for path in args.files:
        data = open(path, "rb").read()
        fields = parse_header(data, path)
        expected = "%s.%s.rwp" % (fields["name"], args.version)
        if os.path.basename(path) != expected:
            fail("%s: should be named %s" % (path, expected))
        if not hash_ok(data):
            fail("%s: the SHA-256 does not match" % path)
        if len(data) != HEADER_BYTES + fields["image_bytes"] + 12 * fields["reloc_count"]:
            fail("%s: the sizes in the header do not add up to the file" % path)
        if fields["api_version"] != api_version:
            fail("%s: API %d, the DOL's is %d" % (path, fields["api_version"], api_version))
        if fields["stamp"] != args.stamp:
            fail("%s: build stamp %s, the DOL's is %s" % (path, fields["stamp"], args.stamp))
        memory = align32(fields["image_bytes"]) + align32(fields["bss_bytes"]) + align32(fields["state_bytes"])
        if memory > arena_bytes:
            fail("%s: needs %d bytes of the arena's %d" % (path, memory, arena_bytes))
        print("%s: matches the DOL (%s, API %d), %d of the arena's %d bytes"
              % (os.path.basename(path), args.stamp, api_version, memory, arena_bytes))


def variant(args):
    data = bytearray(open(args.input, "rb").read())
    fields = parse_header(bytes(data), args.input)
    if args.damage:
        # One image byte changed, the old hash kept: the hash check fails.
        data[HEADER_BYTES + fields["image_bytes"] // 2] ^= 0xFF
        open(args.output, "wb").write(bytes(data))
        return
    if args.name is not None:
        if not NAME.match(args.name):
            fail("plugin name %r: 1 to 16 of A-Za-z0-9_-" % args.name)
        fields["name"] = args.name
    if args.api is not None:
        fields["api_version"] = args.api
    if args.stamp is not None:
        if not STAMP.match(args.stamp):
            fail("build stamp %r: 1 to 32 of A-Za-z0-9_-" % args.stamp)
        fields["stamp"] = args.stamp
    rest = bytes(data[HEADER_BYTES:])
    open(args.output, "wb").write(seal(pack_header(fields), rest) + rest)


def info(args):
    data = open(args.file, "rb").read()
    fields = parse_header(data, args.file)
    for key in ("name", "plugin_version", "api_version", "stamp", "init", "shutdown", "table", "image_bytes",
                "bss_bytes", "reloc_count", "state_bytes", "flags"):
        value = fields[key]
        print("%-15s %s" % (key, ("0x%x" % value) if key in ("init", "shutdown", "table") else value))
    print("%-15s %s (%s)" % ("sha256", fields["hash"].hex(), "matches" if hash_ok(data) else "DOES NOT MATCH"))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("build")
    p.add_argument("--elf", required=True)
    p.add_argument("--alt-elf", required=True)
    p.add_argument("--alt-base", required=True)
    p.add_argument("--name", required=True)
    p.add_argument("--stamp", required=True)
    p.add_argument("--state", default="0")
    p.add_argument("--out", required=True)
    p = sub.add_parser("check")
    p.add_argument("--dol", required=True)
    p.add_argument("--stamp")
    p.add_argument("--version")
    p.add_argument("--commit")
    p.add_argument("files", nargs="*")
    p = sub.add_parser("variant")
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--name")
    p.add_argument("--api", type=int)
    p.add_argument("--stamp")
    p.add_argument("--damage", action="store_true")
    p = sub.add_parser("info")
    p.add_argument("file")
    args = parser.parse_args()
    {"build": build, "check": check, "variant": variant, "info": info}[args.command](args)


if __name__ == "__main__":
    main()
