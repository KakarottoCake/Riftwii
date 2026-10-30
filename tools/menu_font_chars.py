#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks the complete card font's Japanese menu coverage without fontTools.

wii/font/menu.ttf ships as apps/riftwii/font.ttf and stays in memory during
unmounts. Korean uses its own korean.ttf. No font is embedded in the DOL.
"""

import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
FULL_FONT = os.path.join(REPO, "wii", "font", "menu.ttf")
KOREAN_FONT = os.path.join(REPO, "wii", "font", "korean.ttf")


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


def japanese_missing_characters(font_path=FULL_FONT, repo=REPO):
    """Every Japanese msgstr character must exist, including new translations."""
    font = read_cmap(font_path)
    shown = set()
    with open(os.path.join(repo, "wii", "lang", "ja.po"), encoding="utf-8") as f:
        for line in f:
            if line.startswith('msgstr "'):
                shown.update(line[len('msgstr "'):].rstrip("\n").rstrip('"'))
    return sorted(ord(c) for c in shown if ord(c) not in font and c not in " \t")


def korean_missing_characters(repo=REPO):
    """Printable ko.po characters, all 2,350 common syllables and Jamo."""
    font = read_cmap(os.path.join(repo, "wii", "font", "korean.ttf"))
    with open(os.path.join(repo, "wii", "lang", "ko.po"), encoding="utf-8") as source:
        wanted = {ord(char) for char in source.read() if char.isprintable()}
    for lead in range(0xB0, 0xC9):
        for trail in range(0xA1, 0xFF):
            wanted.add(ord(bytes((lead, trail)).decode("euc_kr")))
    for low, high in ((0x1100, 0x11FF), (0x3131, 0x318E), (0xA960, 0xA97C),
                      (0xD7B0, 0xD7C6), (0xD7CB, 0xD7FB)):
        wanted.update(range(low, high + 1))
    with open(os.path.join(repo, "tools", "korean_title_chars.txt"), encoding="utf-8") as source:
        for line in source:
            if line.strip() and not line.startswith("#"):
                wanted.update(int(point, 16) for point in line.split())
    return sorted(wanted - font)


def main():
    japanese = japanese_missing_characters()
    if japanese:
        print("Japanese text uses %d character(s) missing from wii/font/menu.ttf: %s" %
              (len(japanese), "".join(chr(cp) for cp in japanese[:40])))
        sys.exit(1)
    korean = korean_missing_characters()
    if korean:
        print("wii/font/korean.ttf lacks required characters: " +
              ", ".join("U+%04X" % cp for cp in korean[:40]))
        sys.exit(1)
    print("The menu and Korean fonts cover the menu's text")


if __name__ == "__main__":
    main()
