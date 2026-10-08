#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""riftwii-themes.pack: the themes a release comes with, for the in-app
update (include/riftwii/themepack.hpp has the layout).

    python tools/make_theme_pack.py out/riftwii-themes.pack

Every folder in themes/ but the kit goes in. RETIRED lists themes RiftWii
came with once and no longer does, with the theme a player using one is
moved to and the files it shipped with (only those are removed).
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
THEMES = ROOT / "themes"
SKIP = {"kit"}
# (name, replacement or "-", [files])
RETIRED = []

NAME = re.compile(r"^[A-Za-z0-9_\- ]{1,64}$")
FILE = re.compile(r"^[A-Za-z0-9_\-. ]{1,64}$")


def main():
    out = pathlib.Path(sys.argv[1])
    parts = [b"RIFTWII THEMES 1\n"]
    for name, replacement, files in RETIRED:
        parts.append(("retire %s %s %s\n" % (name, replacement, " ".join(files))).rstrip().encode() + b"\n")
    count = 0
    for theme in sorted(p for p in THEMES.iterdir() if p.is_dir() and p.name not in SKIP):
        assert NAME.match(theme.name) and not theme.name.startswith("."), theme.name
        for f in sorted(theme.iterdir()):
            if not f.is_file():
                continue
            assert FILE.match(f.name) and not f.name.startswith("."), f.name
            data = f.read_bytes()
            if f.suffix == ".ini":  # as the release zip has it, so a card from the zip matches
                data = data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
            parts.append(("file %s/%s %d\n" % (theme.name, f.name, len(data))).encode())
            parts.append(data + b"\n")
            count += 1
    parts.append(b"end\n")
    out.write_bytes(b"".join(parts))
    print("%s: %d files, %d bytes" % (out, count, out.stat().st_size))


if __name__ == "__main__":
    main()
