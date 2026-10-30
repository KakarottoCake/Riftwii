#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes the card's complete menu font from Google Fonts' M PLUS 1 variable font.

Source: ofl/mplus1/MPLUS1[wght].ttf at google/fonts commit
e20f9bdde5d8538dcef0c95022d1d3d12acbc453, SIL Open Font License 1.1.
`cut` first makes a static SemiBold (weight 600) instance, then subsets and
rounds it. No part of this font goes in the DOL.

  cut    the characters RiftWii's menu shows, kept as wii/font/menu.ttf:
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
The default subset includes the sources, translations and the extra Japanese
title characters retained in tools/japanese_title_chars.txt. Use --text for
additional GameTDB titles. The whole file ships beside boot.dol as font.ttf
and stays in MEM2 throughout the menu, including every unmount and update.
wii/font/rounded.ttf remains the logo artwork's font and is left unchanged.
Its rare characters absent from M PLUS 1 are retained in the new file too,
so existing titles keep drawing without a second installed font file.

Needs fontTools (pip install fonttools).

    python tools/make_menu_font.py cut 'build-font/MPLUS1[wght].ttf' wii/font/menu.ttf
"""

import argparse
import hashlib
import math
import unicodedata
from pathlib import Path

import menu_font_chars

# A multiple of 8 font units: 16 moves letters at 24 px.
GRID = 8
SOURCE_SHA256 = "32dab296a06c3e87841ecd2bc9912434a964ad541e22ea19f764969dae93fced"


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


def add_halfwidth_kana(font):
    """The source omits half-width kana. Derive them from its own full-width
    outlines, so Japanese titles keep their characters and half-em advances."""
    from fontTools.pens.recordingPen import DecomposingRecordingPen
    from fontTools.pens.transformPen import TransformPen
    from fontTools.pens.ttGlyphPen import TTGlyphPen

    cmap = font.getBestCmap()
    glyph_set = font.getGlyphSet()
    glyph_order = font.getGlyphOrder()[:]
    for codepoint in range(0xFF61, 0xFFA0):
        if codepoint in cmap:
            continue
        normalized = unicodedata.normalize("NFKC", chr(codepoint))
        source_name = cmap[ord(normalized)]
        recording = DecomposingRecordingPen(glyph_set)
        glyph_set[source_name].draw(recording)
        pen = TTGlyphPen(None)
        recording.replay(TransformPen(pen, (0.5, 0, 0, 1, 0, 0)))
        glyph_name = "riftwii%04X" % codepoint
        glyph_order.append(glyph_name)
        glyph = pen.glyph()
        font["glyf"][glyph_name] = glyph
        glyph.recalcBounds(font["glyf"])
        font["hmtx"][glyph_name] = (500, glyph.xMin)
        if "vmtx" in font:
            font["vmtx"][glyph_name] = font["vmtx"][source_name]
        for table in font["cmap"].tables:
            if table.isUnicode():
                table.cmap[codepoint] = glyph_name
    font.setGlyphOrder(glyph_order)


def retain_missing_characters(font, characters):
    """Keep the former font's rare symbols and scripts the new source lacks.
    Copy outlines only; the menu's ordinary text and metrics remain M PLUS 1."""
    from fontTools.pens.recordingPen import DecomposingRecordingPen
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib import TTFont

    legacy_path = Path(menu_font_chars.REPO) / "wii" / "font" / "rounded.ttf"
    legacy = TTFont(legacy_path, recalcTimestamp=False)
    legacy_cmap = legacy.getBestCmap()
    legacy_glyphs = legacy.getGlyphSet()
    cmap = font.getBestCmap()
    glyph_order = font.getGlyphOrder()[:]
    count = 0
    for character in sorted(characters):
        codepoint = ord(character)
        if codepoint in cmap or codepoint not in legacy_cmap:
            continue
        source_name = legacy_cmap[codepoint]
        recording = DecomposingRecordingPen(legacy_glyphs)
        legacy_glyphs[source_name].draw(recording)
        pen = TTGlyphPen(None)
        recording.replay(pen)
        glyph_name = "retained%04X" % codepoint
        glyph_order.append(glyph_name)
        glyph = pen.glyph()
        font["glyf"][glyph_name] = glyph
        glyph.recalcBounds(font["glyf"])
        font["hmtx"][glyph_name] = legacy["hmtx"][source_name]
        if "vmtx" in font:
            font["vmtx"][glyph_name] = legacy["vmtx"][source_name]
        for table in font["cmap"].tables:
            if table.isUnicode():
                table.cmap[codepoint] = glyph_name
        count += 1
    font.setGlyphOrder(glyph_order)
    print("%d rare characters retained from the former font" % count)


def cut(args):
    from fontTools.ttLib import TTFont

    if hashlib.sha256(Path(args.font).read_bytes()).hexdigest() != SOURCE_SHA256:
        raise SystemExit("Use MPLUS1[wght].ttf from the Google Fonts commit pinned above.")

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

    chars |= menu_font_chars.text_characters()
    title_chars = Path(__file__).with_name("japanese_title_chars.txt")
    for line in title_chars.read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            chars.update(chr(int(point, 16)) for point in line.split())

    font = TTFont(args.font, recalcTimestamp=False)
    from fontTools.varLib.instancer import instantiateVariableFont

    font = instantiateVariableFont(font, {"wght": 600}, inplace=True, updateFontNames=True)
    add_halfwidth_kana(font)
    retain_missing_characters(font, chars)
    cmap = font.getBestCmap()
    unicodes = sorted(ord(c) for c in chars if ord(c) in cmap)
    subset_font(font, unicodes)
    round_outlines(font)
    # Existing widgets compensate for Rounded 1c's line box. Keep that box
    # centrally, rather than moving every button, row and tile. The new
    # outlines' cap/x heights (730/520) match the old rounded ones (728/520).
    font["hhea"].ascent = 1075
    font["hhea"].descent = -320
    font["hhea"].lineGap = 90
    font.save(args.out)
    print("%d characters kept, rounded to %d units, written to %s" % (len(unicodes), GRID, args.out))


def round_only(args):
    from fontTools.ttLib import TTFont

    font = TTFont(args.font, recalcTimestamp=False)
    count = round_outlines(font)
    font.save(args.out)
    print("%d glyphs rounded to %d units, written to %s" % (count, GRID, args.out))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    steps = ap.add_subparsers(dest="step", required=True)
    cut_step = steps.add_parser("cut", help="the menu's characters from Google's font, rounded")
    cut_step.add_argument("font")
    cut_step.add_argument("out")
    cut_step.add_argument("--text", action="append", default=[], help="a UTF-8 file whose characters are kept")
    round_step = steps.add_parser("round", help="rounds a font's outlines")
    round_step.add_argument("font")
    round_step.add_argument("out")
    args = ap.parse_args()
    {"cut": cut, "round": round_only}[args.step](args)


if __name__ == "__main__":
    main()
