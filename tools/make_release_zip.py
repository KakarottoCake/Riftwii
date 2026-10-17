#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""The release zip as docs/DEVELOPING.md's "Releasing" packs it, from a
build: RiftWii and the channel installer under sd-card/apps, the themes,
the folders players fill (each with a note saying what goes there), and
the GPL's copies (LICENSE.txt, NOTICE.md, SOURCE.txt). The nightly
workflow (.github/workflows/nightly.yml) runs it; a hand-made release
may too, then swap in its own README.txt.

    python3 tools/make_release_zip.py VERSION riftwii.dol installer.dol out.zip [--nightly]

VERSION is the DOL's (2610-123): the tag is v + VERSION, and must point
at the commit checked out here.
"""
import argparse
import datetime
import pathlib
import re
import subprocess
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
REPO = "https://github.com/KakarottoCake/Riftwii"

NOTES = {
    "sd-card/riivolution/PUT_MOD_PACKS_HERE.txt":
        "Put unpacked mod packs (each with its own .xml file) in this folder.\n"
        "See the README.txt next to the sd-card and usb-drive folders.\n",
    "sd-card/wbfs/PUT_WBFS_GAMES_HERE.txt":
        "Put .wbfs game files here (plus every .wbf1, .wbf2, ... piece when a\n"
        "game is split), either directly or one folder below, for example:\n"
        "  wbfs/Game [RMCE01]/RMCE01.wbfs\n"
        "Raw .iso copies work here too (wbfs/Game [RMCE01]/RMCE01.iso), or in games. Only FAT32 cards work.\n"
        "SD game boot is experimental: this same card also holds your mods\n"
        "and saves.\n",
    "sd-card/games/PUT_ISO_FILES_HERE.txt":
        "Put raw .iso disc copies and Dolphin .rvz files here. Split .wbfs\n"
        "games go in the wbfs folder instead. Only FAT32 cards work.\n",
    "usb-drive/wbfs/PUT_WBFS_GAMES_HERE.txt":
        "Put .wbfs game files here (plus every .wbf1, .wbf2, ... piece when a\n"
        "game is split), either directly or one folder below, for example:\n"
        "  wbfs/Game [RMCE01]/RMCE01.wbfs\n"
        "Raw .iso copies work here too (wbfs/Game [RMCE01]/RMCE01.iso), or in games. FAT32 and NTFS drives work.\n",
    "usb-drive/games/PUT_ISO_FILES_HERE.txt":
        "Put raw .iso disc copies and Dolphin .rvz files here. Split .wbfs games\n"
        "go in the wbfs folder instead. FAT32 and NTFS drives work.\n",
}

# For players, not themes/README.txt (that one is for the repository).
THEMES_README = """Menu themes go here, one folder each (Midnight came with RiftWii).
Pick one in RiftWii under Settings > Theme.
To make your own, get riftwii-theme-kit from RiftWii's download page:
it has a starter theme, picture templates and a guide.
"""

NIGHTLY_README = """RiftWii {version} (nightly)
{rule}

This is a nightly build: made automatically from the newest code on
{date}, and not checked on a real Wii by anyone. Things may be broken.
The newest tested release is at {repo}/releases/latest
and its README.txt tells you everything about RiftWii.

INSTALLING
----------
Copy the sd-card folder onto your SD card and let it replace the files
(and usb-drive onto your USB drive if you use one). Your settings, play
history, covers, mods and games stay where they are.

With Settings > Updates on Beta, RiftWii offers each new nightly by
itself. Set it to Stable to go back to tested releases only.

PROBLEMS
--------
Use Settings > Send a problem report and send the link, or open an
issue at {repo}/issues with sd:/riftwii/session.log,
sd:/riftwii/boot.log, your Wii model, your Menu IOS and your cIOS, and
say it is nightly {version}.

LICENSE AND SOURCE
------------------
RiftWii is free software under the GNU General Public License, version
3 or later, with no warranty (LICENSE.txt). SOURCE.txt says where its
source is; NOTICE.md credits every project RiftWii contains or follows.
"""

SOURCE = """RiftWii {version}: source code
{rule}

RiftWii is free software under the GNU General Public License, version 3
or (at your option) any later version. LICENSE.txt is the license's full
text. NOTICE.md lists every third-party component, its origin and its
license.

The complete corresponding source code of this release:

  {repo}/tree/v{version}
  (tag v{version}, commit {commit})

  Source archives: {repo}/releases/tag/v{version}
  Building: docs/DEVELOPING.md in the source.

Both programs in this zip are built from it:
  sd-card/apps/riftwii/boot.dol          Makefile.wii
  sd-card/apps/riftwii_channel/boot.dol  Makefile.channel

The Gecko code handler inside boot.dol (Gecko OS by Nuke, brkirch, Link
and the Gecko authors, GPL-2.0-or-later) is built from
vendor-gecko/codehandleronly.s in the same source; vendor-gecko/build.sh
rebuilds it and checks it byte for byte.

This source will stay available at those addresses for as long as this
release is offered.
"""


def crlf(text):
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode()


def git(*args):
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True).stdout.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("version")
    ap.add_argument("dol")
    ap.add_argument("installer")
    ap.add_argument("out")
    ap.add_argument("--nightly", action="store_true", help="write a nightly README.txt")
    args = ap.parse_args()
    assert re.fullmatch(r"\d{4}(-\d+)?", args.version), args.version

    commit = git("rev-parse", "--short=7", "HEAD")
    when = datetime.datetime.fromtimestamp(int(git("log", "-1", "--format=%ct")), datetime.timezone.utc)
    stamp = when.timetuple()[:6]  # every entry dated by the commit: the same commit packs the same zip

    meta = (ROOT / "hbc/meta.xml").read_text(encoding="utf-8")
    meta = re.sub(r"<version>[^<]*</version>", "<version>%s</version>" % args.version, meta, count=1)
    meta = re.sub(r"<release_date>[^<]*</release_date>",
                  "<release_date>%s</release_date>" % when.strftime("%Y%m%d%H%M%S"), meta, count=1)

    files = [
        ("sd-card/apps/riftwii/boot.dol", pathlib.Path(args.dol).read_bytes()),
        ("sd-card/apps/riftwii/icon.png", (ROOT / "hbc/icon.png").read_bytes()),
        ("sd-card/apps/riftwii/meta.xml", meta.encode()),
        ("sd-card/apps/riftwii/music.ogg", (ROOT / "hbc/music.ogg").read_bytes()),
        ("sd-card/apps/riftwii_channel/boot.dol", pathlib.Path(args.installer).read_bytes()),
        ("sd-card/apps/riftwii_channel/icon.png", (ROOT / "channel/installer/hbc/icon.png").read_bytes()),
        ("sd-card/apps/riftwii_channel/meta.xml", (ROOT / "channel/installer/hbc/meta.xml").read_bytes()),
    ]
    files += [(path, text.encode()) for path, text in NOTES.items()]
    if args.nightly:
        title = "RiftWii %s (nightly)" % args.version
        files.append(("README.txt", crlf(NIGHTLY_README.format(
            version=args.version, rule="=" * len(title), date=when.strftime("%d %B %Y"), repo=REPO))))
    files.append(("LICENSE.txt", crlf((ROOT / "LICENSE").read_text(encoding="utf-8"))))
    files.append(("NOTICE.md", crlf((ROOT / "NOTICE.md").read_text(encoding="utf-8"))))
    title = "RiftWii %s: source code" % args.version
    files.append(("SOURCE.txt", crlf(SOURCE.format(
        version=args.version, rule="=" * len(title), repo=REPO, commit=commit))))
    # The themes as make_update_pack.py sends them, so a card from the zip matches.
    themes = ROOT / "themes"
    files.append(("sd-card/riftwii/themes/README.txt", crlf(THEMES_README)))
    for theme in sorted(p for p in themes.iterdir() if p.is_dir() and p.name != "kit"):
        for f in sorted(p for p in theme.iterdir() if p.is_file()):
            data = f.read_bytes()
            if f.suffix == ".ini":
                data = data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
            files.append(("sd-card/riftwii/themes/%s/%s" % (theme.name, f.name), data))

    out = pathlib.Path(args.out)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path, data in files:
            info = zipfile.ZipInfo(path, stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data)
    print("%s: %d files, %d bytes" % (out, len(files), out.stat().st_size))


if __name__ == "__main__":
    main()
