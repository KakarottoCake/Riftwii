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

Needs fontTools (pip install fonttools).

    python tools/make_menu_font.py MPLUSRounded1c-Bold.ttf wii/assets/rounded.ttf \
        --text wii/lang/ja.po --text wiitdb-ja.txt
"""

import argparse

from fontTools import subset
from fontTools.ttLib import TTFont


def jis_chars(lead_bytes):
    out = set()
    for lead in lead_bytes:
        for trail in list(range(0x40, 0x7F)) + list(range(0x80, 0xFD)):
            try:
                out.add(bytes([lead, trail]).decode("shift_jis"))
            except UnicodeDecodeError:
                pass
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("font")
    ap.add_argument("out")
    ap.add_argument("--text", action="append", default=[], help="a UTF-8 file whose characters are kept")
    args = ap.parse_args()

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


if __name__ == "__main__":
    main()
