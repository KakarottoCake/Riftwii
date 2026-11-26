#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check approved save manifests and publish the Wii's pinned catalogue.

    py -3.13 tools/save_list.py pin manifests/example.json
    py -3.13 tools/save_list.py check manifests
    py -3.13 tools/save_list.py build manifests site

Pinning never changes approval. Example candidates can be checked with
`check --allow-pending`; manifest builds require a recorded human approval.
The GameBanana source follows the separate linking policy in docs/SAVES.md.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import urllib.request
import zipfile

MAX_DOWNLOAD = 32 << 20
MAX_UNPACKED = 64 << 20
MAX_FILES = 512
MAX_LIST = 1 << 20
FIELDS = (
    "id", "game_id", "platform", "title", "description", "author", "page",
    "format", "url", "size", "sha256", "files", "unpacked_size", "root", "member", "game_ids",
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def read_manifest(path):
    require(path.stat().st_size <= 16384, f"{path}: manifest exceeds 16 KB")
    with path.open(encoding="utf-8") as source:
        manifest = json.load(source, object_pairs_hook=unique_object)
    require(isinstance(manifest, dict), f"{path}: expected an object")
    require(manifest.get("id") == path.stem, f"{path}: id must match the file name")
    return manifest


def web_url(value):
    if not isinstance(value, str) or len(value.encode("utf-8")) >= 300:
        return False
    if not value.isascii():
        return False
    match = re.fullmatch(r"https://([A-Za-z0-9.-]+)/([^\s\\#\"<>]*)", value)
    if not match:
        return False
    host = match.group(1)
    labels = host.split(".")
    return (len(labels) >= 2 and all(0 < len(label) <= 63 and not label.startswith("-") and
            not label.endswith("-") for label in labels) and labels[-1].isalpha())


class HttpsRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        require(web_url(new_url), "a download redirected away from HTTPS")
        return super().redirect_request(request, response, code, message, headers, new_url)


def download(url):
    require(web_url(url), "download needs a plain HTTPS URL")
    request = urllib.request.Request(url, headers={"User-Agent": "RiftWii-save-list/1"})
    opener = urllib.request.build_opener(HttpsRedirects)
    with opener.open(request, timeout=30) as response:
        data = response.read(MAX_DOWNLOAD + 1)
    require(0 < len(data) <= MAX_DOWNLOAD, "download is empty or larger than 32 MB")
    return data


def local_asset(manifest, folder):
    name = manifest.get("file")
    require(isinstance(name, str) and re.fullmatch(r"files/[A-Za-z0-9_-]+\.(gci|zip|bin)", name),
            "local files must be files/<plain-id>.gci, .zip or .bin")
    root = folder.resolve()
    path = (root / name).resolve()
    require(path.is_relative_to(root), "local asset escapes the manifest folder")
    require(path.is_file() and 0 < path.stat().st_size <= MAX_DOWNLOAD, "local asset is missing or too large")
    return path


def payload(manifest, folder):
    if "file" in manifest:
        require("url" not in manifest, "use either file or url")
        return local_asset(manifest, folder).read_bytes()
    return download(manifest.get("url"))


def save_path(name):
    require(name.startswith("data/") and len(name) > 5, f"outside data/: {name}")
    relative = name[5:]
    parts = relative.split("/")
    require(len(parts) <= 8 and len(relative.encode("utf-8")) + 30 < 64, f"NAND path is too long: {name}")
    for part in parts:
        require(0 < len(part) <= 12 and not part.startswith(".") and not part.endswith((".", " ")),
                f"unsafe NAND name: {name}")
        require(all(32 <= ord(c) < 127 and c not in '\\:*?"<>|' for c in part), f"unsafe filename: {name}")
    return relative


def archive_path(name, directory=False):
    require(isinstance(name, str) and 0 < len(name.encode("utf-8")) < 200, "invalid archive path length")
    if directory:
        require(name.endswith("/"), "directory needs a trailing slash")
        name = name[:-1]
    parts = name.split("/")
    require(len(parts) <= 16, "archive path is too deep")
    for part in parts:
        require(part and not part.startswith(".") and not part.endswith((".", " ")), "unsafe archive path")
        require(all(32 <= ord(c) < 127 and c not in '\\:*?"<>|' for c in part), "unsafe archive filename")


def checked_zip(data):
    end = data.rfind(b"PK\x05\x06")
    require(end >= 0 and end + 22 <= len(data), "ZIP has no end record")
    disk, cd_disk, here, count, directory_size, directory_offset, comment = struct.unpack_from("<HHHHIIH", data, end + 4)
    require(disk == 0 and cd_disk == 0 and here == count, "split ZIP files are not supported")
    require(count != 0xFFFF and directory_size != 0xFFFFFFFF and directory_offset != 0xFFFFFFFF,
            "ZIP64 files are not supported")
    require(end + 22 + comment == len(data) and directory_offset + directory_size == end, "ZIP directory is outside the file")
    archive = zipfile.ZipFile(io.BytesIO(data))
    try:
        entries = archive.infolist()
        require(len(entries) <= MAX_FILES * 2, "ZIP has too many entries")
        names = set()
        regular = set()
        total = 0
        file_count = 0
        for entry in entries:
            require(entry.filename == entry.orig_filename, "ZIP path contains a null character")
            archive_path(entry.filename, entry.is_dir())
            name = entry.filename.rstrip("/").lower()
            require(name not in names, f"ZIP repeats a name: {entry.filename}")
            names.add(name)
            require(not entry.flag_bits & 1 and entry.compress_type in (0, 8), "ZIP is encrypted or uses an unsupported method")
            require(entry.compress_type != 0 or entry.compress_size == entry.file_size, "stored ZIP entry has different sizes")
            kind = (entry.external_attr >> 16) & 0o170000
            require(kind in (0, 0o040000, 0o100000), "ZIP contains a link or special file")
            require(entry.file_size < 0xFFFFFFFF and entry.header_offset < 0xFFFFFFFF and
                    entry.extract_version < 45, "ZIP64 files are not supported")
            if entry.is_dir():
                require(entry.file_size == 0, "ZIP directory holds file data")
            else:
                regular.add(name)
                file_count += 1
                total += entry.file_size
                require(total <= MAX_UNPACKED and file_count <= MAX_FILES, "save exceeds unpack limits")
        for name in names:
            parts = name.split("/")
            for end in range(1, len(parts)):
                require("/".join(parts[:end]) not in regular, "ZIP uses a file as a directory")
        return archive
    except Exception:
        archive.close()
        raise


def manifest_game_ids(manifest):
    primary = manifest.get("game_id", "")
    require(isinstance(primary, str) and re.fullmatch("[A-Z0-9]{6}", primary),
            "game_id must have six uppercase letters or digits")
    ids = manifest.get("game_ids", [primary])
    require(isinstance(ids, list) and 1 <= len(ids) <= 8 and primary in ids, "invalid game_ids")
    require(all(isinstance(value, str) and re.fullmatch("[A-Z0-9]{6}", value) for value in ids), "invalid game_ids")
    require(len(set(ids)) == len(ids), "duplicate game_ids")
    require(all(value[:3] == primary[:3] and value[4:] == primary[4:] for value in ids), "game_ids must name regions of one title")
    require(manifest.get("platform") != "gamecube" or ids == [primary], "GCI regions must match the header exactly")
    return ids


def inspect_payload(manifest, data):
    format_name = manifest.get("format")
    platform = manifest.get("platform")
    manifest_game_ids(manifest)
    game_id = manifest["game_id"]
    if format_name == "gci":
        require(platform == "gamecube" and "root" not in manifest, "GCI saves need platform gamecube and no root")
        if manifest.get("member"):
            with checked_zip(data) as archive:
                gcis = [entry for entry in archive.infolist() if not entry.is_dir() and entry.filename.lower().endswith(".gci")]
                require(len(gcis) == 1 and gcis[0].filename == manifest["member"], "ZIP must hold exactly the named GCI")
                require(8256 <= gcis[0].file_size <= 64 + 2043 * 8192, "GCI member is too large or too short")
                data = archive.read(gcis[0])
        require(len(data) >= 8256 and data[:6] == game_id.encode("ascii"), "GCI belongs to a different game or is too short")
        blocks = struct.unpack_from(">H", data, 0x38)[0]
        require(0 < blocks <= 2043 and len(data) == 64 + blocks * 8192, "GCI block count and size disagree")
        return {}
    require(platform == "wii" and "member" not in manifest, "Wii saves need platform wii and no member")
    if format_name == "data.bin":
        require("root" not in manifest, "data.bin does not use root")
        # Listed for a PC only. No decryption, key or claim of in-game validity.
        return {}
    require(format_name == "wii-zip", "unknown save format")
    root = manifest.get("root", "data/")
    require(isinstance(root, str) and len(root.encode("utf-8")) < 128, "invalid root")
    if root:
        archive_path(root, True)
    files = 0
    unpacked = 0
    banner = False
    with checked_zip(data) as archive:
        for entry in archive.infolist():
            name = entry.filename
            if entry.is_dir() and name == root:
                continue
            if not name.startswith(root):
                require("root" in manifest, f"outside data/: {name}")
                continue
            relative = name[len(root):].rstrip("/") if entry.is_dir() else name[len(root):]
            save_path("data/" + relative)
            if entry.is_dir():
                continue
            files += 1
            unpacked += entry.file_size
            require(files <= MAX_FILES and unpacked <= MAX_UNPACKED, "save exceeds unpack limits")
            contents = archive.read(entry)
            require(len(contents) == entry.file_size, "ZIP entry has the wrong size")
            if relative == "banner.bin":
                require(contents.startswith(b"WIBN"), "banner.bin has no Wii banner header")
                banner = True
    require(banner and files > 0 and unpacked > 0, "ZIP needs banner.bin and save files under root")
    return {"files": files, "unpacked_size": unpacked}


def catalogue_entry(manifest, folder, base_url, allow_pending=False):
    require(re.fullmatch(r"[A-Za-z0-9_-]{1,47}", manifest.get("id", "")), "invalid save id")
    for key, limit in (("title", 80), ("author", 64), ("description", 240)):
        text = manifest.get(key)
        require(isinstance(text, str) and 0 < len(text.encode("utf-8")) < limit and text == text.strip() and
                all(ord(c) >= 32 and ord(c) != 127 for c in text), f"invalid {key}")
    require(web_url(manifest.get("page")), "page must be HTTPS")
    approval = manifest.get("approval", {})
    require(isinstance(approval, dict), "approval must be an object")
    if not allow_pending:
        require(approval.get("approved") is True and web_url(approval.get("evidence")) and
                isinstance(approval.get("reviewer"), str) and approval["reviewer"].strip(),
                "human review and permission evidence are required before publication")
    data = payload(manifest, folder)
    require(type(manifest.get("size")) is int and len(data) == manifest["size"], "pinned size does not match")
    require(re.fullmatch("[a-f0-9]{64}", manifest.get("sha256", "")) and
            hashlib.sha256(data).hexdigest() == manifest["sha256"], "pinned SHA-256 does not match")
    counts = inspect_payload(manifest, data)
    for key in ("files", "unpacked_size"):
        require(type(manifest.get(key, 0)) is int and manifest.get(key, 0) == counts.get(key, 0),
                f"pinned {key} does not match")
    entry = {key: manifest[key] for key in FIELDS if key in manifest}
    if "file" in manifest:
        entry["url"] = base_url + manifest["file"]
    require(web_url(entry.get("url")), "published URL is invalid")
    return entry


def native_check(text, executable):
    if executable:
        subprocess.run([str(executable)], input=text, check=True)


def run(args):
    if args.command == "pin":
        path = args.manifest
        manifest = read_manifest(path)
        data = payload(manifest, path.parent)
        counts = inspect_payload(manifest, data)
        manifest.update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        manifest.pop("files", None)
        manifest.pop("unpacked_size", None)
        manifest.update(counts)
        path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print(f"Pinned {manifest['id']}; approval unchanged")
        return
    if args.command == "gamebanana":
        from save_gamebanana import build
        build(args)
        return
    require(web_url(args.base_url + "saves.json") and args.base_url.endswith("/"), "invalid site base URL")
    paths = sorted(args.manifests.glob("*.json"))
    require(0 < len(paths) <= 512, "expected between 1 and 512 manifest files")
    entries = []
    assets = []
    ids = set()
    for path in paths:
        manifest = read_manifest(path)
        require(manifest["id"].lower() not in ids, f"duplicate id: {manifest['id']}")
        ids.add(manifest["id"].lower())
        entry = catalogue_entry(manifest, path.parent, args.base_url,
                                allow_pending=args.command == "check" and args.allow_pending)
        entries.append(entry)
        if "file" in manifest:
            assets.append((local_asset(manifest, path.parent), manifest["file"]))
        print(f"Checked {manifest['id']}")
    text = (json.dumps({"schema": 1, "saves": entries}, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
    require(len(text) <= MAX_LIST, "published list exceeds 1 MB")
    native_check(text, args.check_tool)
    if args.command == "build":
        args.output.mkdir(parents=True, exist_ok=True)
        for source, name in assets:
            target = args.output / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        staged = args.output / "saves.json.tmp"
        staged.write_bytes(text)
        staged.replace(args.output / "saves.json")
        print(f"Published {len(entries)} saves to {args.output}")


def main():
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check-tool", type=Path, help="host save_list_check executable; use this in CI")
    parser.add_argument("--base-url", default="https://riftwii.com/saves/")
    commands = parser.add_subparsers(dest="command", required=True)
    pin = commands.add_parser("pin")
    pin.add_argument("manifest", type=Path)
    for command in ("check", "build"):
        sub = commands.add_parser(command)
        sub.add_argument("manifests", type=Path)
        if command == "build":
            sub.add_argument("output", type=Path)
        else:
            sub.add_argument("--allow-pending", action="store_true", help="validate example candidates without approving them")
    gamebanana = commands.add_parser("gamebanana", help="index supported saves hosted by GameBanana")
    gamebanana.add_argument("output", type=Path)
    gamebanana.add_argument("--cache", type=Path, default=Path("build-host/save-gamebanana-cache"))
    gamebanana.add_argument("--refresh", action="store_true", help="refresh cached API pages; immutable file downloads stay cached")
    args = parser.parse_args()
    try:
        run(args)
    except (ValueError, OSError, KeyError, TypeError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        print(f"Save list: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
