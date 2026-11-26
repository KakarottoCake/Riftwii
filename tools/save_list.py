#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check approved save manifests and publish the Wii's pinned catalogue.

    py -3.13 tools/save_list.py pin manifests/example.json
    py -3.13 tools/save_list.py check manifests
    py -3.13 tools/save_list.py build manifests site

Pinning never changes approval. Example candidates can be checked with
`check --allow-pending`; build always requires a recorded human approval.
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
    "format", "url", "size", "sha256", "files", "unpacked_size",
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


def inspect_payload(manifest, data):
    format_name = manifest.get("format")
    platform = manifest.get("platform")
    game_id = manifest.get("game_id", "")
    require(isinstance(game_id, str) and re.fullmatch("[A-Z0-9]{6}", game_id), "game_id must have six uppercase letters or digits")
    if format_name == "gci":
        require(platform == "gamecube", "GCI saves need platform gamecube")
        require(len(data) >= 8256 and data[:6] == game_id.encode("ascii"), "GCI belongs to a different game or is too short")
        blocks = struct.unpack_from(">H", data, 0x38)[0]
        require(0 < blocks <= 2043 and len(data) == 64 + blocks * 8192, "GCI block count and size disagree")
        return {}
    require(platform == "wii", "Wii saves need platform wii")
    if format_name == "data.bin":
        # Listed for a PC only. No decryption, key or claim of in-game validity.
        return {}
    require(format_name == "wii-zip", "unknown save format")
    files = 0
    unpacked = 0
    names = set()
    file_names = set()
    banner = False
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries = archive.infolist()
        require(len(entries) <= MAX_FILES * 2, "ZIP has too many entries")
        for entry in entries:
            name = entry.filename.rstrip("/") if entry.is_dir() else entry.filename
            if name != "data" or not entry.is_dir():
                save_path(name)
            folded = name.lower()
            require(folded not in names, f"ZIP repeats a name: {name}")
            names.add(folded)
            require(not entry.flag_bits & 1 and entry.compress_type in (0, 8), "ZIP is encrypted or uses an unsupported method")
            # Reject symlinks and device files, even though the Wii's writer
            # would only create ordinary files from their bytes.
            kind = (entry.external_attr >> 16) & 0o170000
            require(kind in (0, 0o040000, 0o100000), "ZIP contains a link or special file")
            if entry.is_dir():
                require(entry.file_size == 0, "ZIP directory holds file data")
                continue
            files += 1
            unpacked += entry.file_size
            file_names.add(folded)
            require(files <= MAX_FILES and unpacked <= MAX_UNPACKED, "save exceeds unpack limits")
            # Reading checks CRC and the actual expansion, not only metadata.
            contents = archive.read(entry)
            require(len(contents) == entry.file_size, "ZIP entry has the wrong size")
            if name == "data/banner.bin":
                require(contents.startswith(b"WIBN"), "banner.bin has no Wii banner header")
                banner = True
        for name in names:
            parts = name.split("/")
            for end in range(1, len(parts)):
                require("/".join(parts[:end]) not in file_names, "ZIP uses a file as a directory")
    require(banner and files > 0 and unpacked > 0, "ZIP needs data/banner.bin and save files")
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
    args = parser.parse_args()
    try:
        run(args)
    except (ValueError, OSError, KeyError, TypeError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        print(f"Save list: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
