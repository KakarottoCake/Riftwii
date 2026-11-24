# The app store

RiftWii's app store shows a curated list of homebrew apps and installs
them on the SD card. RiftWii hosts none of the apps: the list points at
each app's own release file and pins it by its SHA-256, so the Wii
installs exactly the version the list approved, wherever that file is
served from.

Three pieces:

- **Manifests**, one JSON file per app, written by people and reviewed
  in pull requests (in the Apps repository).
- **`tools/app_list.py`**, which pins each manifest to a release, downloads
  every pinned file, checks it, and writes `apps.json` and the icons.
- **`apps.json`**, published at `https://riftwii.github.io/apps/apps.json`
  and read by RiftWii (`src/net/applist.cpp`) and, later, by the separate
  downloader. One list, read by both.

## A manifest

`<id>.json`, named after the app's id:

```json
{
  "id": "snes9xgx",
  "name": "Snes9x GX",
  "author": "Tantric",
  "category": "emulators",
  "description": "Super Nintendo Emulator",
  "license": "GPL-2.0",
  "page": "https://github.com/dborth/snes9xgx",
  "platforms": ["wii", "vwii", "mini"],
  "controllers": ["wiimote", "nunchuk", "classic", "gamecube"],
  "data_folders": ["snes9xgx"],
  "source": {"github": "dborth/snes9xgx", "asset": "Snes9xGX-*[!GameCube].zip"},
  "install": [
    {"from": "apps/snes9xgx/", "to": "apps/snes9xgx/"},
    {"from": "snes9xgx/", "to": "snes9xgx/"}
  ],
  "pin": {"tag": "5.0.6", "sha256": "…"}
}
```

| Field | |
| --- | --- |
| `id` | The app's folder: it installs to `sd:/apps/<id>/`. Letters, digits and `-_ .+()!`, at most 47 bytes. |
| `name`, `author` | Shown in the store; at most 63 bytes each. |
| `category` | One of the keys below. |
| `description` | One line, at most 159 bytes. |
| `license` | Optional; an SPDX id where there is one. |
| `page` | The app's own page (https), shown as a QR code. |
| `platforms` | Any of `wii`, `vwii`, `mini`. On a Wii U in Wii mode, an app without `vwii` is marked "Wii only" and the store asks before installing it. |
| `controllers` | Optional: the controllers the app can be used with, any of `wiimote`, `nunchuk`, `classic`, `gamecube`, `keyboard` (a USB keyboard). Shown in the band of the app's page with the pictures a game's page shows for its controllers (none for a keyboard); an app with `classic` or `gamecube` passes the store front's "Without a Wii Remote" filter. A name RiftWii does not know is passed over, and a broken list (more than five names, or not strings) only leaves the pictures out: the app stays in the store. |
| `newer` | Optional: the id of an app on the list that does the same job and is kept up better. The store points to it; both stay listed. |
| `data_folders` | Folders at the card's root the app keeps its data in (at most 4). They must not be folders the Wii, RiftWii or other loaders own (`apps`, `riftwii`, `private`, `title`, `wbfs`, `games`, `nands`, ...). Files the store did not put there are never replaced in them, and removing the app asks before deleting them (they hold saves). |
| `source` | `{"github": "owner/repo", "asset": "<pattern>"}`: the release file matching the pattern (`fnmatch`; exactly one must match). Add `"prerelease": true` to follow a nightly build: `update` then pins the newest release of any kind, so the builder must publish each build under its own tag (`nightly-YYYY-MM-DD-<commit>`) and keep the recent ones, never move one tag. Or `{"url": "https://...", "version": "1.0", "released": "YYYY-MM-DD"}` for a file elsewhere (`http://` too: the pin, not the connection, makes the file the approved one). |
| `version` | Optional for GitHub sources: the release's tag, without a leading `v`, is used when it is missing. |
| `format` | `zip` (the default) or `dol` install on the Wii. `7z`, `rar`, `tar`, `tar.gz` and `tar.bz2` are listed with their page so a player can get them on a PC. |
| `install` | Zip only: up to 6 rules. Entries under `from` (a folder ending in `/`, `""` for the zip's top level, or one file) go under `to`. In a folder `from`, a part that is exactly `*` matches any one folder, for a release whose top folder carries its version (`"*/wii/gcmm/"`). `to` starts with `apps/<id>/` or one of the app's data folders. The first rule that covers an entry places it; entries no rule covers are not installed. The install must put a `boot.dol` or `boot.elf` in `apps/<id>/`. |
| `icon_in_zip` | Optional: the entry to show as the app's icon; by default, whatever installs as `apps/<id>/icon.png`. |
| `pin` | The approved file: its `sha256` (and, for GitHub, the release `tag`). Written by `app_list.py update`; changing it is what approves a new version. |

### Categories

| Key | Shown as |
| --- | --- |
| `games` | Games |
| `emulators` | Emulators |
| `media` | Media |
| `internet` | Internet |
| `online` | Online services |
| `gametools` | Game tools |
| `loaders` | Loaders |
| `system` | System tools |
| `wiimenu` | Wii Menu |
| `controllers` | Controllers |
| `creative` | Creative |
| `toys` | Demos and toys |

A RiftWii that does not know a category shows its apps under "More", so
a new key needs no new schema.

## `apps.json`

What `app_list.py build` writes, schema 1:

```json
{"schema": 1, "apps": [
  {"id": "snes9xgx", "name": "Snes9x GX", "author": "Tantric", "version": "5.0.6",
   "released": "2026-11-18", "category": "emulators", "description": "Super Nintendo Emulator",
   "license": "GPL-2.0", "page": "https://github.com/dborth/snes9xgx",
   "platforms": ["wii", "vwii", "mini"], "icon": "https://riftwii.github.io/apps/icons/snes9xgx.png",
   "data_folders": ["snes9xgx"],
   "download": {"url": "https://github.com/dborth/snes9xgx/releases/download/5.0.6/Snes9xGX-5.0.6.zip",
                "format": "zip", "size": 2523211, "sha256": "…", "files": 41, "unpacked_size": 6012345},
   "install": [{"from": "apps/snes9xgx/", "to": "apps/snes9xgx/"}, {"from": "snes9xgx/", "to": "snes9xgx/"}]}
]}
```

`files` and `unpacked_size` count what the install rules place, so the
Wii knows the room it needs before it downloads. Unknown fields are
skipped, at any level, so a later list can add some.

## What the Wii accepts

The list comes from the internet, so RiftWii reads it as untrusted
(`include/riftwii/applist.hpp`):

- The list is at most 2 MB and 1000 apps; every string is bounded.
- An app that breaks a rule is left out, never trusted: an id that is not
  one plain name, a download address that is not `https://` or `http://`
  to a host name (no user, port or IP address), a page that is not
  `https://`, a SHA-256 that is not 64 lower-case hex digits, a download
  over 128 MB, more than 8192 files or 256 MB unpacked,
  an install rule that writes outside `apps/<id>/` and the app's data
  folders, a data folder the Wii or a loader owns.
- Every entry's path is checked again as it is unpacked: each part is a
  plain name, so nothing climbs out of the folder its rule names.
- The download must match its SHA-256 before anything is written.
- Icons come only from RiftWii's own site.
- No app may take RiftWii's own folders (`apps/riftwii`, the channel
  installer's) as its id, and a path part may not end in a dot or space
  (FAT drops them) or be malformed UTF-8.

The SHA-256s live in the list, so they are only as good as the list: it
must always be fetched over verified TLS (`kAppListUrl` is `https://`, and
stays so).

`app_list.py --check-tool build-host/app_list_check build <manifests> <out>` reads every
list it builds back through the Wii's parser and fails if the Wii would
leave any app out.

## Keeping it up to date

```
py -3.13 tools/app_list.py update <manifests>          # pin new releases
py -3.13 tools/app_list.py check <manifests>           # what a PR runs
py -3.13 tools/app_list.py --check-tool build-host/app_list_check build <manifests> <site>
```

`update` only moves pins forward for GitHub sources; the pull request it
makes is the review. A file that changes behind an unchanged pin (a
release whose file was replaced) fails `build`, so it is never published.

## The store on the Wii

The Rift Store button on Home's bar (the bag, left of Search) and Settings > Online > Rift Store open the store (the Rift Store: its name in every language, and its logo, `docs/images/rift-store.png`, made by `tools/make_store_logo.py`). Its pages are the `appstore`
plugin's (`plugins/appstore/`, docs/PLUGINS.md), loaded only while the store
is open, so the DOL carries none of them.

| Page | What it shows |
| --- | --- |
| Store front | The categories with apps in them, as tiles with their icons (four across on a 16:9 menu, three on a 4:3 one), and under them a line of what the lit one holds and how many apps; how many apps there are and how many are installed in the band; My apps (with the updates waiting) and Search with the prompts. - turns on the "Without a Wii Remote" filter (apps listed with a Classic or GameCube controller), which the counts, the categories and Search follow, and turns it off again. |
| A category, My apps, search results | The apps as tiles: each app's Homebrew Channel icon (its category's until it is in), its name, and its state (Installed, Update, On the SD card, Get it on a PC) or its author and version; the lit app's description under them. A category's icon in its band. Two rows a page; the D-pad turns the page at the edges, - and + too. On a Wii U, an app not listed for vWii says "Wii only". |
| An app | Laid out like a game's page: its category, name, author and version in the band, the controllers it works with as pictures at the band's right, its icon, one big action (Install, Update, Replace, Remove), the description, a QR code to its own page, a note when `newer` names an app that does the same job (+ opens it), and along the bottom its size, date, consoles and license, each after its icon. While it installs, the action shows each step and a bar; B stops it. |

The icons are theme pictures (`store_*` in [THEMES.md](THEMES.md)), drawn
with `RWP_SKIN_PICTURE`, so a theme can replace them. The plugin paints
RiftWii's own (`src/art/storeicons.cpp`) in the theme's icon colour, so the
DOL carries only the lookup of a theme's PNG.

Search is Home's keyboard: the apps that match are named above it as you
type, and keys that lead nowhere are dimmed (`app_matches`,
`app_search_next` in `src/net/appcatalog.cpp`). All text is 18 pixels or
larger, for small TVs and 480i.

An install, on RiftWii's network thread:

1. The card must have room for the download and its unpacked files side by
   side, and a megabyte more (the host's `sd_free_kib`, which may walk the
   whole FAT, so it is asked here and not on a frame); else the player is
   told how much room to make, before anything is downloaded.
2. The download streams to `sd:/riftwii/apps/download.tmp`, hashed on the
   way (`HttpGetStream`, `wii/online/httpmem.cpp`), refused past the size
   the list gives, and given up when under 32 KB arrive in 30 seconds.
3. Its SHA-256 must be the list's, or nothing is written.
4. A zip's directory is read from the card a piece at a time
   (`zip_read_directory_at`), the plan checked as a whole
   (`app_plan_install`), and every file unpacked into
   `sd:/riftwii/apps/staging`. Stop is heard inside a file too. Until
   here nothing of the app has changed: a failure, Stop or a full card
   leaves it as it was.
5. What is on the card decides what goes in (`app_install_replaces`):
   the program (`boot.dol`, `boot.elf`, `meta.xml`, `icon.png` in
   `apps/<id>/`) always; a file the store put there before and nobody
   changed since (same size and CRC-32 as its note says); and a new file.
   Kept, and logged: a file the app or the player changed since the store
   put it there (its settings, a save), any file in a data folder the
   store did not put there, and, once the store has a list of what it
   installed, any such file in `apps/<id>/` too.
6. The store's note (`sd:/riftwii/apps/<id>.txt`: the version, the
   SHA-256 and every file the store put on the card, with its size and
   CRC-32) is staged with the files, its write checked.
7. Then the files are put in place, each old one kept as `<file>.bak`
   until all are in. If one cannot be moved, the ones already moved are
   put back, so the app is the old version or the new one, never a mix
   (a power cut in the middle leaves the `.bak` files, nothing lost). A
   fresh install that fails leaves no folder. Files of the old version
   the new one does not have (its settings) stay.

An app whose note differs from the list has an update. An app whose
program is there without a note was put there some other way, and
installing (Replace) replaces the files in its own folder; its data
folders are kept all the same. A note from before the store listed its
files works the same way.

Remove deletes the files the store put there and nobody changed since
(without a list, only the program files), and the folders that leaves
empty. What is left (the app's settings in its own folder, its data
folders with their saves) is named, and the player chooses: Keep them
(also B), or Remove them, which deletes `sd:/apps/<id>` and the data
folders whole. Kept, the note stays with `removed=1` and the files kept,
so the app counts as not installed and a later install keeps them too.
Home's Homebrew view reads `sd:/apps` again when the store changed
something.

## What gets in

- Source available (or a stated reason why not), and a working download.
- No malware, no copyrighted game data, nothing built around something
  its owner had taken down.
- AI use by itself is no reason to leave an app out; whether it works is.
