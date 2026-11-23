# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the app store's list (apps.json, docs/APPSTORE.md) from one
manifest per app, and keeps each manifest pinned to an approved release.

  app_list.py update <manifests> [id ...]   pin each GitHub app to its newest
                                            release (writes the manifests;
                                            the pull request is the approval)
  app_list.py build <manifests> <out>       download every pinned file, check
                                            it against its pin and the Wii's
                                            rules, write <out>/apps.json and
                                            <out>/icons/<id>.png
  app_list.py check <manifests>             build without writing (for PRs)

Downloads are cached in --cache (default .app-cache). Set GITHUB_TOKEN to
lift GitHub's API limit. The rules below follow src/net/applist.cpp; the
built list is read back by build-host/app_list_check when --check-tool
names it, so the Wii's own parser has the last word.
"""
import argparse
import fnmatch
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import urllib.request
import zipfile

SCHEMA = 1
SITE = "https://riftwii.github.io/"
ICON_BASE = SITE + "apps/icons/"
CATEGORIES = ["games", "emulators", "media", "internet", "online", "gametools", "loaders", "system",
              "wiimenu", "controllers", "creative", "toys"]
FORMATS = {"zip", "dol", "7z", "rar", "tar", "tar.bz2", "tar.gz"}
PLATFORMS = {"wii", "vwii", "mini"}
RESERVED = {"apps", "riftwii", "private", "title", "ticket", "shared1", "shared2", "sys", "import", "meta",
            "tmp", "wfs", "nands", "wbfs", "games", "wad", "wads", "wiiware", "riivolution", "codes",
            "txtcodes", "gct", "saves", "bootmii", "wiiflow", "usbloader_gx", "nintendont", "roms"}
MAX_DOWNLOAD = 128 << 20
MAX_FILES = 8192
MAX_UNPACKED = 256 << 20
MAX_RULES = 6
MAX_DATA_FOLDERS = 4
MAX_ICON = 64 << 10
MAX_CARD_PATH = 255
# Field sizes of AppListing (bytes, without the final NUL).
LIMITS = {"id": 47, "name": 63, "author": 63, "version": 31, "description": 159, "license": 31,
          "page": 199, "icon": 199, "url": 299, "rule": 63, "data_folder": 31}


class AppError(Exception):
    pass


# ---------------------------------------------------------------- rules ---

def id_ok(text):
    return (0 < len(text) <= LIMITS["id"] and text[0] not in ". " and text[-1] not in ". "
            and re.fullmatch(r"[A-Za-z0-9\-_ .+()!]+", text) is not None)


def data_folder_ok(text):
    return id_ok(text) and len(text) <= LIMITS["data_folder"] and text.lower() not in RESERVED


def part_ok(part):
    return (part not in ("", ".", "..") and len(part.encode()) <= 255
            and not any(ord(c) < 0x20 or ord(c) == 0x7F or c in '/\\:*?"<>|' for c in part))


def relative_path_ok(path, folder, wildcards=False):
    if not path or path.startswith("/"):
        return False
    if folder:
        if not path.endswith("/"):
            return False
        path = path[:-1]
    elif path.endswith("/"):
        return False
    return all((wildcards and p == "*") or part_ok(p) for p in path.split("/"))


def https_url_ok(url, limit, http_too=False):
    scheme = "https?" if http_too else "https"
    m = re.fullmatch(scheme + r"://([A-Za-z0-9.\-]+)(/[A-Za-z0-9\-_.~/%+!(),=&?]*)", url)
    if not m or len(url) > limit:
        return False
    host, path = m.group(1), m.group(2)
    if "." not in host or not re.search(r"[A-Za-z]", host) or host[0] in ".-" or host[-1] == ".":
        return False
    return "/../" not in path and "/./" not in path and not path.endswith("/..")


def text_ok(text, limit):
    cleaned = "".join(" " if ord(c) < 0x20 or ord(c) == 0x7F else c for c in text).strip()
    return cleaned == text and 0 < len(text.encode()) <= limit


def rule_ok(app_id, data_folders, rule):
    src, dst = rule.get("from"), rule.get("to")
    if not isinstance(src, str) or not isinstance(dst, str):
        return False
    folder = src == "" or src.endswith("/")
    if (src and not relative_path_ok(src, folder, folder)) or not relative_path_ok(dst, folder):
        return False
    if len(src.encode()) > LIMITS["rule"] or len(dst.encode()) > LIMITS["rule"]:
        return False
    return dst.startswith(f"apps/{app_id}/") or any(dst.startswith(d + "/") for d in data_folders)


def install_path(rules, entry):
    """Where a zip entry goes on the card (src/net/applist.cpp's
    app_install_path): None when no rule covers it."""
    for rule in rules:
        src, dst = rule["from"], rule["to"]
        if src == "" or src.endswith("/"):
            want = src.split("/")[:-1]
            have = entry.split("/")
            if len(have) <= len(want) or any(w != "*" and w != h for w, h in zip(want, have)):
                continue
            rest = "/".join(have[len(want):])
            if rest.endswith("/") or not relative_path_ok(rest, False):
                return None
            path = dst + rest
        else:
            if entry != src:
                continue
            path = dst
        return path if len(path.encode()) <= MAX_CARD_PATH else None
    return None


def check_manifest(m, path):
    """The manifest's own fields, before anything is downloaded."""
    where = os.path.basename(path)
    def need(cond, why):
        if not cond:
            raise AppError(f"{where}: {why}")
    need(id_ok(m.get("id", "")), "id must be a plain folder name (letters, digits, -_ .+()!), at most 47 bytes")
    need(os.path.splitext(where)[0] == m["id"], "the file must be named <id>.json")
    for key in ("name", "author", "description"):
        need(isinstance(m.get(key), str) and text_ok(m[key], LIMITS[key]), f"{key} must be plain text, at most {LIMITS[key]} bytes")
    need(m.get("category") in CATEGORIES, f"category must be one of {', '.join(CATEGORIES)}")
    if "license" in m:
        need(isinstance(m["license"], str) and text_ok(m["license"], LIMITS["license"]), "license is too long")
    need(isinstance(m.get("page"), str) and https_url_ok(m["page"], LIMITS["page"]), "page must be an https:// address")
    need(isinstance(m.get("platforms"), list) and m["platforms"] and set(m["platforms"]) <= PLATFORMS,
         "platforms must list some of wii, vwii, mini")
    if "newer" in m:
        need(id_ok(m["newer"]) and m["newer"] != m["id"], "newer must be another app's id")
    folders = m.get("data_folders", [])
    need(isinstance(folders, list) and len(folders) <= MAX_DATA_FOLDERS and all(data_folder_ok(f) for f in folders),
         "data_folders: at most 4 plain names, none of the folders the Wii, RiftWii or loaders own")
    src = m.get("source", {})
    need(("github" in src) != ("url" in src), "source needs exactly one of github or url")
    if "github" in src:
        need(re.fullmatch(r"[A-Za-z0-9\-_.]+/[A-Za-z0-9\-_.]+", src["github"]) is not None, "source.github is owner/repo")
        need(isinstance(src.get("asset"), str) and src["asset"], "source.asset names the release file (a pattern)")
        need(isinstance(src.get("prerelease", False), bool), "source.prerelease is true or false")
    else:
        need(https_url_ok(src["url"], LIMITS["url"], True), "source.url must be an http:// or https:// address")
        need(isinstance(src.get("version"), str) and text_ok(src["version"], LIMITS["version"]), "a url source needs its version")
    fmt = m.get("format", "zip")
    need(fmt in FORMATS, f"format must be one of {', '.join(sorted(FORMATS))}")
    rules = m.get("install", [])
    if fmt == "zip":
        need(isinstance(rules, list) and 0 < len(rules) <= MAX_RULES, "a zip needs 1 to 6 install rules")
        for rule in rules:
            need(rule_ok(m["id"], folders, rule),
                 f"install rule {rule} must write under apps/{m['id']}/ or a data folder, with plain path parts")
    else:
        need(not rules, "only a zip has install rules")


# ------------------------------------------------------------ downloads ---

def fetch(url, token=None, accept=None):
    req = urllib.request.Request(url, headers={"User-Agent": "riftwii-app-list"})
    if token and url.startswith("https://api.github.com/"):
        req.add_header("Authorization", f"Bearer {token}")
    if accept:
        req.add_header("Accept", accept)
    with urllib.request.urlopen(req, timeout=120) as r:
        data = r.read(MAX_DOWNLOAD + 1)
    if len(data) > MAX_DOWNLOAD:
        raise AppError(f"{url}: bigger than {MAX_DOWNLOAD >> 20} MB")
    return data


def github_release(repo, tag, token):
    path = f"releases/tags/{tag}" if tag else "releases/latest"
    return json.loads(fetch(f"https://api.github.com/repos/{repo}/{path}", token))


def newest_release(src, token):
    """The release `update` pins: the latest full release, or with
    "prerelease": true the newest one of any kind (a nightly fork's, each
    under its own tag so earlier pins stay downloadable)."""
    if not src.get("prerelease"):
        return github_release(src["github"], None, token)
    listing = json.loads(fetch(f"https://api.github.com/repos/{src['github']}/releases?per_page=20", token))
    for release in listing:
        if not release.get("draft"):
            return release
    raise AppError(f"{src['github']}: no release")


def release_asset(release, pattern, where):
    hits = [a for a in release.get("assets", []) if fnmatch.fnmatchcase(a["name"], pattern)]
    if len(hits) != 1:
        names = ", ".join(a["name"] for a in release.get("assets", []))
        raise AppError(f"{where}: {len(hits)} release files match {pattern!r} (files: {names})")
    return hits[0]


def cached_download(url, cache):
    os.makedirs(cache, exist_ok=True)
    name = hashlib.sha256(url.encode()).hexdigest()[:24]
    path = os.path.join(cache, name)
    if os.path.exists(path):
        return open(path, "rb").read()
    data = fetch(url)
    with open(path, "wb") as f:
        f.write(data)
    return data


# ------------------------------------------------------------- commands ---

def load_manifests(folder, only=None):
    out = []
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".json"):
            continue
        path = os.path.join(folder, name)
        m = json.load(open(path, encoding="utf-8"))
        if only and m.get("id") not in only:
            continue
        out.append((path, m))
    return out


def cmd_update(args):
    token = os.environ.get("GITHUB_TOKEN")
    changed = 0
    for path, m in load_manifests(args.manifests, set(args.ids) or None):
        src = m.get("source", {})
        if "github" not in src:
            continue
        release = newest_release(src, token)
        if m.get("pin", {}).get("tag") == release["tag_name"]:
            continue
        asset = release_asset(release, src["asset"], os.path.basename(path))
        data = cached_download(asset["browser_download_url"], args.cache)
        m["pin"] = {"tag": release["tag_name"], "sha256": hashlib.sha256(data).hexdigest()}
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(m, f, indent=2, ensure_ascii=False)
            f.write("\n")
        changed += 1
        print(f"{m['id']}: pinned {release['tag_name']} ({asset['name']}, {len(data)} bytes)")
    print(f"{changed} app(s) pinned to a new release")


def build_one(path, m, token, cache, icons):
    """The app's apps.json record, after checking its download."""
    check_manifest(m, path)
    where = os.path.basename(path)
    src = m["source"]
    pin = m.get("pin", {})
    if not re.fullmatch(r"[0-9a-f]{64}", pin.get("sha256", "")):
        raise AppError(f"{where}: no approved pin (run update, or add pin.sha256 for a url source)")
    if "github" in src:
        release = github_release(src["github"], pin.get("tag"), token)
        asset = release_asset(release, src["asset"], where)
        url = asset["browser_download_url"]
        version = m.get("version") or release["tag_name"].lstrip("vV")
        released = (release.get("published_at") or "")[:10]
    else:
        url, version, released = src["url"], src["version"], src.get("released", "")
    if not https_url_ok(url, LIMITS["url"], True):
        raise AppError(f"{where}: the download address {url} breaks the address rule")
    data = cached_download(url, cache)
    digest = hashlib.sha256(data).hexdigest()
    if digest != pin["sha256"]:
        raise AppError(f"{where}: the download is not the approved file (sha256 {digest}, pinned {pin['sha256']})")
    fmt = m.get("format", "zip")
    download = {"url": url, "format": fmt, "size": len(data), "sha256": digest}
    if fmt == "zip":
        files, unpacked, targets = 0, 0, set()
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            for info in z.infolist():
                if info.is_dir():
                    continue
                target = install_path(m["install"], info.filename)
                if target is None:
                    continue
                if target.lower() in targets:
                    raise AppError(f"{where}: two entries install to {target}")
                targets.add(target.lower())
                files += 1
                unpacked += info.file_size
            if not any(t in (f"apps/{m['id']}/boot.dol".lower(), f"apps/{m['id']}/boot.elf".lower()) for t in targets):
                raise AppError(f"{where}: the install puts no boot.dol or boot.elf in apps/{m['id']}/")
            if files > MAX_FILES or unpacked > MAX_UNPACKED:
                raise AppError(f"{where}: {files} files, {unpacked} bytes: over the Wii's limits")
            download.update({"files": files, "unpacked_size": unpacked})
            icon_entry = m.get("icon_in_zip")
            if not icon_entry:
                own = [n for n in z.namelist() if install_path(m["install"], n) == f"apps/{m['id']}/icon.png"]
                icon_entry = own[0] if own else None
            # Sized before it is read: a zip can claim a small file that
            # unpacks to gigabytes.
            icon = None
            if icon_entry and z.getinfo(icon_entry).file_size <= MAX_ICON:
                icon = z.read(icon_entry)
    else:
        icon = None
    record = {
        "id": m["id"], "name": m["name"], "author": m["author"], "version": version, "released": released,
        "category": m["category"], "description": m["description"], "page": m["page"],
        "platforms": m["platforms"], "download": download,
    }
    if not text_ok(version, LIMITS["version"]):
        raise AppError(f"{where}: version {version!r} is too long; set version in the manifest")
    for key in ("license", "newer", "data_folders"):
        if key in m:
            record[key] = m[key]
    if fmt == "zip":
        record["install"] = m["install"]
    if icon and len(icon) <= MAX_ICON and icon[:8] == b"\x89PNG\r\n\x1a\n":
        record["icon"] = ICON_BASE + m["id"] + ".png"
        if icons:
            with open(os.path.join(icons, m["id"] + ".png"), "wb") as f:
                f.write(icon)
    return record


def cmd_build(args, write=True):
    token = os.environ.get("GITHUB_TOKEN")
    out = getattr(args, "out", None)
    icons = os.path.join(out, "icons") if write else None
    if icons:
        os.makedirs(icons, exist_ok=True)
    apps, failures = [], []
    seen = set()
    for path, m in load_manifests(args.manifests):
        try:
            record = build_one(path, m, token, args.cache, icons)
        except (AppError, OSError, zipfile.BadZipFile, KeyError, ValueError) as e:
            failures.append(str(e))
            continue
        if record["id"].lower() in seen:
            failures.append(f"{record['id']}: a second app with this id")
            continue
        seen.add(record["id"].lower())
        apps.append(record)
    known = {a["id"] for a in apps}
    for a in apps:
        if "newer" in a and a["newer"] not in known:
            failures.append(f"{a['id']}: newer names {a['newer']}, which is not on the list")
    apps.sort(key=lambda a: (CATEGORIES.index(a["category"]), a["name"].lower()))
    listing = {"schema": SCHEMA, "apps": apps}
    text = json.dumps(listing, ensure_ascii=False, separators=(",", ":"))
    if write:
        with open(os.path.join(out, "apps.json"), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    if args.check_tool:
        # The Wii's own parser reads the list back: every app must stay.
        result = subprocess.run([args.check_tool], input=text.encode(), capture_output=True)
        print(result.stdout.decode(errors="replace").strip())
        if result.returncode != 0:
            failures.append("the Wii's parser left apps out: " + result.stderr.decode(errors="replace").strip())
    print(f"{len(apps)} app(s) listed, {len(failures)} problem(s)")
    for f in failures:
        print("  " + f, file=sys.stderr)
    return 1 if failures else 0


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cache", default=".app-cache")
    p.add_argument("--check-tool", help="build-host/app_list_check, to read the list back as the Wii does")
    sub = p.add_subparsers(dest="cmd", required=True)
    u = sub.add_parser("update")
    u.add_argument("manifests")
    u.add_argument("ids", nargs="*")
    b = sub.add_parser("build")
    b.add_argument("manifests")
    b.add_argument("out")
    c = sub.add_parser("check")
    c.add_argument("manifests")
    args = p.parse_args()
    if args.cmd == "update":
        cmd_update(args)
        return 0
    return cmd_build(args, write=args.cmd == "build")


if __name__ == "__main__":
    sys.exit(main())
