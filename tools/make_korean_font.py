#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cut Noto Sans KR down for the menu, without putting it in the DOL.

Official source, Google Fonts commit b38c5c93af322c45f633e17ac440ec1e6c94d489:
https://github.com/google/fonts/tree/b38c5c93af322c45f633e17ac440ec1e6c94d489/ofl/notosanskr
Download NotoSansKR[wght].ttf there; its OFL is wii/font/Korean-OFL.txt.
The original stays outside the repository. Needs fontTools.

    python tools/make_korean_font.py NotoSansKR.ttf wii/font/korean.ttf \
        --titles titles-ko.txt

--titles is GameTDB's https://www.gametdb.com/wiitdb.txt?LANG=KO, saved as
UTF-8 (wii/online/online.cpp downloads it as sd:/riftwii/titles-ko.txt).
Pass additional titles-*.txt with --titles to retain their characters too.
The title snapshot in tools/korean_title_chars.txt, all 2,350 KS X 1001
syllables, Jamo blocks, and ko.po are always kept.
Bold at weight 700 matches the weight of the menu's M PLUS Rounded 1c.
"""

import argparse
import hashlib
import pathlib

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._g_l_y_f import dropImpliedOnCurvePoints
from fontTools.varLib.instancer import instantiateVariableFont

import make_menu_font

ROOT = pathlib.Path(__file__).resolve().parent.parent
JAMO_RANGES = ((0x1100, 0x11FF), (0x3131, 0x318E),
               (0xA960, 0xA97C), (0xD7B0, 0xD7C6), (0xD7CB, 0xD7FB))


def common_syllables():
    """KS X 1001's Hangul rows, rather than CP949's extended syllables."""
    return {ord(bytes((lead, trail)).decode("euc_kr"))
            for lead in range(0xB0, 0xC9) for trail in range(0xA1, 0xFF)}


def text_codepoints(path):
    return {ord(char) for char in path.read_text(encoding="utf-8-sig") if char.isprintable()}


def build(args):
    expected_hash = "194018e6b2b293a7964f037b25c0249ce1418bc9ab3c971060a03aa57861e252"
    if hashlib.sha256(args.font.read_bytes()).hexdigest() != expected_hash:
        raise SystemExit("Use Noto Sans KR from the Google Fonts commit above")
    wanted = common_syllables()
    # Retain the title snapshot even if a later GameTDB download changes.
    manifest = ROOT / "tools/korean_title_chars.txt"
    for line in manifest.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith("#"):
            wanted.update(int(point, 16) for point in line.split())
    for first, last in JAMO_RANGES:
        wanted.update(range(first, last + 1))
    wanted.update(text_codepoints(ROOT / "wii/lang/ko.po"))
    for path in args.titles:
        wanted.update(text_codepoints(path))

    font = TTFont(args.font, recalcTimestamp=False)
    # A fixed weight needs neither variation tables nor their outlines.
    if "fvar" in font:
        instantiateVariableFont(font, {"wght": 700}, inplace=True)
    cmap = font.getBestCmap()
    missing = wanted - cmap.keys()
    if missing:
        points = ", ".join("U+%04X" % cp for cp in sorted(missing))
        raise SystemExit("The source font lacks required characters: " + points)

    options = subset.Options()
    options.hinting = False
    options.layout_features = []  # FreeTypeGX draws one code point at a time.
    options.drop_tables += ["GSUB", "GPOS", "GDEF", "kern", "vhea", "vmtx", "VORG", "BASE"]
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
    options.notdef_outline = True
    cutter = subset.Subsetter(options)
    cutter.populate(unicodes=wanted)
    cutter.subset(font)

    # 8 / 1000 em, as with the menu font. Preserve advances and bounds.
    if font["head"].unitsPerEm != 1000:
        raise SystemExit("Expected 1000 units per em before rounding")
    make_menu_font.round_outlines(font)
    removed = 0
    for glyph in font["glyf"].glyphs.values():
        removed += len(dropImpliedOnCurvePoints(glyph))

    # A distinct family name identifies this modified font in the release.
    names = font["name"]
    for name_id, value in ((1, "RiftWii Korean"), (2, "Bold"),
                           (3, "RiftWii Korean 1.0"), (4, "RiftWii Korean Bold"),
                           (6, "RiftWiiKorean-Bold")):
        for platform, encoding, language in ((3, 1, 0x409), (1, 0, 0)):
            names.setName(value, name_id, platform, encoding, language)
    font.save(args.out)
    before = args.font.stat().st_size
    after = args.out.stat().st_size
    print("%d characters; %d implied points removed; 8-unit grid" % (len(wanted), removed))
    print("%d -> %d bytes (%.1f%% smaller): %s" %
          (before, after, 100 * (1 - after / before), args.out))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=pathlib.Path)
    parser.add_argument("out", type=pathlib.Path)
    parser.add_argument("--titles", type=pathlib.Path, action="append", default=[])
    build(parser.parse_args())


if __name__ == "__main__":
    main()
