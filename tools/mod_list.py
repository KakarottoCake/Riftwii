# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the Rift Store's list of Riivolution mods (mods.json,
docs/MODS.md) from GameBanana.

  mod_list.py build <out> [--exclude F]     find Wii mods on GameBanana, keep
                                              the ones whose download really
                                              is a Riivolution pack, write
                                              <out>/mods.json and
                                              <out>/icons/<id>.png

--exclude names a file of GameBanana mod ids to leave out, one per line
(# starts a comment): mods whose authors asked not to be listed.

GameBanana hosts the mods; RiftWii only lists them. Every listed file was
downloaded here once and pinned by its SHA-256, so the Wii installs exactly
what was checked, from GameBanana's own servers. A zip's file list is read
first from its last 64 KiB (an HTTP range request), so only real
Riivolution packs are downloaded whole. 7z and rar files are listed when
the 7z command can read them; the Wii can't unpack those, so the store
shows them as "Get it on a PC".

Everything learned is cached in --cache (default .mod-cache): a mod's
page until GameBanana says it changed, a file's verdict for good (a
GameBanana file never changes; a new version is a new file). The rules
follow src/net/applist.cpp (the Wii's parser, which reads mods.json too);
--check-tool runs that parser over the list, as app_list.py does.
"""
import argparse
import hashlib
import html
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile
import zlib
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import app_list  # noqa: E402  (the shared rules: text, paths, folders, icons)

SCHEMA = 1
API = "https://gamebanana.com/apiv11/"
ICON_BASE = "https://riftwii.github.io/mods/icons/"
USER_AGENT = "RiftWii-mod-list/1 (+https://riftwii.com)"
# GameBanana asks clients to be gentle: one request at a time, with a pause.
PAUSE = 0.35
MAX_DOWNLOAD = app_list.MAX_DOWNLOAD       # what the Wii takes (128 MB)
MAX_LISTED_OTHER = 64 << 20                # 7z/rar downloaded to look inside, at most
MAX_DESCRIPTION = 159
MAX_API = 8 << 20          # one GameBanana API answer (a page of 50 mods is ~100 KB)
MAX_PACK_XMLS = 64         # Riivolution XMLs read from one archive
MAX_PACK_XML_BYTES = 4 << 20  # and all of them together
MAX_PROGRAM = 95           # AppListing::program is char[96]
MAX_GAMES = 4
# A game with more mods than this (Mario Kart Wii, Brawl: thousands, nearly
# all single-file replacements) has only the mods a search for Riivolution
# finds looked at; reading every one would take a night. Smaller games have
# every mod looked at.
BIG_GAME = 600
# Only precise words: "riivolution xml" or "riivolution pack" match about a
# thousand loosely related mods each, and their deep pages come back empty.
SEARCHES = ["riivolution", "riivo"]
MAX_ROOT = 127  # AppListing::root, the folder in the archive that holds the pack
# The Wii games whose GameBanana mods are looked at, besides every game a
# "riivolution" search finds. Names as GameBanana writes them.
SEED_GAMES = [
    "New Super Mario Bros. Wii", "Super Mario Galaxy", "Super Mario Galaxy 2", "Mario Kart Wii",
    "Super Smash Bros. Brawl", "Wii Sports", "Wii Sports Resort", "Kirby's Return to Dream Land",
    "Super Paper Mario", "Donkey Kong Country Returns", "Mario Party 8", "Mario Party 9",
    "The Legend of Zelda: Twilight Princess", "The Legend of Zelda: Skyward Sword", "Xenoblade Chronicles",
    "Metroid Prime 3: Corruption", "Metroid: Other M", "Sonic Colors", "Punch-Out!!", "Wii Party",
    "Mario Strikers Charged", "Mario & Sonic at the Olympic Games", "Kirby's Epic Yarn",
    "Super Mario Sunshine", "Wii Play", "Animal Crossing: City Folk", "Fire Emblem: Radiant Dawn",
    "Pikmin 2", "Mario Super Sluggers", "Wii Fit",
]
RIIVOLUTION_XML = re.compile(r"(?:^|/)riivolution/[^/]+\.xml$", re.IGNORECASE)
GAME_ID = re.compile(r"^[A-Z0-9]{3,6}$")
SKIP_TOP = {"__macosx", ".ds_store"}


class Skip(Exception):
    """This file or mod is not listed (the reason goes to the log)."""


# ------------------------------------------------------------------ web ---

class Web:
    def __init__(self, cache):
        self.cache = cache
        self.last = 0.0
        self.requests = 0

    def _wait(self):
        delay = self.last + PAUSE - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        self.last = time.monotonic()
        self.requests += 1

    def open(self, url, headers=None, tries=4):
        for attempt in range(tries):
            self._wait()
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, **(headers or {})})
            try:
                return urllib.request.urlopen(req, timeout=60)
            except urllib.error.HTTPError as e:
                if e.code in (429, 500, 502, 503, 504) and attempt + 1 < tries:
                    time.sleep(5 * (attempt + 1))
                    continue
                raise
            except (urllib.error.URLError, TimeoutError, ConnectionError):
                if attempt + 1 < tries:
                    time.sleep(5 * (attempt + 1))
                    continue
                raise

    def api(self, path, **params):
        """GameBanana's answer as JSON. An answer that isn't JSON (GameBanana
        sometimes sends an empty one) is asked again, then raises Skip."""
        query = urllib.parse.urlencode(params)
        for attempt in range(3):
            with self.open(API + path + ("?" + query if query else "")) as r:
                body = r.read(MAX_API + 1)
            if len(body) > MAX_API:
                raise Skip(f"GameBanana's answer to {path} is over {MAX_API} bytes")
            try:
                return json.loads(body)
            except ValueError:
                time.sleep(5 * (attempt + 1))
        raise Skip(f"GameBanana's answer to {path} was not JSON")

    def tail(self, url, size, count=65536):
        """The last `count` bytes of a file, by a range request."""
        start = max(0, size - count)
        with self.open(url, {"Range": f"bytes={start}-{size - 1}"}) as r:
            data = r.read(count + 1)
            if r.status == 200:  # no ranges: the whole file came
                return data[start:] if len(data) == size else None
            return data

    def download(self, url, limit):
        with self.open(url) as r:
            data = r.read(limit + 1)
        if len(data) > limit:
            raise Skip(f"bigger than {limit} bytes")
        return data


# ------------------------------------------------------------ archives ---

def zip_names_from_tail(tail, size):
    """The entry names of a zip from its last bytes (its central directory),
    or None when they are not all there (a long comment, zip64)."""
    end = tail.rfind(b"PK\x05\x06")
    if end < 0 or end + 22 > len(tail):
        return None
    _disk, _cd_disk, _n_disk, count, cd_size, cd_offset, _comment = struct.unpack_from("<HHHHIIH", tail, end + 4)
    if count == 0xFFFF or cd_offset == 0xFFFFFFFF:
        return None
    base = size - len(tail)
    start = cd_offset - base
    if start < 0 or start + cd_size > len(tail):
        return None
    names, at, end = [], start, start + cd_size
    for _ in range(count):
        # A record's fixed 46 bytes, then its name, extra and comment, all
        # inside the directory: a damaged one is "can't tell", not a crash.
        if at + 46 > end or tail[at:at + 4] != b"PK\x01\x02":
            return None
        flags = struct.unpack_from("<H", tail, at + 8)[0]
        name_len, extra_len, comment_len = struct.unpack_from("<HHH", tail, at + 28)
        if at + 46 + name_len + extra_len + comment_len > end:
            return None
        raw = tail[at + 46:at + 46 + name_len]
        names.append(raw.decode("utf-8" if flags & 0x800 else "cp437", "replace"))
        at += 46 + name_len + extra_len + comment_len
    return names


def seven_zip():
    for name in ("7z", "7zz", "7za"):
        found = shutil.which(name)
        if found:
            return found
    windows = r"C:\Program Files\7-Zip\7z.exe"
    return windows if os.path.exists(windows) else None


MAX_LISTING = 4 << 20   # 7z's listing of one archive, at most
MAX_XML = 1 << 20       # one Riivolution XML, at most


def run_capped(args, limit):
    """A command's output, or None when it fails or writes more than
    `limit` bytes (the command is stopped there: a small archive can unpack
    to gigabytes)."""
    child = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    out = bytearray()
    try:
        while True:
            chunk = child.stdout.read(65536)
            if not chunk:
                break
            out += chunk
            if len(out) > limit:
                child.kill()
                return None
    finally:
        child.stdout.close()
        child.wait()
    return bytes(out) if child.returncode == 0 else None


def checked_program(name_in_riivolution):
    """The pack's XML as the list names it ("riivolution/<name>"), refused
    when it is longer than the Wii's field holds (it would skip the mod)."""
    program = "riivolution/" + name_in_riivolution
    if len(program.encode()) > MAX_PROGRAM:
        raise Skip(f"its XML's name is too long for the Wii ({program})")
    return program


def read_pack_xmls(names, read):
    """The text of each XML, by `read(name)` (bytes, or None when it is over
    MAX_XML or unreadable). One XML that can't be read whole rejects the
    archive: its game IDs would be missing from the list."""
    if len(names) > MAX_PACK_XMLS:
        raise Skip(f"{len(names)} Riivolution XMLs (at most {MAX_PACK_XMLS} are read)")
    texts = {}
    total = 0
    for name in names:
        text = read(name)
        if text is None or len(text) > MAX_XML:
            raise Skip(f"its XML {name} is over {MAX_XML} bytes or unreadable")
        total += len(text)
        if total > MAX_PACK_XML_BYTES:
            raise Skip(f"its XMLs are over {MAX_PACK_XML_BYTES} bytes together")
        texts[name] = text
    return texts


def other_names_and_xml(data, ext):
    """Entry names of a 7z or rar download, and the text of its Riivolution
    XMLs, through the 7z command."""
    tool = seven_zip()
    if not tool:
        raise Skip(f"no 7z command to read a .{ext}")
    with tempfile.TemporaryDirectory() as folder:
        path = os.path.join(folder, "mod." + ext)
        with open(path, "wb") as f:
            f.write(data)
        listing = run_capped([tool, "l", "-slt", "-ba", path], MAX_LISTING)
        if listing is None:
            raise Skip(f"7z could not read the .{ext}")
        text = listing.decode("utf-8", "replace")
        names = [line[7:].replace("\\", "/") for line in text.splitlines() if line.startswith("Path = ")]
        wanted = [name for name in names if RIIVOLUTION_XML.search(name)]
        xmls = read_pack_xmls(wanted, lambda name: run_capped([tool, "e", "-so", path, name], MAX_XML))
        return names, xmls


# ----------------------------------------------------------- the mods ---

def plain_text(text, limit):
    """GameBanana's HTML description as one plain line of at most `limit`
    bytes, cut at a word."""
    text = re.sub(r"<(br|/p|/div|/li)[^>]*>", " ", text or "", flags=re.IGNORECASE)
    text = html.unescape(re.sub(r"<[^>]+>", "", text))
    text = "".join(c for c in text if c.isprintable() or c == " ")
    text = re.sub(r"\s+", " ", text).strip()
    return cut(text, limit)


def cut(text, limit):
    if len(text.encode("utf-8")) <= limit:
        return text
    words, out = text.split(" "), ""
    for word in words:
        trial = (out + " " + word).strip()
        if len((trial + "...").encode("utf-8")) > limit:
            break
        out = trial
    if not out:  # one very long word
        out = text.encode("utf-8")[:limit - 3].decode("utf-8", "ignore")
    return out.rstrip(" ,.;:") + "..."


def game_ids(xml_texts):
    """The game IDs the packs' XMLs name (<id game="SMN">), at most MAX_GAMES."""
    found = []
    for text in xml_texts:
        for match in re.finditer(rb"<id\b[^>]*\bgame\s*=\s*[\"']([^\"']+)[\"']", text):
            gid = match.group(1).decode("ascii", "ignore").strip().upper()
            if GAME_ID.match(gid) and gid not in found:
                found.append(gid)
    return found[:MAX_GAMES]


def pack_layout(names):
    """Where the pack is in the archive: the folder holding riivolution/
    ("" or "Newer/"), its XMLs, and the folders beside riivolution/ that
    it brings (installed as they are at the card's root)."""
    xmls = sorted(n for n in names if RIIVOLUTION_XML.search(n))
    if not xmls:
        raise Skip("no riivolution/*.xml")
    lowered = xmls[0].lower()
    root = xmls[0][:lowered.rfind("riivolution/")]
    # Every XML must be in the same riivolution/ folder.
    xmls = [x for x in xmls if x.startswith(root)]
    riiv = xmls[0][len(root):len(root) + len("riivolution/")]
    folders = []
    for name in names:
        if not name.startswith(root):
            continue
        rest = name[len(root):]
        if "/" not in rest:
            continue  # a readme beside the folders: not installed
        top = rest.split("/", 1)[0]
        if top.lower() == "riivolution" or top.lower() in SKIP_TOP or top in folders:
            continue
        folders.append(top)
    return root, riiv, xmls, folders


def unpacked(names_sizes, root, tops):
    files = total = 0
    for name, size in names_sizes:
        if name.endswith("/") or not name.startswith(root):
            continue
        rest = name[len(root):]
        if "/" in rest and rest.split("/", 1)[0] in tops:
            files += 1
            total += size
    return files, total


def make_icon(image_bytes):
    """A 128x48 PNG (the Homebrew Channel's icon size the store shows) from
    the mod's first picture, cropped to fill, or None."""
    try:
        from PIL import Image  # noqa: PLC0415 (Pillow is needed for icons only)
    except ImportError:
        return None
    try:
        with Image.open(io.BytesIO(image_bytes)) as picture:
            picture = picture.convert("RGB")
            scale = max(128 / picture.width, 48 / picture.height)
            w, h = max(128, round(picture.width * scale)), max(48, round(picture.height * scale))
            picture = picture.resize((w, h), Image.LANCZOS)
            left, top = (w - 128) // 2, (h - 48) // 2
            picture = picture.crop((left, top, left + 128, top + 48))
            for colors in (None, 256, 64):
                out = io.BytesIO()
                (picture if colors is None else picture.quantize(colors)).save(out, "PNG", optimize=True)
                if out.tell() <= app_list.MAX_ICON:
                    return out.getvalue()
    except Exception:  # noqa: BLE001 a picture we can't read: no icon
        return None
    return None


# The rules a listed file passed. A file's verdict is cached for good (the
# file never changes), so when the rules for listing one change, this goes
# up and the files listed under older rules are looked at again. 2: the XML
# path is always under "riivolution/", and an XML too big to read whole
# rejects its archive. 3: the XML's path fits the Wii's 96-byte field.
# A "skip" stays: a file that wasn't a pack still isn't.
VERDICT_VERSION = 3


def cached_verdict_ok(verdict):
    """Whether a cached verdict can be used as it is."""
    return "skip" in verdict or verdict.get("v", 0) >= VERDICT_VERSION


def zip_layout(data):
    """A pack zip's layout as the Wii will install it, or Skip."""
    z = zipfile.ZipFile(io.BytesIO(data))
    infos = [i for i in z.infolist()]
    names = [i.filename for i in infos]
    for name in names:
        parts = [p for p in name.split("/") if p]
        if any(not app_list.part_ok(p) for p in parts):
            raise Skip(f"an entry the Wii won't write: {name!r}")
    root, riiv, xml_names, tops = pack_layout(names)
    if len(tops) > 4:
        raise Skip(f"{len(tops)} folders beside riivolution/ (the store takes 4)")
    for top in tops:
        if not app_list.data_folder_ok(top):
            raise Skip(f"its folder {top!r} is one RiftWii or the Wii owns")
    files, total = unpacked([(i.filename, i.file_size) for i in infos], root, set(tops) | {riiv[:-1]})
    if files == 0 or files > app_list.MAX_FILES or total > app_list.MAX_UNPACKED:
        raise Skip(f"{files} files, {total} bytes")

    def read_xml(name):
        if z.getinfo(name).file_size > MAX_XML:
            return None
        try:
            return z.read(name)
        except (NotImplementedError, RuntimeError, zipfile.BadZipFile, zlib.error, EOFError):
            return None  # an unsupported method, encrypted, damaged: the archive is refused

    xml_texts = list(read_pack_xmls(xml_names, read_xml).values())
    program = checked_program(xml_names[0][len(root) + len(riiv):])
    return {"v": VERDICT_VERSION, "format": "zip", "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "root": root, "riivolution": riiv, "program": program,
            "folders": tops, "games": game_ids(xml_texts), "files": files, "unpacked": total}


HOSTED_URL = re.compile(r"https://(github\.com/RiftWii/mods/releases/download/files/[\w.-]+\.zip"
                        r"|github\.com/[\w.-]+/[\w.-]+/releases/download/[^/]+/[^/]+\.zip"
                        r"|[\w-]+\.github\.io/.+\.zip)$")


def read_hosted(path):
    """The rehosted mods (RiftWii/mods' hosted.json): each a dict with id,
    name, makers ("A; B; C"), game, page, released, url, and optionally
    picture, description, version and replaces (GameBanana mod ids the
    entry stands in for)."""
    if not path:
        return []
    with open(path, encoding="utf-8") as f:
        items = json.load(f)
    for item in items:
        for key in ("id", "name", "makers", "game", "page", "released", "url"):
            if not isinstance(item.get(key), str) or not item[key]:
                raise SystemExit(f"hosted.json: {item.get('id', '?')} has no {key}")
        if not app_list.https_url_ok(item["url"], app_list.LIMITS["url"]) or not item["id"].startswith("rw-"):
            raise SystemExit(f"hosted.json: {item['id']} needs an rw- id and an https URL")
        # Only RiftWii/mods' own release, or a maker's own GitHub release or
        # GitHub Pages site (two mods are linked where their makers put
        # them): a slip in hosted.json can't send the Wii anywhere else.
        if not HOSTED_URL.match(item["url"]):
            raise SystemExit(f"hosted.json: {item['id']}'s url is not RiftWii/mods' release or a maker's GitHub")
        replaces = item.get("replaces", [])
        if not isinstance(replaces, list) or not all(isinstance(i, int) and i > 0 for i in replaces):
            raise SystemExit(f"hosted.json: {item['id']}'s replaces is not a list of GameBanana mod ids")
        # The page as the Wii reads it (an archive.org link, with its "http://"
        # inside, is one it refuses): the original page or Luma's Workshop's.
        if not app_list.https_url_ok(item["page"], app_list.LIMITS["page"]):
            raise SystemExit(f"hosted.json: {item['id']}'s page is not a link the Wii takes")
    return items


def hosted_entry(item, v, log):
    """A rehosted mod's entry: its makers credited (the author field takes as
    many as fit, then "and others"; the description names them all)."""
    makers = [m.strip() for m in item["makers"].split(";") if m.strip()]
    author = ""
    for count in range(len(makers), 0, -1):
        candidate = ", ".join(makers[:count]) + ("" if count == len(makers) else " and others")
        if len(candidate.encode()) <= app_list.LIMITS["author"]:
            author = candidate
            break
    author = author or cut(makers[0], app_list.LIMITS["author"])
    description = item.get("description") or ("By " + ", ".join(makers) + ".")
    e = {
        "id": item["id"],
        "kind": "mod",
        "name": cut(item["name"], app_list.LIMITS["name"]),
        "author": author,
        "version": cut(item.get("version") or item["released"], app_list.LIMITS["version"]),
        "released": item["released"],
        "category": "mods",
        "game": cut(item["game"], app_list.LIMITS["author"]),
        "games": v.get("games", []),
        "description": cut(plain_text(description, 2000), MAX_DESCRIPTION),
        "page": item["page"],
        "platforms": ["wii", "vwii"],
        "download": {"url": item["url"], "format": "zip", "size": v["size"], "sha256": v["sha256"],
                     "files": v["files"], "unpacked_size": v["unpacked"]},
        "program": v["program"],
        "data_folders": v["folders"],
    }
    if v["root"]:
        if len(v["root"].encode()) > MAX_ROOT:
            log(f"  {item['name']}: its folder in the archive is too long ({v['root']})")
            return None
        e["root"] = v["root"]
    rules = [{"from": v["riivolution"], "to": "riivolution/"}]
    rules += [{"from": top + "/", "to": top + "/"} for top in v["folders"]]
    for rule in rules:
        if len(rule["from"].encode()) > app_list.LIMITS["rule"]:
            log(f"  {item['name']}: a path too long for the store ({rule['from']})")
            return None
    e["install"] = rules
    return e


class Builder:
    def __init__(self, web, cache, log):
        self.web = web
        self.cache = cache
        self.log = log
        self.state_path = os.path.join(cache, "state.json")
        self.state = {"mods": {}, "files": {}}
        if os.path.exists(self.state_path):
            with open(self.state_path, encoding="utf-8") as f:
                self.state = json.load(f)

    def save_state(self):
        tmp = self.state_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.state, f)
        os.replace(tmp, self.state_path)

    # -- finding games and mods --

    def find_game(self, name):
        try:
            found = self.web.api("Util/Search/Results", _sModelName="Game", _sSearchString=name, _nPage=1)
        except (Skip, urllib.error.URLError, OSError):
            return None
        for record in found.get("_aRecords", []):
            if record.get("_sName", "").lower() == name.lower():
                return record["_idRow"], record["_sName"]
        return None

    def riivolution_search(self):
        """The games of every mod a search for Riivolution finds, and those
        mods by game."""
        games, mods = {}, {}
        for words in SEARCHES:
            page = 1
            while page <= 40:
                try:
                    found = self.web.api("Util/Search/Results", _sModelName="Mod", _sSearchString=words,
                                         _nPage=page)
                except (Skip, urllib.error.URLError, OSError):
                    break  # an empty or failed page ends this search, not the build
                records = found.get("_aRecords", [])
                for record in records:
                    game = record.get("_aGame") or {}
                    if game.get("_idRow"):
                        # Only the plain search names games: the others match
                        # loosely ("xml", "pack") and would bring in PC games.
                        if words == SEARCHES[0]:
                            games[game["_idRow"]] = game.get("_sName", "")
                        mods.setdefault(game["_idRow"], {})[record["_idRow"]] = record
                if found.get("_aMetadata", {}).get("_bIsComplete") or not records:
                    break
                page += 1
        return games, mods

    def game_mods(self, game_id):
        mods, page = [], 1
        while page <= 400:
            found = self.web.api("Mod/Index", _nPerpage=50, _nPage=page, **{"_aFilters[Generic_Game]": game_id})
            records = found.get("_aRecords", [])
            mods.extend(records)
            if found.get("_aMetadata", {}).get("_bIsComplete") or not records:
                break
            page += 1
        return mods

    # -- one mod --

    def profile(self, record):
        key = str(record["_idRow"])
        stamp = record.get("_tsDateModified") or record.get("_tsDateUpdated") or 0
        cached = self.state["mods"].get(key)
        if cached and cached.get("stamp") == stamp:
            return cached["profile"]
        page = self.web.api(f"Mod/{key}/ProfilePage")
        keep = {k: page.get(k) for k in ("_idRow", "_sName", "_sText", "_sVersion", "_sLicense", "_sProfileUrl",
                                           "_bIsPrivate", "_bIsTrashed", "_bIsWithheld", "_bIsObsolete",
                                           "_sInitialVisibility", "_tsDateAdded", "_tsDateModified")}
        keep["_aSubmitter"] = {"_sName": (page.get("_aSubmitter") or {}).get("_sName", "")}
        keep["_aGame"] = {k: (page.get("_aGame") or {}).get(k) for k in ("_idRow", "_sName")}
        keep["_aFiles"] = page.get("_aFiles") or []
        images = ((page.get("_aPreviewMedia") or {}).get("_aImages") or [])
        keep["_sPicture"] = (images[0]["_sBaseUrl"] + "/" + images[0]["_sFile"]) if images else ""
        self.state["mods"][key] = {"stamp": stamp, "profile": keep}
        return keep

    def file_verdict(self, f):
        """What a download is: a dict with the pack's layout, or a "skip"."""
        key = str(f["_idRow"])
        if key in self.state["files"] and not cached_verdict_ok(self.state["files"][key]):
            del self.state["files"][key]  # listed under older rules: looked at again
        if key in self.state["files"]:
            return self.state["files"][key]
        try:
            verdict = self.look_inside(f)
        except Skip as why:
            verdict = {"skip": str(why)}
        except (urllib.error.URLError, OSError, zipfile.BadZipFile, ValueError) as e:
            return {"skip": f"not reachable now: {e}"}  # not cached: tried again next time
        self.state["files"][key] = verdict
        return verdict

    def look_inside(self, f):
        name, size = f.get("_sFile", ""), int(f.get("_nFilesize") or 0)
        ext = name.lower().rsplit(".", 1)[-1] if "." in name else ""
        if f.get("_sAvResult") not in ("clean", None):
            raise Skip(f"GameBanana's scan said {f.get('_sAvResult')}")
        if size <= 0 or size > MAX_DOWNLOAD:
            raise Skip(f"size {size}")
        url = f["_sDownloadUrl"]
        if ext == "zip":
            tail = self.web.tail(url, size)
            names = zip_names_from_tail(tail, size) if tail else None
            if names is not None and not any(RIIVOLUTION_XML.search(n) for n in names):
                raise Skip("no riivolution/*.xml in the zip")
            data = self.web.download(url, MAX_DOWNLOAD)
            return self.zip_verdict(data, f)
        if ext in ("7z", "rar"):
            if size > MAX_LISTED_OTHER:
                raise Skip(f"a .{ext} too big to look inside")
            data = self.web.download(url, MAX_LISTED_OTHER)
            names, xmls = other_names_and_xml(data, ext)
            root, riiv, xml_names, tops = pack_layout(names)
            return {"v": VERDICT_VERSION, "format": ext, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                    "root": root, "riivolution": riiv, "program": checked_program(xml_names[0][len(root) + len(riiv):]),
                    "folders": tops, "games": game_ids(xmls.values())}
        raise Skip(f"a .{ext} file")

    def zip_verdict(self, data, f):
        if len(data) != int(f.get("_nFilesize") or 0):
            raise Skip("the download's size is not GameBanana's")
        md5 = f.get("_sMd5Checksum")
        if md5 and hashlib.md5(data).hexdigest() != md5.lower():
            raise Skip("the download's MD5 is not GameBanana's")
        return zip_layout(data)

    # -- mods in RiftWii/mods' own release (hosted.json) --

    def hosted_entries(self, hosted):
        """The rehosted mods' entries: each zip downloaded once (its verdict
        cached by URL; a new file gets a new URL) and checked as a
        GameBanana one is."""
        cache = self.state.setdefault("hosted", {})
        out = []
        for item in hosted:
            verdict = cache.get(item["url"])
            if verdict is None or not cached_verdict_ok(verdict):
                try:
                    data = self.web.download(item["url"], MAX_DOWNLOAD)
                    verdict = zip_layout(data)
                    verdict.update({"v": VERDICT_VERSION})
                except Skip as why:
                    verdict = {"skip": str(why)}
                except (urllib.error.URLError, OSError, zipfile.BadZipFile, ValueError) as e:
                    self.log(f"  {item['name']}: not reachable now ({e})")
                    continue
                cache[item["url"]] = verdict
            if "skip" in verdict:
                self.log(f"  {item['name']}: {verdict['skip']}")
                continue
            entry = hosted_entry(item, verdict, self.log)
            if entry:
                out.append(entry)
                self.log(f"  + {entry['name']} (RiftWii/mods, {', '.join(entry['games']) or 'no id'})")
        return out

    # -- the entry --

    def entries(self, record, game_name):
        try:
            p = self.profile(record)
        except (Skip, urllib.error.URLError, OSError) as why:
            # Not cached: asked again on the next run.
            self.log(f"  {record.get('_sName')}: its page could not be read now ({why})")
            return []
        if p.get("_bIsPrivate") or p.get("_bIsTrashed") or p.get("_bIsWithheld"):
            return []
        if p.get("_sInitialVisibility") not in (None, "show"):
            return []  # GameBanana hides or warns about it (mature content)
        files = [f for f in p.get("_aFiles") or [] if f.get("_sDownloadUrl")]
        out = []
        for f in files:
            verdict = self.file_verdict(f)
            if "skip" in verdict:
                self.log(f"  {p.get('_sName')}: {f.get('_sFile')}: {verdict['skip']}")
                continue
            out.append((f, verdict))
        if not out:
            return []
        # One entry per mod, always "gb<mod id>": its newest file the Wii can
        # install (else its newest listed one). A new version is a new file
        # on GameBanana; under the same id the store shows it as an update
        # and keeps the install's note, which an id per file would lose.
        out.sort(key=lambda fv: (fv[1]["format"] == "zip", int(fv[0].get("_tsDateAdded") or 0)), reverse=True)
        # The first that makes an entry (a newer zip can still fail later
        # checks, e.g. a folder too long, while an older one is fine).
        for f, v in out:
            entry = self.entry(p, f, v, game_name)
            if entry:
                return [entry]
        return []

    def entry(self, p, f, v, game_name):
        mod_id = p["_idRow"]
        app_id = f"gb{mod_id}"
        name = cut(plain_text(p.get("_sName") or "", 200), app_list.LIMITS["name"])
        author = cut(plain_text(p["_aSubmitter"].get("_sName", ""), 200), app_list.LIMITS["author"]) or "GameBanana"
        version = cut(plain_text(p.get("_sVersion") or "", 60), app_list.LIMITS["version"])
        if not version:
            version = datetime.fromtimestamp(int(f.get("_tsDateAdded") or 0), timezone.utc).strftime("%Y-%m-%d")
        description = plain_text(p.get("_sText") or "", MAX_DESCRIPTION) or f"A Riivolution mod for {game_name}."
        e = {
            "id": app_id,
            "kind": "mod",
            "name": name,
            "author": author,
            "version": version,
            "released": datetime.fromtimestamp(int(f.get("_tsDateAdded") or 0), timezone.utc).strftime("%Y-%m-%d"),
            "category": "mods",
            "game": cut(plain_text(game_name, 200), app_list.LIMITS["author"]),
            "games": v.get("games", []),
            "description": description,
            "page": f"https://gamebanana.com/mods/{mod_id}",
            "platforms": ["wii", "vwii"],
            "download": {"url": f["_sDownloadUrl"], "format": v["format"], "size": v["size"], "sha256": v["sha256"]},
            "program": v["program"],
        }
        license_text = plain_text(p.get("_sLicense") or "", 200)
        if license_text and len(license_text.encode()) <= app_list.LIMITS["license"]:
            e["license"] = license_text
        if v["format"] == "zip":
            e["data_folders"] = v["folders"]
            e["download"]["files"] = v["files"]
            e["download"]["unpacked_size"] = v["unpacked"]
            # The rules are under `root`, the folder in the archive that
            # holds the pack, so a long top folder doesn't make them too long.
            if len(v["root"].encode()) > MAX_ROOT:
                self.log(f"  {name}: its folder in the archive is too long ({v['root']})")
                return None
            if v["root"]:
                e["root"] = v["root"]
            rules = [{"from": v["riivolution"], "to": "riivolution/"}]
            rules += [{"from": top + "/", "to": top + "/"} for top in v["folders"]]
            for rule in rules:
                if len(rule["from"].encode()) > app_list.LIMITS["rule"] or len(rule["to"].encode()) > app_list.LIMITS["rule"]:
                    self.log(f"  {name}: a path too long for the store ({rule['from']})")
                    return None
            e["install"] = rules
        return e


def read_excluded(path):
    """The mod ids in an exclude file: one per line, "# ..." a comment."""
    if not path:
        return set()
    ids = set()
    # utf-8-sig: a file saved by a Windows editor may start with a BOM,
    # which would otherwise hide the first id. A stray byte in a comment
    # must not stop the night's list; only plain ASCII digits are ids.
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            word = line.split("#", 1)[0].strip()
            if word.isascii() and word.isdigit():
                ids.add(int(word))
    return ids


def build(args):
    os.makedirs(args.cache, exist_ok=True)
    excluded = read_excluded(args.exclude)
    hosted = read_hosted(args.hosted)
    excluded |= {int(i) for item in hosted for i in item.get("replaces", [])}
    log_lines = []

    def log(line):
        log_lines.append(line)
        print(line, flush=True)

    web = Web(args.cache)
    builder = Builder(web, args.cache, log)
    games, searched = builder.riivolution_search()
    log(f"a riivolution search names {len(games)} games")
    for name in SEED_GAMES:
        found = builder.find_game(name)
        if found:
            games.setdefault(found[0], found[1])
        else:
            log(f"no GameBanana game is named {name!r}")
    if args.only_game:
        games = {k: v for k, v in games.items() if v in args.only_game}
    entries, seen = [], set()
    for number, (game_id, game_name) in enumerate(sorted(games.items(), key=lambda g: g[1].lower()), 1):
        try:
            mods = builder.game_mods(game_id)
        except (Skip, urllib.error.URLError, OSError) as why:
            log(f"[{number}/{len(games)}] {game_name}: its mods could not be listed now ({why})")
            continue
        if len(mods) > BIG_GAME:
            log(f"[{number}/{len(games)}] {game_name}: {len(mods)} mods, only the "
                f"{len(searched.get(game_id, {}))} a Riivolution search finds are looked at")
            found = searched.get(game_id, {})
            mods = [m for m in mods if m["_idRow"] in found]
        else:
            log(f"[{number}/{len(games)}] {game_name}: {len(mods)} mods")
        for record in mods:
            if record["_idRow"] in excluded:
                log(f"  {record.get('_sName')}: left out (exclude list)")
                continue
            for e in builder.entries(record, game_name):
                if e["id"] in seen:
                    continue
                seen.add(e["id"])
                entries.append(e)
                log(f"  + {e['name']} ({e['download']['format']}, {', '.join(e['games']) or 'no id'})")
        builder.save_state()
    if hosted:
        log(f"{len(hosted)} mod(s) kept in RiftWii/mods' release")
        for e in builder.hosted_entries(hosted):
            if e["id"] not in seen:
                seen.add(e["id"])
                entries.append(e)
        builder.save_state()
    pictures = {item["id"]: item.get("picture") for item in hosted}
    # Icons: from each mod's first picture, kept in the cache by mod.
    out_icons = os.path.join(args.out, "icons")
    os.makedirs(out_icons, exist_ok=True)
    icon_cache = os.path.join(args.cache, "icons")
    os.makedirs(icon_cache, exist_ok=True)
    for e in entries:
        mod_key = e["id"][2:]
        picture = pictures.get(e["id"]) or builder.state["mods"].get(mod_key, {}).get("profile", {}).get("_sPicture")
        cached = os.path.join(icon_cache, e["id"] + ".png")
        if not os.path.exists(cached) and picture:
            try:
                icon = make_icon(web.download(picture, 8 << 20))
            except (Skip, urllib.error.URLError, OSError):
                icon = None
            if icon and not app_list.png_problem(icon):
                with open(cached, "wb") as f:
                    f.write(icon)
        if os.path.exists(cached):
            shutil.copyfile(cached, os.path.join(out_icons, e["id"] + ".png"))
            e["icon"] = ICON_BASE + e["id"] + ".png"
    entries.sort(key=lambda e: (e["game"].lower(), e["name"].lower()))
    listing = {"schema": SCHEMA, "generated": datetime.now(timezone.utc).strftime("%Y-%m-%d"), "apps": entries}
    text = json.dumps(listing, ensure_ascii=False, separators=(",", ":"))
    with open(os.path.join(args.out, "mods.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    problems = 0
    if args.check_tool:
        result = subprocess.run([args.check_tool], input=text.encode(), capture_output=True)
        print(result.stdout.decode(errors="replace").strip())
        if result.returncode != 0:
            problems += 1
            print("the Wii's parser left mods out: " + result.stderr.decode(errors="replace").strip(), file=sys.stderr)
    log(f"{len(entries)} mod(s) listed from {len(games)} games, {web.requests} requests, {problems} problem(s)")
    return 1 if problems else 0


def main():
    # Mod names hold any character; a Windows console may not.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--cache", default=".mod-cache")
    p.add_argument("--check-tool", help="build-host/app_list_check, to read the list back as the Wii does")
    sub = p.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build")
    b.add_argument("out")
    b.add_argument("--only-game", action="append", help="only this GameBanana game name (for a quick try)")
    b.add_argument("--exclude", help="a file of GameBanana mod ids to leave out")
    b.add_argument("--hosted", help="hosted.json: mods kept in RiftWii/mods' own release")
    args = p.parse_args()
    if args.cmd == "build":
        return build(args)
    return 2


if __name__ == "__main__":
    sys.exit(main())
