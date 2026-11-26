#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""GameBanana discovery for save_list.py; all payloads stay on GameBanana.

Only data and public API behavior are shared with the sibling mod index.
No external mod or save-manager implementation is used.
"""

from collections import Counter
import hashlib
import html
import json
from pathlib import Path
import re
import struct
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

import save_list

API = "https://gamebanana.com/apiv11/"
USER_AGENT = "RiftWii-save-list/2 (+https://riftwii.com)"
PAUSE = 0.35
MAX_PAGES = 1000
SEARCHES = ("save file", "100% save", "savegame")
SAVE_WORDS = re.compile(r"\bsave(?:[ -]?(?:file|game|data))?s?\b|100\s*%|all unlocked", re.I)

# Region codes are alternatives to identify, never a claim of portability.
# A GCI must name its exact header ID. Wii regions require a path, explicit
# ID, or an unambiguous author region statement. No default region is used.
GAMES = {
    "Super Mario Galaxy": ("RMGE01", "RMGP01", "RMGJ01"),
    "Super Mario Galaxy 2": ("SB4E01", "SB4P01", "SB4J01"),
    "New Super Mario Bros. Wii": ("SMNE01", "SMNP01", "SMNJ01"),
    "Mario Kart Wii": ("RMCE01", "RMCP01", "RMCJ01", "RMCK01"),
    "Super Smash Bros. Brawl": ("RSBE01", "RSBP01", "RSBJ01", "RSBK01"),
    "Super Mario Sunshine": ("GMSE01", "GMSP01", "GMSJ01"),
    "Kirby's Return to Dream Land": ("SUKE01", "SUKP01", "SUKJ01", "SUKK01"),
    "Wii Sports Resort": ("RZTE01", "RZTP01", "RZTJ01", "RZTK01"),
    "Donkey Kong Country Returns": ("SF8E01", "SF8P01", "SF8J01"),
    "The Legend of Zelda: Twilight Princess": ("RZDE01", "RZDP01", "RZDJ01", "GZ2E01", "GZ2P01", "GZ2J01"),
    "Super Smash Bros. Melee": ("GALE01", "GALP01", "GALJ01"),
    "Mario Kart: Double Dash!!": ("GM4E01", "GM4P01", "GM4J01"),
    "Paper Mario: The Thousand Year Door": ("G8ME01", "G8MP01", "G8MJ01"),
    "Wii Sports": ("RSPE01", "RSPP01", "RSPJ01", "RSPK01"),
    "Wii Play": ("RHAE01", "RHAP01", "RHAJ01"),
    "Super Paper Mario": ("R8PE01", "R8PP01", "R8PJ01"),
    "Mario Party 8": ("RM8E01", "RM8P01", "RM8J01"),
    "Mario Party 9": ("SSQE01", "SSQP01", "SSQJ01"),
    "The Legend of Zelda: Skyward Sword": ("SOUE01", "SOUP01", "SOUJ01"),
    "Kirby's Epic Yarn": ("RK5E01", "RK5P01", "RK5J01"),
    "Sonic Colors": ("SNCE8P", "SNCP8P", "SNCJ8P"),
    "Animal Crossing: City Folk": ("RUUE01", "RUUP01", "RUUJ01"),
    "Luigi's Mansion": ("GLME01", "GLMP01", "GLMJ01"),
    "Sonic Adventure 2": ("GSNE8P", "GSNP8P", "GSBJ8P"),
    "Sonic Adventure DX": ("GXSE8P", "GXSP8P", "GASJ8P"),
    "Pikmin 2": ("GPVE01", "GPVP01", "GPVJ01"),
    "Metroid Prime": ("GM8E01", "GM8P01", "GM8J01"),
    "F-Zero GX": ("GFZE01", "GFZP01", "GFZJ01"),
    "Animal Crossing": ("GAFE01", "GAFP01", "GAFJ01"),
    "Kirby Air Ride": ("GKYE01", "GKYP01", "GKYJ01"),
    "The Legend of Zelda: The Wind Waker": ("GZLE01", "GZLP01", "GZLJ01"),
    "Pikmin": ("GPIE01", "GPIP01", "GPIJ01"),
    "Super Mario Strikers": ("G4QE01", "G4QP01", "G4QJ01"),
    "Star Fox Adventures": ("GSAE01", "GSAP01", "GSAJ01"),
    "Metroid Prime 3: Corruption": ("RM3E01", "RM3P01", "RM3J01"),
    "Metroid Prime: Trilogy": ("R3ME01", "R3MP01"),
    "Kirby's Dream Collection: Special Edition": ("S72E01", "S72J01"),
    "Mario Super Sluggers": ("RMBE01", "RMBJ01"),
    "Mario Sports Mix": ("RMKE01", "RMKP01", "RMKJ01"),
    "Mario Strikers Charged": ("R4QE01", "R4QP01", "R4QJ01"),
    "Mario & Sonic at the Olympic Winter Games": ("ROLE8P", "ROLP8P"),
    "Sonic Unleashed (Wii)": ("RSVE8P", "RSVP8P", "RSVJ8P"),
    "Go Vacation (Wii)": ("SGVEAF", "SGVPAF", "SGVJAF"),
}


class Web:
    def __init__(self, cache, refresh=False):
        self.cache = Path(cache)
        self.refresh = refresh
        self.last = 0.0
        self.requests = 0
        self.opener = urllib.request.build_opener(save_list.HttpsRedirects)
        self.cache.mkdir(parents=True, exist_ok=True)

    @staticmethod
    def cache_write(path, data):
        staged = path.with_suffix(path.suffix + ".tmp")
        staged.write_bytes(data)
        staged.replace(path)

    def get(self, url, limit, headers=None):
        save_list.require(save_list.web_url(url), "invalid request URL")
        for attempt in range(4):
            delay = self.last + PAUSE - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            self.last = time.monotonic()
            self.requests += 1
            request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, **(headers or {})})
            try:
                with self.opener.open(request, timeout=45) as response:
                    data = response.read(limit + 1)
                    if len(data) > limit and response.status == 200 and "Range" in (headers or {}):
                        # The server ignored the range and sent the whole file:
                        # the caller downloads it properly instead.
                        return None, response.status, response.headers
                    save_list.require(len(data) <= limit, "response exceeds its size limit")
                    return data, response.status, response.headers
            except urllib.error.HTTPError as error:
                if error.code not in (429, 500, 502, 503, 504) or attempt == 3:
                    raise
                retry = error.headers.get("Retry-After", "")
                delay = min(int(retry), 60) if retry.isdigit() else 5 * (attempt + 1)
            except (urllib.error.URLError, TimeoutError, ConnectionError):
                if attempt == 3:
                    raise
                delay = 5 * (attempt + 1)
            time.sleep(delay)
        raise OSError("request retries exhausted")

    def api(self, endpoint, **params):
        url = API + endpoint + ("?" + urllib.parse.urlencode(params) if params else "")
        path = self.cache / ("api-" + hashlib.sha256(url.encode()).hexdigest() + ".json")
        if self.refresh or not path.exists():
            data, _, _ = self.get(url, 8 << 20)
            value = json.loads(data, object_pairs_hook=save_list.unique_object)
            save_list.require(isinstance(value, dict), "API did not return an object")
            self.cache_write(path, data)
        return json.loads(path.read_bytes(), object_pairs_hook=save_list.unique_object)

    def file(self, record):
        file_id = int(record["_idRow"])
        save_list.require(file_id > 0, "invalid file id")
        url = record["_sDownloadUrl"]
        save_list.require(url == f"https://gamebanana.com/dl/{file_id}", "unexpected download URL")
        size = int(record.get("_nFilesize") or 0)
        save_list.require(0 < size <= save_list.MAX_DOWNLOAD, f"unsupported size {size}")
        path = self.cache / f"file-{file_id}.bin"
        if not path.exists():
            data, _, _ = self.get(url, save_list.MAX_DOWNLOAD)
            verify_file(record, data)
            self.cache_write(path, data)
        data = path.read_bytes()
        verify_file(record, data)
        return data

    def names(self, record):
        file_id = int(record["_idRow"])
        save_list.require(file_id > 0 and record["_sDownloadUrl"] == f"https://gamebanana.com/dl/{file_id}",
                          "unexpected download URL")
        size = int(record.get("_nFilesize") or 0)
        save_list.require(0 < size <= save_list.MAX_DOWNLOAD, f"unsupported size {size}")
        path = self.cache / f"tail-{int(record['_idRow'])}.bin"
        if path.exists():
            tail = path.read_bytes()
        else:
            start = max(0, size - 65536)
            tail, status, headers = self.get(record["_sDownloadUrl"], 65536,
                                             {"Range": f"bytes={start}-{size - 1}"})
            if tail is None:
                return None
            if status == 206:
                save_list.require(headers.get("Content-Range") == f"bytes {start}-{size - 1}/{size}", "bad byte range")
            elif status != 200 or size > 65536:
                return None
            save_list.require(len(tail) == size - start, "incomplete ZIP tail")
            self.cache_write(path, tail)
        return zip_names_from_tail(tail, size)


def verify_file(record, data):
    save_list.require(len(data) == int(record["_nFilesize"]), "download size differs from GameBanana")
    md5 = record.get("_sMd5Checksum")
    if md5:
        save_list.require(hashlib.md5(data).hexdigest() == md5.lower(), "download MD5 differs from GameBanana")


def zip_names_from_tail(tail, size):
    """Read a complete ordinary central directory, or request a full check."""
    end = tail.rfind(b"PK\x05\x06")
    if end < 0 or end + 22 > len(tail):
        return None
    disk, cd_disk, here, count, cd_size, offset, comment = struct.unpack_from("<HHHHIIH", tail, end + 4)
    if disk or cd_disk or here != count or count == 65535 or end + 22 + comment != len(tail):
        return None
    start = offset - (size - len(tail))
    if start < 0 or start + cd_size != end or count > save_list.MAX_FILES * 2:
        return None
    names = []
    at = start
    for _ in range(count):
        if at + 46 > end or tail[at:at + 4] != b"PK\x01\x02":
            return None
        flags = struct.unpack_from("<H", tail, at + 8)[0]
        name_size, extra, comment_size = struct.unpack_from("<HHH", tail, at + 28)
        next_at = at + 46 + name_size + extra + comment_size
        if next_at > end:
            return None
        raw = tail[at + 46:at + 46 + name_size]
        names.append(raw.decode("utf-8" if flags & 0x800 else "cp437", "replace"))
        at = next_at
    return names if at == end else None


def plain_text(value, limit):
    text = re.sub(r"<(br\b|/p|/div|/li)[^>]*>", " ", value or "", flags=re.I)
    text = html.unescape(re.sub(r"<[^>]*>", "", text))
    text = " ".join("".join(c if c.isprintable() else " " for c in text).split())
    if len(text.encode()) >= limit:
        text = text.encode()[:limit - 4].decode("utf-8", "ignore").rstrip() + "..."
    return text


def establish_ids(game, names, text, gamecube=False):
    candidates = [value for value in GAMES[game] if value.startswith("G") == gamecube]
    save_list.require(candidates, "this game has no known IDs on this platform")
    found = []
    # Paths are stronger evidence than prose. A title code need not include
    # its maker bytes, but must be one of this exact game's known IDs.
    for name in names:
        for candidate in candidates:
            pattern = r"(?<![A-Z0-9])" + candidate[:4] + r"(?:" + candidate[4:] + r")?(?![A-Z0-9])"
            if re.search(pattern, name.upper()) and candidate not in found:
                found.append(candidate)
        for match in re.finditer(r"(?:^|/)(?:00010000)?([0-9a-fA-F]{8})(?:/|$)", name):
            title = match.group(1).lower()
            for candidate in candidates:
                if title == candidate[:4].encode().hex() and candidate not in found:
                    found.append(candidate)
    if found:
        save_list.require(len(found) == 1, "archive names identify several regions")
        return found, "archive title path"
    explicit = [value for value in candidates if re.search(r"\b" + value + r"\b", text, re.I)]
    if explicit:
        save_list.require(len(explicit) == 1, "description names several regions without a portable save")
        return explicit, "author names the full ID"
    # Broad claims about a Switch/PC port must not be mistaken for Wii saves.
    save_list.require(not re.search(r"\b(?:switch|ryujinx|yuzu|3d all.stars|switch 2)\b", text, re.I),
                      "description is for a different platform")
    regions = []
    for code, pattern in (("E", r"\b(?:USA|NTSC-U|North America(?:n)?)\b"),
                          ("P", r"\b(?:PAL|EUR|Europe(?:an)?)\b"),
                          ("J", r"\b(?:JPN|NTSC-J|Japan(?:ese)?)\b"),
                          ("K", r"\b(?:KOR|Korea(?:n)?)\b")):
        if re.search(pattern, text, re.I):
            regions.append(code)
    if len(regions) == 1:
        found = [value for value in candidates if value[3] == regions[0]]
        if found:
            return found, "author names the region"
    # Require an explicit portability claim; a mere list of region names
    # can describe conversion instructions rather than this payload.
    if not gamecube and re.search(r"\b(?:region[ -]free|all regions|any region)\b", text, re.I):
        return candidates, "author explicitly says all regions"
    raise ValueError("no reliable game ID or region")


def gci_game_id(game, data):
    game_id = data[:6].decode("ascii", "replace")
    save_list.require(re.fullmatch(r"G[A-Z0-9]{5}", game_id) and
                      any(value.startswith("G") and value[:3] == game_id[:3] for value in GAMES[game]),
                      "GCI header is not this game's ID")
    # The header can identify a disc hack with its own maker/revision code.
    # Keep that exact ID; never change it to the stock disc's table entry.
    return game_id


def export_game_id(game, data):
    # WiiBrew's public Savegame Files format documents this unencrypted
    # backup header after the encrypted 0xF0C0-byte banner/header area.
    # Read only its title ID; no keys, decryption or console identifiers.
    backup = 0xF0C0
    if len(data) < backup + 0x80 or data[backup:backup + 8] != b"\x00\x00\x00\x70Bk\x00\x01":
        return None
    title = data[backup + 0x60:backup + 0x68]
    save_list.require(title[:4] == b"\x00\x01\x00\x00", "export is not a Wii disc save")
    code = title[4:].decode("ascii", "replace")
    ids = [value for value in GAMES[game] if not value.startswith("G") and value[:4] == code]
    save_list.require(len(ids) == 1, f"export header {code} does not identify this game")
    return ids[0]


def classify(game, record, data, text):
    name = record["_sFile"]
    if name.lower().endswith(".gci"):
        game_id = gci_game_id(game, data)
        manifest = {"game_id": game_id, "platform": "gamecube", "format": "gci"}
        save_list.inspect_payload(manifest, data)
        return manifest, "GCI header"
    save_list.require(name.lower().endswith(".zip"), "only .gci and .zip downloads are supported")
    with save_list.checked_zip(data) as archive:
        regular = [entry for entry in archive.infolist() if not entry.is_dir()]
        gcis = [entry for entry in regular if entry.filename.lower().endswith(".gci")]
        banners = [entry for entry in regular if entry.filename.rsplit("/", 1)[-1] == "banner.bin"]
        exports = [entry for entry in regular if entry.filename.rsplit("/", 1)[-1].lower() == "data.bin"]
        if gcis:
            save_list.require(len(gcis) == 1 and not banners and not exports, "archive has several saves")
            entry = gcis[0]
            save_list.require(entry.file_size <= 64 + 2043 * 8192, "GCI member is too large")
            gci = archive.read(entry)
            game_id = gci_game_id(game, gci)
            manifest = {"game_id": game_id, "platform": "gamecube", "format": "gci", "member": entry.filename}
            evidence = "GCI header"
        else:
            save_list.require(banners or exports, "ZIP needs one extracted Wii save or data.bin")
            save_list.require(len(banners) <= 1, "archive has several Wii saves")
            if banners:
                root = banners[0].filename[:-len("banner.bin")]
                ids, evidence = establish_ids(game, [root], text + " " + name)
                # A ZIP may include an SD export of the same save beside
                # its extracted folder. It is never installed. A data.bin
                # inside root is an ordinary game file, not a second save.
                companion_exports = [entry for entry in exports if not entry.filename.startswith(root)]
                save_list.require(len(companion_exports) <= 1, "archive has several Wii exports")
                for entry in companion_exports:
                    export_id = export_game_id(game, archive.read(entry))
                    save_list.require(export_id in ids, "companion export does not identify the selected save")
            else:
                save_list.require(len(exports) == 1, "archive has several Wii saves")
                export_id = export_game_id(game, archive.read(exports[0]))
                save_list.require(export_id is not None, "data.bin has no Wii SD export backup header")
                ids = [export_id]
                evidence = "unencrypted data.bin backup header"
            manifest = {"game_id": ids[0], "platform": "wii", "format": "wii-zip" if banners else "data.bin"}
            if len(ids) > 1:
                manifest["game_ids"] = ids
            if banners:
                manifest["root"] = root
        manifest.update(save_list.inspect_payload(manifest, data))
        if manifest["format"] == "wii-zip":
            save_list.require(manifest["files"] >= 2, "extracted save has only a banner and no save files")
        return manifest, evidence


class Builder:
    def __init__(self, web, log):
        self.web = web
        self.log = log
        self.coverage = {}

    def pages(self, endpoint, max_pages=MAX_PAGES, **params):
        for page in range(1, max_pages + 1):
            result = self.web.api(endpoint, _nPage=page, **params)
            records = result.get("_aRecords", [])
            save_list.require(isinstance(records, list), "API records are not a list")
            yield from records
            metadata = result.get("_aMetadata", {})
            if not records or metadata.get("_bIsComplete"):
                return
            per_page = int(metadata.get("_nPerpage") or len(records))
            count = metadata.get("_nRecordCount")
            if count is not None and page * per_page >= int(count):
                return
        raise ValueError(f"{endpoint}: reached page cap; refusing incomplete discovery")

    def discover(self):
        found = {}
        for words in SEARCHES:
            records = list(self.pages("Util/Search/Results", _sModelName="Mod", _sSearchString=words))
            self.log(f"Search {words!r}: {len(records)} results")
            for record in records:
                game = (record.get("_aGame") or {}).get("_sName")
                if game in GAMES:
                    found[int(record["_idRow"])] = (game, record)
        for game in GAMES:
            matches = self.web.api("Util/Search/Results", _sModelName="Game", _sSearchString=game, _nPage=1)
            rows = [row for row in matches.get("_aRecords", []) if row.get("_sName", "").casefold() == game.casefold()]
            if not rows:
                self.log(f"Game {game}: exact game page not found")
                self.coverage[game] = {"mods": 0, "candidates": 0, "found": False}
                continue
            row_id = int(rows[0]["_idRow"])
            total = candidates = 0
            for record in self.pages("Mod/Index", _nPerpage=50, **{"_aFilters[Generic_Game]": row_id}):
                total += 1
                summary = " ".join(str(record.get(key) or "") for key in ("_sName", "_sText", "_sDescription", "_sDescriptionExcerpt"))
                category = record.get("_aRootCategory") or {}
                summary += " " + category.get("_sName", "")
                if SAVE_WORDS.search(summary):
                    candidates += 1
                    found[int(record["_idRow"])] = (game, record)
            searched = set()
            for words in ("save", "100%", "savegame"):
                for record in self.pages("Util/Search/Results", _sModelName="Mod", _sSearchString=words, _idGameRow=row_id):
                    # Verify the server applied the game filter as requested.
                    actual_game = (record.get("_aGame") or {}).get("_idRow")
                    save_list.require(actual_game == row_id, "search returned another game's submission")
                    mod_id = int(record["_idRow"])
                    searched.add(mod_id)
                    found[mod_id] = (game, record)
            self.coverage[game] = {"mods": total, "candidates": candidates, "searched_candidates": len(searched),
                                   "game_row": row_id, "found": True}
            self.log(f"Game {game}: {total} mods scanned; {candidates} named candidates; {len(searched)} search candidates")
        return found

    def entries(self, mod_id, game):
        profile = self.web.api(f"Mod/{mod_id}/ProfilePage")
        if any(profile.get(key) for key in ("_bIsPrivate", "_bIsTrashed", "_bIsWithheld", "_bIsObsolete")):
            raise ValueError("submission is private, removed, withheld or obsolete")
        save_list.require(profile.get("_sInitialVisibility") in (None, "show"), "submission visibility is restricted")
        save_list.require((profile.get("_aGame") or {}).get("_sName") == game, "submission changed games")
        title = plain_text(profile.get("_sName"), 80)
        description = plain_text(profile.get("_sText"), 240) or f"A shared save for {game}."
        author = plain_text((profile.get("_aSubmitter") or {}).get("_sName"), 64)
        save_list.require(title and author, "missing title or submitter")
        full_text = plain_text(profile.get("_sText"), 100000) + " " + title
        save_list.require(SAVE_WORDS.search(full_text), "not described as a save")
        files = profile.get("_aFiles") or []
        save_list.require(isinstance(files, list) and len(files) <= 100, "too many submission files")
        entries = []
        for record in files:
            file_id = record.get("_idRow")
            label = f"{game} / {mod_id} / {file_id} / {record.get('_sFile')}"
            try:
                save_list.require(record.get("_sAvResult") in ("clean", None), "GameBanana virus scan is not clean")
                name = record.get("_sFile", "")
                save_list.require(name.lower().endswith((".zip", ".gci")), "only .gci and .zip downloads are supported")
                if name.lower().endswith(".zip"):
                    names = self.web.names(record)
                    if names is not None:
                        save_list.require(any(value.lower().endswith((".gci", "/banner.bin", "/data.bin")) or
                                              value.lower() in ("banner.bin", "data.bin") for value in names),
                                          "no GCI, banner.bin or data.bin in ZIP directory")
                data = self.web.file(record)
                manifest, evidence = classify(game, record, data, full_text)
                manifest.update({
                    "id": f"gb{mod_id}-{file_id}", "title": title, "description": description,
                    "author": author, "page": f"https://gamebanana.com/mods/{mod_id}",
                    "url": record["_sDownloadUrl"], "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                })
                # GameBanana-hosted sharing is explicitly authorized by the
                # task. This source never grants permission to mirror bytes.
                manifest["approval"] = {"approved": True, "evidence": manifest["page"],
                                        "reviewer": "GameBanana source policy", "note": evidence}
                entries.append(manifest)
                self.log(f"Keep {label}: {manifest['format']}, {manifest['game_id']} ({evidence})")
            except (ValueError, OSError, zipfile.BadZipFile, KeyError, TypeError, RuntimeError) as error:
                self.log(f"Skip {label}: {error}")
        return entries


def read_excluded(path):
    """GameBanana mod ids to leave out, one per line ("# ..." is a comment):
    saves whose makers asked RiftWii/saves not to list them."""
    if not path:
        return set()
    ids = set()
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        word = line.split("#", 1)[0].strip()
        if word.isdigit():
            ids.add(int(word))
    return ids


def build(args):
    args.output.mkdir(parents=True, exist_ok=True)
    excluded = read_excluded(getattr(args, "exclude", None))
    log_path = args.output / "discovery.log"
    with log_path.open("w", encoding="utf-8", newline="\n") as log_file:
        def log(message):
            print(message, flush=True)
            log_file.write(message + "\n")
            log_file.flush()

        web = Web(args.cache, args.refresh)
        builder = Builder(web, log)
        candidates = builder.discover()
        manifests = []
        for mod_id, (game, _) in sorted(candidates.items()):
            if mod_id in excluded:
                log(f"Skip {game} / {mod_id}: left out (exclude list)")
                continue
            try:
                for manifest in builder.entries(mod_id, game):
                    manifests.append((game, manifest))
            except (ValueError, OSError, KeyError, TypeError) as error:
                log(f"Skip {game} / {mod_id}: {error}")
        save_list.require(len(manifests) <= 512, "catalogue exceeds 512 entries")
        manifests.sort(key=lambda item: (item[0], item[1]["title"], item[1]["id"]))
        entries = [{key: manifest[key] for key in save_list.FIELDS if key in manifest} for _, manifest in manifests]
        text = (json.dumps({"schema": 1, "saves": entries}, ensure_ascii=False, separators=(",", ":")) + "\n").encode()
        save_list.require(len(text) <= save_list.MAX_LIST, "catalogue exceeds 1 MiB")
        save_list.native_check(text, args.check_tool)
        # Keep approval and ID evidence separate from the published schema.
        evidence = {manifest["id"]: {"game": game, **manifest["approval"]} for game, manifest in manifests}
        summary = {"formats": dict(Counter(entry["format"] for entry in entries)),
                   "games": dict(Counter(game for game, _ in manifests)),
                   "coverage": builder.coverage, "requests": web.requests, "entries": len(entries)}
        (args.output / "evidence.json").write_text(json.dumps(evidence, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        (args.output / "summary.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        staged = args.output / "saves.json.tmp"
        staged.write_bytes(text)
        staged.replace(args.output / "saves.json")
        log(f"Listed {len(entries)} saves; formats: {summary['formats']}; requests: {web.requests}")
