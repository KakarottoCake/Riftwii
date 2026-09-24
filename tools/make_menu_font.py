#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cuts M PLUS Rounded 1c Bold down to the characters RiftWii's menu shows.

The full font (Google Fonts, ofl/mplusrounded1c/MPLUSRounded1c-Bold.ttf,
SIL Open Font License 1.1) is 3.5 MB; the menu keeps:
  - Latin with the accents of the menu's languages (Latin-1, Latin
    Extended-A) and common punctuation;
  - JIS X 0208's symbols, kana and first-level kanji (the everyday ones),
    and half-width kana;
  - every other character in the files given with --text (the Japanese
    translation, GameTDB's Japanese game names), so those always show.

The subset is kept as wii/font/rounded.ttf; the menu embeds it brotli-
compressed as wii/assets/menufont.bin (a big-endian u32 of the TTF's size,
then the brotli stream), which main.cpp unpacks into MEM2 at start.

Needs fontTools and brotli (pip install fonttools brotli).

    python tools/make_menu_font.py MPLUSRounded1c-Bold.ttf wii/font/rounded.ttf \
        --packed wii/assets/menufont.bin --text wii/lang/ja.po --text wiitdb-ja.txt
    python tools/make_menu_font.py wii/font/rounded.ttf wii/assets/menufont.bin --pack-only
"""

import argparse
import struct


def jis_chars(lead_bytes):
    out = set()
    for lead in lead_bytes:
        for trail in list(range(0x40, 0x7F)) + list(range(0x80, 0xFD)):
            try:
                out.add(bytes([lead, trail]).decode("shift_jis"))
            except UnicodeDecodeError:
                pass
    return out


def pack(ttf_path, out_path):
    import brotli

    data = open(ttf_path, "rb").read()
    packed = brotli.compress(data, quality=11, lgwin=22)
    assert brotli.decompress(packed) == data
    with open(out_path, "wb") as f:
        f.write(struct.pack(">I", len(data)))
        f.write(packed)
    print("%s: %d bytes packed to %d in %s" % (ttf_path, len(data), len(packed) + 4, out_path))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("font")
    ap.add_argument("out")
    ap.add_argument("--text", action="append", default=[], help="a UTF-8 file whose characters are kept")
    ap.add_argument("--packed", help="also write the menu's compressed copy here")
    ap.add_argument("--pack-only", action="store_true", help="font is an existing subset: only write out, packed")
    args = ap.parse_args()
    if args.pack_only:
        pack(args.font, args.out)
        return

    ranges = [(0x20, 0x7E), (0xA0, 0x17F), (0x2010, 0x2027), (0x2030, 0x203A), (0x2122, 0x2122),
              (0x2190, 0x2193), (0x3000, 0x30FF), (0xFF01, 0xFF9F)]
    chars = set()
    for lo, hi in ranges:
        chars.update(chr(c) for c in range(lo, hi + 1))
    # Shift JIS lead bytes 0x81-0x84: symbols, full-width letters, kana,
    # Greek, Cyrillic; 0x88-0x98: the first-level kanji.
    chars |= jis_chars(list(range(0x81, 0x85)) + list(range(0x88, 0x99)))
    for path in args.text:
        with open(path, encoding="utf-8", errors="ignore") as f:
            chars.update(f.read())

    from fontTools import subset
    from fontTools.ttLib import TTFont

    font = TTFont(args.font)
    cmap = font.getBestCmap()
    unicodes = sorted(ord(c) for c in chars if ord(c) in cmap)

    options = subset.Options()
    options.layout_features = ["kern", "palt"]
    options.name_IDs = ["*"]
    options.notdef_outline = True
    options.hinting = False
    options.desubroutinize = True
    sub = subset.Subsetter(options)
    sub.populate(unicodes=unicodes)
    sub.subset(font)
    font.save(args.out)
    print("%d characters kept, written to %s" % (len(unicodes), args.out))
    if args.packed:
        pack(args.out, args.packed)


if __name__ == "__main__":
    main()
