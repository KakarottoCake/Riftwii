#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes src/data/builtinpads_data.cpp from vendor-nintendont-controllers:
the USB controller layouts RiftWii knows without a file on the SD card
(src/data/builtinpads.cpp, wii/controllers/hidpads.cpp).

The layouts are Nintendont's controller.ini files, kept as they are. Each
becomes one entry keyed by its VID and PID. Left out:
  - Nintendo's Wii U GameCube adapter (057E:0337), which RiftWii drives
    itself (wii/controllers/gcadapter.cpp);
  - files with MultiIn=2, 3 or 4, reports that carry several pads, which
    RiftWii does not read (docs/GUIDE.md).
Two files with the same IDs: PREFERRED names the one kept.

    py -3.13 tools/make_builtin_pads.py
"""
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "vendor-nintendont-controllers"
OUT = ROOT / "src" / "data" / "builtinpads_data.cpp"

# The 8BitDo SNES30 Pro answers with a DualShock 4's IDs; the DualShock 4
# is the pad most people have with them.
PREFERRED = {0x054C05C4: "controller_ps4.ini"}
SKIPPED_IDS = {0x057E0337}


def value(text, key):
    match = re.search(r"^\s*" + key + r"\s*=\s*([0-9A-Fa-f]+)\s*$", text, re.MULTILINE | re.IGNORECASE)
    return match.group(1) if match else None


def c_string(text):
    out = []
    for line in text.splitlines(keepends=True):
        escaped = line.replace("\\", "\\\\").replace('"', '\\"').replace("\r", "\\r").replace("\n", "\\n")
        out.append('        "' + escaped + '"')
    return "\n".join(out) if out else '        ""'


entries = {}
for path in sorted(SRC.glob("*.ini")):
    text = path.read_text(encoding="ascii")
    vid, pid = value(text, "VID"), value(text, "PID")
    if vid is None or pid is None:
        raise SystemExit(f"{path.name}: no VID or PID")
    multi = value(text, "MultiIn")
    if multi is not None and int(multi, 16) >= 2:
        continue
    key = int(vid, 16) << 16 | int(pid, 16)
    if key in SKIPPED_IDS:
        continue
    if key in entries and PREFERRED.get(key) != path.name:
        if PREFERRED.get(key) != entries[key][0]:
            raise SystemExit(f"{path.name}: {key:08X} again, and PREFERRED names neither file")
        continue
    header = re.search(r"^\[(.+?)\]", text, re.MULTILINE)
    name = (header.group(1).strip() if header else path.stem).replace("\\", "\\\\").replace("\"", "\\\"")
    entries[key] = (path.name, name, text)

lines = [
    "// SPDX-FileCopyrightText: 2026 RiftWii contributors",
    "// SPDX-FileCopyrightText: FIX94 and the Nintendont contributors, used with permission",
    "// SPDX-License-Identifier: GPL-3.0-or-later",
    "// Made by tools/make_builtin_pads.py from vendor-nintendont-controllers: do not edit.",
    '#include "riftwii/builtinpads.hpp"',
    "",
    "namespace riftwii {",
    "",
    "const BuiltinPad kBuiltinPads[] = {",
]
for key in sorted(entries):
    file_name, name, text = entries[key]
    lines.append(f"    {{0x{key:08X}u, \"{name}\", \"{file_name}\",")
    lines.append(c_string(text) + "},")
lines += [
    "};",
    "",
    "const std::size_t kBuiltinPadCount = sizeof(kBuiltinPads) / sizeof(kBuiltinPads[0]);",
    "",
    "}  // namespace riftwii",
    "",
]
OUT.write_text("\n".join(lines), encoding="ascii", newline="\n")
print(f"{OUT.relative_to(ROOT)}: {len(entries)} layouts")
