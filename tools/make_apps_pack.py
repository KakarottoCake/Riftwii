#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""riftwii-apps.pack: the release zip's sd:/apps files besides RiftWii's
own boot.dol, meta.xml and music.ogg, for the in-app update
(include/riftwii/themepack.hpp has the layout).

    python tools/make_apps_pack.py riftwii-vYYMM-N.zip out/riftwii-apps.pack

What goes in is the channel installer's folder and RiftWii's icon, the
files wii/online.cpp (AppsFileAllowed) writes.
"""
import re
import sys
import zipfile

PREFIX = "sd-card/apps/"
NAME = re.compile(r"^[A-Za-z0-9_\-. ]{1,64}$")


def wanted(path):
    return path.startswith("riftwii_channel/") or path == "riftwii/icon.png"


def main():
    zip_path, out = sys.argv[1], sys.argv[2]
    parts = [b"RIFTWII APPS 1\n"]
    count = 0
    with zipfile.ZipFile(zip_path) as z:
        for info in sorted(z.infolist(), key=lambda i: i.filename):
            if info.is_dir() or not info.filename.startswith(PREFIX):
                continue
            path = info.filename[len(PREFIX):]
            if not wanted(path):
                continue
            folder, _, name = path.partition("/")
            assert NAME.match(folder) and NAME.match(name) and "/" not in name, path
            data = z.read(info)
            parts.append(("file %s %d\n" % (path, len(data))).encode())
            parts.append(data + b"\n")
            count += 1
    assert count >= 3, "no channel installer in the zip"
    parts.append(b"end\n")
    with open(out, "wb") as f:
        f.write(b"".join(parts))
    print("%s: %d files" % (out, count))


if __name__ == "__main__":
    main()
