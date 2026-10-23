#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes RiftWii's menu font from M PLUS Rounded 1c Bold, in two parts.

The full font (Google Fonts, ofl/mplusrounded1c/MPLUSRounded1c-Bold.ttf,
SIL Open Font License 1.1) is 3.5 MB. Three steps:

  cut    the characters RiftWii's menu shows, kept as wii/font/rounded.ttf
         (4,012 of them):
           - Latin with the accents of the menu's languages (Latin-1, Latin
             Extended-A) and common punctuation;
           - JIS X 0208's symbols, kana and first-level kanji (the everyday
             ones), and half-width kana;
           - every other character in the files given with --text (the
             Japanese translation, GameTDB's Japanese game names).
  round  every outline point of the simple glyphs moved to a multiple of 8
         font units (1000 to the em: 0.2 px at 24 px), the advance widths
         kept and the bounds recomputed. It looks the same at 18 to 60 px
         and packs about a third smaller. Not 16 units: the letter spacing
         shifts at 24 px. `cut` rounds too.
  core   the small part the DOL carries (wii/font/core.ttf, zstd in the
         start-up stash): tools/menu_font_chars.py says which characters.

wii/font/rounded.ttf itself, rounded, ships next to the DOL as
sd:/apps/riftwii/font.ttf (tools/make_release_zip.py), and the menu reads
a character the core lacks from there (wii/menu/fullfont.cpp). Run `core`
again whenever a translation brings a character the core lacks;
tools/lang_source.py and the menufont host test say when.

Needs fontTools (pip install fonttools).

    python tools/make_menu_font.py cut MPLUSRounded1c-Bold.ttf wii/font/rounded.ttf \\
        --text wii/lang/ja.po --text wiitdb-ja.txt
    python tools/make_menu_font.py round wii/font/rounded.ttf wii/font/rounded.ttf
    python tools/make_menu_font.py core wii/font/rounded.ttf wii/font/core.ttf
"""

import argparse
import math

import menu_font_chars

# A multiple of 8 font units: 16 moves letters at 24 px.
GRID = 8


def jis_chars(lead_bytes):
    out = set()
    for lead in lead_bytes:
        for trail in list(range(0x40, 0x7F)) + list(range(0x80, 0xFD)):
            try:
                out.add(bytes([lead, trail]).decode("shift_jis"))
            except UnicodeDecodeError:
                pass
    return out


def subset_font(font, unicodes):
    from fontTools import subset

    options = subset.Options()
    options.layout_features = ["kern", "palt"]
    options.name_IDs = ["*"]
    options.notdef_outline = True
    options.hinting = False
    options.desubroutinize = True
    sub = subset.Subsetter(options)
    sub.populate(unicodes=unicodes)
    sub.subset(font)


def snap(value):
    """`value` to the nearest multiple of GRID, halves away from zero, so
    a glyph and its mirror image stay mirrored."""
    steps = math.floor(abs(value) / GRID + 0.5)
    return int(math.copysign(steps, value)) * GRID


def round_outlines(font):
    """Moves every point of each simple glyph onto the grid. The advance
    widths stay as they were. The left side bearing follows the new xMin,
    so the outline stays where it was drawn, and the vertical metrics' top
    side bearing follows the new yMax. Returns the glyphs rounded."""
    glyf = font["glyf"]
    hmtx = font["hmtx"]
    vmtx = font["vmtx"] if "vmtx" in font else None
    rounded = 0
    for name in font.getGlyphOrder():
        glyph = glyf[name]
        if glyph.isComposite() or glyph.numberOfContours <= 0:
            continue
        old_y_max = glyph.yMax
        coordinates = glyph.coordinates
        for i in range(len(coordinates)):
            x, y = coordinates[i]
            coordinates[i] = (snap(x), snap(y))
        glyph.recalcBounds(glyf)
        advance, _ = hmtx[name]
        hmtx[name] = (advance, glyph.xMin)
        if vmtx is not None:
            vertical_advance, top_bearing = vmtx[name]
            vmtx[name] = (vertical_advance, top_bearing + old_y_max - glyph.yMax)
        rounded += 1
    return rounded


def cut(args):
    from fontTools.ttLib import TTFont

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

    font = TTFont(args.font, recalcTimestamp=False)
    cmap = font.getBestCmap()
    unicodes = sorted(ord(c) for c in chars if ord(c) in cmap)
    subset_font(font, unicodes)
    round_outlines(font)
    font.save(args.out)
    print("%d characters kept, rounded to %d units, written to %s" % (len(unicodes), GRID, args.out))


def round_only(args):
    from fontTools.ttLib import TTFont

    font = TTFont(args.font, recalcTimestamp=False)
    count = round_outlines(font)
    font.save(args.out)
    print("%d glyphs rounded to %d units, written to %s" % (count, GRID, args.out))


def core(args):
    from fontTools.ttLib import TTFont

    font = TTFont(args.font, recalcTimestamp=False)
    unicodes = menu_font_chars.core_codepoints(set(font.getBestCmap()))
    subset_font(font, unicodes)
    # Rounded already when cut from the rounded full font; once more in
    # case it was not, so the core's glyphs never differ from the full's.
    round_outlines(font)
    font.save(args.out)
    print("%d characters in the core, written to %s" % (len(unicodes), args.out))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    steps = ap.add_subparsers(dest="step", required=True)
    cut_step = steps.add_parser("cut", help="the menu's characters from Google's font, rounded")
    cut_step.add_argument("font")
    cut_step.add_argument("out")
    cut_step.add_argument("--text", action="append", default=[], help="a UTF-8 file whose characters are kept")
    for name, what in (("round", "rounds a font's outlines"), ("core", "the core font for the DOL")):
        step = steps.add_parser(name, help=what)
        step.add_argument("font")
        step.add_argument("out")
    args = ap.parse_args()
    {"cut": cut, "round": round_only, "core": core}[args.step](args)


if __name__ == "__main__":
    main()
