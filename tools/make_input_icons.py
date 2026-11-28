#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes src/art/inputicons_data.cpp from vendor-kenney/input-prompts: each
picture's alpha channel (64x64, a byte a pixel), the menu drawing them in
its theme's colour (src/art/inputicons.cpp). The masks are mostly runs of
fully clear and fully solid pixels, so each is stored as (count, value) byte
pairs: about 7 KiB for all of them instead of 56 KiB of MEM1.

The pictures are Kenney's "Input Prompts" 1.5 (CC0, kenney.nl): white
shapes on a transparent ground, so the alpha is the whole picture.

    py -3.13 tools/make_input_icons.py      (needs Pillow)
"""
import pathlib
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "vendor-kenney" / "input-prompts"
OUT = ROOT / "src" / "art" / "inputicons_data.cpp"
SIZE = 64

pictures = sorted(p for p in SRC.glob("*.png"))
lines = [
    "// SPDX-FileCopyrightText: 2026 RiftWii contributors",
    "// SPDX-FileCopyrightText: Kenney (www.kenney.nl), Input Prompts 1.5, CC0",
    "// SPDX-License-Identifier: GPL-3.0-or-later",
    "// Made by tools/make_input_icons.py from vendor-kenney/input-prompts: do not edit.",
    '#include "inputicons_data.hpp"',
    "",
    "namespace riftwii {",
    "",
]
names = []
for p in pictures:
    im = Image.open(p).convert("RGBA")
    assert im.size == (SIZE, SIZE), (p.name, im.size)
    alpha = im.getchannel("A").tobytes()
    runs = []
    i = 0
    while i < len(alpha):
        count = 1
        while i + count < len(alpha) and alpha[i + count] == alpha[i] and count < 255:
            count += 1
        runs += [count, alpha[i]]
        i += count
    ident = "k_" + p.stem
    names.append((p.stem, ident, len(runs)))
    lines.append("static const unsigned char %s[%d] = {" % (ident, len(runs)))
    for i in range(0, len(runs), 32):
        lines.append("    " + ", ".join(str(b) for b in runs[i:i + 32]) + ",")
    lines.append("};")
    lines.append("")
lines.append("const InputIconData kInputIcons[] = {")
for stem, ident, run_bytes in names:
    lines.append('    {"%s", %s, %d},' % (stem, ident, run_bytes))
lines.append("};")
lines.append("const unsigned kInputIconCount = %d;" % len(names))
lines.append("")
lines.append("}  // namespace riftwii")
OUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
print(OUT, len(names), "icons")
