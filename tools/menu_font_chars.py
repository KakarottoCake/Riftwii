#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Which characters the menu's core font holds, and what a TrueType font
maps, with the standard library alone (the CI translation check has no
fontTools).

The menu font comes in two parts (issue #242, item 4):
  - wii/font/core.ttf, in the DOL's start-up stash: everything below
    U+3000 (Latin, punctuation, symbols), U+3000-U+30FF (CJK punctuation
    and kana), U+FF00-U+FFEF (half- and full-width forms), and every
    character of wii/lang/*.po and of the menu's own sources;
  - wii/font/rounded.ttf, all of it, shipped as sd:/apps/riftwii/font.ttf
    and read by the menu only for a character the core lacks
    (wii/menu/fullfont.cpp).

    python tools/menu_font_chars.py    # checks wii/font/core.ttf
"""

import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
CORE_FONT = os.path.join(REPO, "wii", "font", "core.ttf")
FULL_FONT = os.path.join(REPO, "wii", "font", "rounded.ttf")

# Whole blocks kept in the core whatever the text uses, so a game name or
# a pack's text in kana or Latin never needs the SD card.
CORE_RANGES = [(0x0000, 0x2FFF), (0x3000, 0x30FF), (0xFF00, 0xFFEF)]


def text_characters(repo=REPO):
    """Every character of the translations (wii/lang/*.po, English msgid
    included) and of the menu's and the plugins' sources (text drawn
    without tr(), such as the language names in their own script)."""
    paths = glob.glob(os.path.join(repo, "wii", "lang", "*.po"))
    for pattern in ("*.cpp", "*.hpp", "*.c", "*.h"):
        paths += glob.glob(os.path.join(repo, "wii", "**", pattern), recursive=True)
        paths += glob.glob(os.path.join(repo, "plugins", "**", pattern), recursive=True)
    chars = set()
    for path in sorted(paths):
        with open(path, encoding="utf-8", errors="ignore") as f:
            chars.update(f.read())
    return chars


def in_core_ranges(codepoint):
    return any(low <= codepoint <= high for low, high in CORE_RANGES)


def core_codepoints(full_cmap, repo=REPO):
    """The code points the core keeps: those of the full font in the core
    ranges or in the text."""
    wanted = {ord(c) for c in text_characters(repo)}
    return sorted(cp for cp in full_cmap if in_core_ranges(cp) or cp in wanted)


def read_cmap(path):
    """The code points a TrueType font maps to a glyph (its Unicode cmap
    subtables, formats 4 and 12)."""
    with open(path, "rb") as f:
        data = f.read()
    num_tables = struct.unpack_from(">H", data, 4)[0]
    cmap_offset = None
    for i in range(num_tables):
        tag, _, offset, _ = struct.unpack_from(">4sIII", data, 12 + 16 * i)
        if tag == b"cmap":
            cmap_offset = offset
    if cmap_offset is None:
        raise ValueError("%s has no cmap table" % path)
    codepoints = set()
    count = struct.unpack_from(">H", data, cmap_offset + 2)[0]
    for i in range(count):
        platform, encoding, offset = struct.unpack_from(">HHI", data, cmap_offset + 4 + 8 * i)
        unicode_table = platform == 0 or (platform == 3 and encoding in (1, 10))
        if not unicode_table:
            continue
        table = cmap_offset + offset
        table_format = struct.unpack_from(">H", data, table)[0]
        if table_format == 4:
            codepoints |= read_format4(data, table)
        elif table_format == 12:
            codepoints |= read_format12(data, table)
    return codepoints


def read_format4(data, table):
    segments = struct.unpack_from(">H", data, table + 6)[0] // 2
    ends_at = table + 14
    starts_at = ends_at + 2 * segments + 2
    deltas_at = starts_at + 2 * segments
    range_offsets_at = deltas_at + 2 * segments
    out = set()
    for s in range(segments):
        end = struct.unpack_from(">H", data, ends_at + 2 * s)[0]
        start = struct.unpack_from(">H", data, starts_at + 2 * s)[0]
        delta = struct.unpack_from(">h", data, deltas_at + 2 * s)[0]
        range_offset = struct.unpack_from(">H", data, range_offsets_at + 2 * s)[0]
        for cp in range(start, end + 1):
            if cp == 0xFFFF:
                continue
            if range_offset == 0:
                glyph = (cp + delta) & 0xFFFF
            else:
                at = range_offsets_at + 2 * s + range_offset + 2 * (cp - start)
                glyph = struct.unpack_from(">H", data, at)[0]
                if glyph != 0:
                    glyph = (glyph + delta) & 0xFFFF
            if glyph != 0:
                out.add(cp)
    return out


def read_format12(data, table):
    groups = struct.unpack_from(">I", data, table + 12)[0]
    out = set()
    for g in range(groups):
        start, end, first_glyph = struct.unpack_from(">III", data, table + 16 + 12 * g)
        for cp in range(start, end + 1):
            if first_glyph + (cp - start) != 0:
                out.add(cp)
    return out


def missing_from_core(core_path=CORE_FONT, full_path=FULL_FONT, repo=REPO):
    """The text's characters that the full font has and the core lacks
    (sorted). Empty when the core is up to date."""
    core = read_cmap(core_path)
    full = read_cmap(full_path)
    return sorted(cp for cp in core_codepoints(full, repo) if cp not in core)


def japanese_missing_from_core(core_path=CORE_FONT, repo=REPO):
    """The characters of the Japanese msgstr values that the core font lacks
    (sorted). Unlike missing_from_core() this does not ask the full font
    first: a Japanese character in neither font would draw blank on the
    Wii, and that must fail here rather than pass unnoticed. (Korean is
    not checked: Hangul is in neither font and comes from the Wii Menu's.)"""
    core = read_cmap(core_path)
    shown = set()
    with open(os.path.join(repo, "wii", "lang", "ja.po"), encoding="utf-8") as f:
        for line in f:
            if line.startswith('msgstr "'):
                shown.update(line[len('msgstr "'):].rstrip("\n").rstrip('"'))
    return sorted(ord(c) for c in shown if ord(c) not in core and c not in " \t")


def main():
    missing = missing_from_core()
    if missing:
        print("wii/font/core.ttf lacks %d character(s) the menu's text uses: %s" %
              (len(missing), "".join(chr(cp) for cp in missing[:40])))
        print("Run: python tools/make_menu_font.py core wii/font/rounded.ttf wii/font/core.ttf")
        sys.exit(1)
    japanese = japanese_missing_from_core()
    if japanese:
        print("Japanese text uses %d character(s) in neither font: %s" %
              (len(japanese), "".join(chr(cp) for cp in japanese[:40])))
        sys.exit(1)
    print("wii/font/core.ttf holds every character of the menu's text")


if __name__ == "__main__":
    main()
