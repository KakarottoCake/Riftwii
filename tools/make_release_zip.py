#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""riftwii-vYYMM-N.zip, the release's download, as docs/DEVELOPING.md
(Releasing) lays it out:

    sd-card/apps/riftwii/          boot.dol, meta.xml, icon.png, music.ogg, plugins/
    sd-card/apps/riftwii_channel/  boot.dol (the channel installer), meta.xml, icon.png
    sd-card/riftwii/themes/        the themes RiftWii comes with
    sd-card/..., usb-drive/...     the folders for games and packs, each with a note
    README.txt, LICENSE.txt, NOTICE.md, SOURCE.txt

The player-facing text files are kept in release/zip/ and filled in here
({{VERSION}}, {{TAG}}, {{COMMIT}}, {{REPO_URL}}). Text goes out with CRLF
line ends, as players open it in Notepad.

    python3 tools/make_release_zip.py --version 2610-500 --dol riftwii.dol \\
        --channel build-channel/installer.dol --plugins build-plugins/plugins \\
        --out riftwii-v2610-500.zip

The release workflow (.github/workflows/release.yml) runs it; it works the
same by hand.
"""
import argparse
import pathlib
import re
import subprocess
import sys
import time
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEMPLATES = ROOT / "release" / "zip"
REPO_URL = "https://github.com/KakarottoCake/Riftwii"
VERSION = re.compile(r"^[0-9]{4}-[0-9]+$")
PLUGIN_FILE = re.compile(r"^([A-Za-z0-9_-]{1,16})\.([A-Za-z0-9_-]{1,32})\.rwp$")
# make_update_pack.py refuses a zip without these, so refuse it here first.
REQUIRED_PLUGINS = ("gamecube",)
TEXT_SUFFIXES = (".txt", ".md", ".xml", ".ini")


def fail(message):
    sys.exit("make_release_zip: " + message)


def git(*args):
    result = subprocess.run(["git", "-C", str(ROOT)] + list(args), capture_output=True, text=True, check=True)
    return result.stdout.strip()


def fill(text, values):
    """Replace each {{NAME}}, and resize a ==== line under a title that
    grew or shrank with the version in it."""
    for name, value in values.items():
        text = text.replace("{{%s}}" % name, value)
    left = re.findall(r"\{\{[A-Z_]+\}\}", text)
    if left:
        fail("a template still has %s" % ", ".join(sorted(set(left))))
    lines = text.split("\n")
    for i in range(1, len(lines)):
        is_underline = lines[i] and set(lines[i]) == {"="}
        if is_underline and lines[i - 1]:
            lines[i] = "=" * len(lines[i - 1])
    return "\n".join(lines)


def crlf(text):
    return text.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")


def release_meta_xml(version, commit_time):
    """hbc/meta.xml with this release's version and date, as the Homebrew
    Channel shows them."""
    meta = (ROOT / "hbc" / "meta.xml").read_text(encoding="utf-8")
    meta, versions = re.subn(r"<version>[^<]*</version>", "<version>%s</version>" % version, meta, count=1)
    stamp = time.strftime("%Y%m%d%H%M%S", time.gmtime(commit_time))
    meta, dates = re.subn(r"<release_date>[^<]*</release_date>", "<release_date>%s</release_date>" % stamp, meta, count=1)
    if versions != 1 or dates != 1:
        fail("hbc/meta.xml has no <version> or <release_date> to fill in")
    return meta


def plugin_files(folder, version):
    """Every plugin of this build, each named for this version: a plugin
    left over from another build would be refused on the Wii."""
    found = {}
    for path in sorted(folder.iterdir()):
        match = PLUGIN_FILE.match(path.name)
        if not path.is_file() or not match:
            fail("%s is not a plugin file" % path)
        if match.group(2) != version:
            fail("%s is from another version than %s" % (path.name, version))
        found[match.group(1)] = path
    for name in REQUIRED_PLUGINS:
        if name not in found:
            fail("no %s.%s.rwp in %s" % (name, version, folder))
    return found


def tracked_theme_files():
    """themes/ as the commit has it, without the kit (its own download) and
    without themes/README.txt (written for the repository; release/zip has
    the players' one)."""
    listed = git("ls-files", "-z", "--", "themes").split("\0")
    files = []
    for name in listed:
        if not name or name.startswith("themes/kit/") or name == "themes/README.txt":
            continue
        files.append(name)
    return files


def zip_entries(args, values, commit_time):
    """(name in the zip, bytes) for every file, in the order they go in."""
    apps = "sd-card/apps/"
    entries = [
        (apps + "riftwii/boot.dol", args.dol.read_bytes()),
        (apps + "riftwii/meta.xml", crlf(release_meta_xml(args.version, commit_time))),
        (apps + "riftwii/icon.png", (ROOT / "hbc" / "icon.png").read_bytes()),
        (apps + "riftwii/music.ogg", (ROOT / "hbc" / "music.ogg").read_bytes()),
    ]
    for path in plugin_files(args.plugins, args.version).values():
        entries.append((apps + "riftwii/plugins/" + path.name, path.read_bytes()))
    channel_hbc = ROOT / "channel" / "installer" / "hbc"
    entries += [
        (apps + "riftwii_channel/boot.dol", args.channel.read_bytes()),
        (apps + "riftwii_channel/meta.xml", crlf((channel_hbc / "meta.xml").read_text(encoding="utf-8"))),
        (apps + "riftwii_channel/icon.png", (channel_hbc / "icon.png").read_bytes()),
    ]
    for name in tracked_theme_files():
        entries.append(("sd-card/riftwii/" + name, (ROOT / name).read_bytes()))
    for name in sorted(git("ls-files", "-z", "--", "release/zip").split("\0")):
        if not name:
            continue
        path = ROOT / name
        text = fill(path.read_text(encoding="utf-8"), values)
        entries.append((path.relative_to(TEMPLATES).as_posix(), crlf(text)))
    entries += [
        ("LICENSE.txt", crlf((ROOT / "LICENSE").read_text(encoding="utf-8"))),
        ("NOTICE.md", crlf((ROOT / "NOTICE.md").read_text(encoding="utf-8"))),
    ]
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--version", required=True, help="YYMM-N, as Makefile.wii numbers the build")
    parser.add_argument("--dol", required=True, type=pathlib.Path, help="riftwii.dol (the zip's boot.dol)")
    parser.add_argument("--channel", required=True, type=pathlib.Path, help="build-channel/installer.dol")
    parser.add_argument("--plugins", required=True, type=pathlib.Path, help="build-plugins/plugins")
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args()
    if not VERSION.match(args.version):
        fail("%r is not a YYMM-N version" % args.version)

    # A zip built from changed files would name a commit that doesn't hold them.
    changed = git("status", "--porcelain", "--untracked-files=no", "--", "hbc", "themes", "release", "channel/installer/hbc", "LICENSE", "NOTICE.md")
    if changed:
        fail("files the zip takes have changes not committed:\n" + changed)

    commit = git("rev-parse", "HEAD")
    commit_time = int(git("show", "-s", "--format=%ct", "HEAD"))
    values = {"VERSION": args.version, "TAG": "v" + args.version, "COMMIT": commit, "REPO_URL": REPO_URL}

    # Every entry carries the commit's time, so the same commit gives the
    # same zip.
    stamp = time.gmtime(commit_time)[:6]
    entries = zip_entries(args, values, commit_time)
    names = [name for name, data in entries]
    if len(names) != len(set(names)):
        fail("two files would have the same name in the zip")
    with zipfile.ZipFile(args.out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for name, data in entries:
            info = zipfile.ZipInfo(name, date_time=stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            out.writestr(info, data)
    print("%s: %d files, %d bytes, commit %s" % (args.out, len(entries), args.out.stat().st_size, commit[:8]))


if __name__ == "__main__":
    main()
