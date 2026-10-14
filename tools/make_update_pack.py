#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""riftwii-update.pack: what the in-app update brings besides boot.dol
(include/riftwii/themepack.hpp has the layout). Two parts, one after the
other:

- the themes RiftWii comes with ("RIFTWII THEMES 1"): every folder in
  themes/ but the kit. RETIRED lists themes RiftWii came with once and no
  longer does, with the theme a player using one is moved to and the
  files it shipped with (only those are removed);
- the release zip's sd:/apps files besides RiftWii's own boot.dol,
  meta.xml and music.ogg ("RIFTWII APPS 1"): the channel installer's
  folder and RiftWii's icon, the files wii/online/online.cpp (AppsFileAllowed)
  writes.

    python tools/make_update_pack.py riftwii-vYYMM-N.zip out/riftwii-update.pack
"""
import pathlib
import re
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
THEMES = ROOT / "themes"
SKIP = {"kit"}
# (name, replacement or "-", [files])
RETIRED = []

NAME = re.compile(r"^[A-Za-z0-9_\- ]{1,64}$")
FILE = re.compile(r"^[A-Za-z0-9_\-. ]{1,64}$")
APPS = "sd-card/apps/"


def fail(message):
    # Not assert: python -O drops those, and a console trusts a pack whose
    # SHA-256 matches completely.
    sys.exit("make_update_pack: " + message)


def tracked_theme_files():
    """themes/ as the commit has it: only tracked files (an untracked
    desktop.ini or Thumbs.db stays out), and none changed since."""
    git = ["git", "-C", str(ROOT)]
    changed = subprocess.run(git + ["status", "--porcelain", "--untracked-files=no", "--", "themes"], capture_output=True, text=True, check=True)
    if changed.stdout.strip():
        fail("themes/ has changes not committed:\n" + changed.stdout)
    listed = subprocess.run(git + ["ls-files", "-z", "--", "themes"], capture_output=True, text=True, check=True)
    return {ROOT / f for f in listed.stdout.split("\0") if f}


def file_entry(path, data):
    return ("file %s %d\n" % (path, len(data))).encode() + data + b"\n"


def themes_part():
    parts = [b"RIFTWII THEMES 1\n"]
    for name, replacement, files in RETIRED:
        parts.append(("retire %s %s %s\n" % (name, replacement, " ".join(files))).rstrip().encode() + b"\n")
    count = 0
    tracked = tracked_theme_files()
    for theme in sorted(p for p in THEMES.iterdir() if p.is_dir() and p.name not in SKIP):
        if not (NAME.match(theme.name) and not theme.name.startswith(".")):
            fail("theme folder name not allowed: %r" % theme.name)
        for f in sorted(theme.iterdir()):
            if not f.is_file() or f not in tracked:
                continue
            if not (FILE.match(f.name) and not f.name.startswith(".")):
                fail("theme file name not allowed: %s/%s" % (theme.name, f.name))
            data = f.read_bytes()
            if f.suffix == ".ini":  # as the release zip has it, so a card from the zip matches
                data = data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
            parts.append(file_entry("%s/%s" % (theme.name, f.name), data))
            count += 1
    parts.append(b"end\n")
    return b"".join(parts), count


def apps_part(zip_path):
    parts = [b"RIFTWII APPS 1\n"]
    count = 0
    found = set()
    with zipfile.ZipFile(zip_path) as z:
        for info in sorted(z.infolist(), key=lambda i: i.filename):
            if info.is_dir() or not info.filename.startswith(APPS):
                continue
            path = info.filename[len(APPS):]
            if not (path.startswith("riftwii_channel/") or path == "riftwii/icon.png"):
                continue
            folder, _, name = path.partition("/")
            if not (NAME.match(folder) and FILE.match(name) and "/" not in name):
                fail("file name not allowed in the zip: " + path)
            parts.append(file_entry(path, z.read(info)))
            found.add(path)
            count += 1
    # The installer the channel prompt runs, by name: three files of any
    # kind used to be enough.
    for needed in ("riftwii_channel/boot.dol", "riftwii_channel/meta.xml"):
        if needed not in found:
            fail("the zip has no %s%s" % (APPS, needed))
    parts.append(b"end\n")
    return b"".join(parts), count


def main():
    if len(sys.argv) != 3:
        fail("usage: make_update_pack.py riftwii-vYYMM-N.zip out/riftwii-update.pack")
    themes, theme_files = themes_part()
    apps, app_files = apps_part(sys.argv[1])
    out = pathlib.Path(sys.argv[2])
    out.write_bytes(themes + apps)
    print("%s: %d theme files, %d app files, %d bytes" % (out, theme_files, app_files, out.stat().st_size))


if __name__ == "__main__":
    main()
