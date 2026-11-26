# Downloading saves

Open a Wii or GameCube disc game's page, choose **Saves**, then **Get a
save**. The list shows the saves for that exact six-character game ID
(region and maker included), with their title, author and description.
Choose one, confirm the destination, and the download and installation
run with a progress bar and Cancel. The approved list is cached for the
session; a failed request can be retried.

WiiWare and Virtual Console titles are outside schema 1. A pack with its
own save location must be turned off before installing a stock save.

## Sources

Research checked on 2026-11-26, using public pages and file headers only.
No third-party save manager implementation or Riivolution source was read.

| Source | What is available | Suitability |
| --- | --- | --- |
| [GameFAQs: Mario Kart Wii](https://gamefaqs.gamespot.com/wii/942008-mario-kart-wii/saves) and [Super Mario Sunshine](https://gamefaqs.gamespot.com/gamecube/533287-super-mario-sunshine/saves) | Player submissions, author, region and descriptions; separate saves per game. The search index returned the Wii page; direct page fetches were blocked. | Useful for finding contributors, not a reliable game-ID download API. Do not scrape, infer permission to mirror, or follow mutable downloads without review and pins. |
| [Tapioca's GameCube saves](https://tapioca.moe/gc/saves) | The author's own playthrough saves, direct GCI and ZIP links, and completion notes. Three direct GCI downloads were fetched and their game IDs, block counts, lengths and SHA-256s checked. | Good real examples of author-hosted files. No blanket redistribution license was established. The included manifests are **pending review**, not an approved live catalogue. |
| [SaveGame Manager GX](https://wiibrew.org/wiki/SaveGame_Manager_GX) | Extracts and installs Wii saves, including data.bin support. | A contributor can supply their own extracted save. It is a tool, not a curated, game-ID save service. No code or keys from it are used. |
| [GitHub: wii-inspect](https://github.com/Aaron98990/wii-inspect) | An example of a game-specific save analysis project, using a player's own Wii Sports Resort data.bin. | GitHub is suitable hosting for reviewed submissions and immutable release assets, but a code license does not automatically cover uploaded game data. No general permission-cleared Wii/GameCube completion-save API was established in this research. |
| [WiiBrew: Savegame Files](https://wiibrew.org/wiki/Savegame_Files) | Public description of the encrypted SD export and save banner. | Confirms why data.bin needs conversion. This feature does not decrypt it or contain any SD key. |

Decision: maintain a **RiftWii-owned catalogue** like the
Rift Store, published over verified HTTPS at
`https://riftwii.com/saves/saves.json`. Each save has a full game ID, an
author, permission evidence and an approved file pinned by size and
SHA-256. Ordinary submissions require human review. The GameBanana source
below follows an explicitly authorized linking policy. External downloads
remain possible, and explicitly authorized files can be hosted by the
catalogue repository. An unavailable source fails without changing a save.

This is a distribution policy, not a claim that all game saves have a
particular legal status. Review each submission: the contributor must
have authority to share it; remove personal information and exclude keys,
console dumps, game programs, exploit payloads and unauthorized assets.
Do not copy a site wholesale or assume its hosting grants mirror rights.
Only catalogue records and evidence are included in this code repository.
GameBanana downloads are cached under ignored build-host/; no save payloads
are included in the generated site or added to the repository.

### GameBanana saves

GameBanana uploads count as their makers' permission to share **from
GameBanana**. RiftWii links to GameBanana's own downloads; it does not
re-host the saves or claim a mirror license. This source policy was
explicitly authorized for this catalogue. It does not approve arbitrary
third-party manifests or remove their existing human-review requirement.

```text
py -3.13 tools/save_list.py --check-tool build-host/save_list_check.exe gamebanana tools/saves-gamebanana
```

The source is implemented in `tools/save_gamebanana.py`. It searches for
`save file`, `100% save` and `savegame` globally; scans each seeded game's
complete mod index for save names and categories; and searches `save`,
`100%` and `savegame` within each game. The full description is checked on
each candidate's profile. This covers saves filed under Maps, Game files,
Other/Misc and Save Files, without treating every mod as a save. The seed
table includes the thirteen requested Wii/GameCube games and thirty more.
Unknown games or regions are omitted rather than guessed.

The API and file cache defaults to `build-host/save-gamebanana-cache/`.
Requests include a User-Agent, run one at a time with at least 0.35 seconds
between them, and retry HTTP 429 and server failures. ZIP central
directories are screened with a range request before a complete download.
API pages, ZIP tails and complete payloads are cached; `--refresh` updates
API pages while retaining immutable file-ID downloads. Cached downloads
are checked against GameBanana's size and MD5 again; published records pin
the complete original download with SHA-256. No certificate checking is
disabled. Transient failures and unsupported submissions are logged.

Only a loose GCI, a ZIP containing exactly one GCI, a ZIP containing one
extracted Wii save, or a ZIP containing one data.bin export is accepted.
RAR/7z archives, multiple-save bundles, missing IDs, restricted/removed
submissions and non-clean virus scans are left out. Safe companion files,
including readmes, title metadata and an SD export of the same save, may
remain outside the selected save folder; they are never installed. A
companion export must identify that same save's title and region.
`data.bin` records remain PC-only even
when the source file is a ZIP. They are never fetched by the Wii.

A GCI's six ID bytes are authoritative and must belong to the listed title
family. A disc hack's own full ID is retained, including its maker code;
it is never retagged as the stock game. The lookup table's factual IDs were
checked against [GameTDB's title list](https://www.gametdb.com/wiitdb.txt?LANG=EN).
For data.bin, the source can read the plain backup header's title ID at
offset 0xF124, after checking its size and Bk/version markers, as documented
in [WiiBrew's file format](https://www.wiibrew.org/wiki/Savegame_Files).
This needs no key and does not decrypt or install the export. A missing
backup marker or a header identifying a different game is rejected.
For extracted saves, Wii title paths, full
IDs or 8/16-digit hexadecimal title folders identify a region; failing
those, the author must name an unambiguous region. The known-ID table is a lookup, never a default
region. Multiple Wii IDs require the author's explicit region-free/all
regions statement. The source records that evidence; this is not proof of
in-game portability. GCI headers cannot be retagged for another region.

The generated `saves.json` contains only the Wii's published fields.
`evidence.json` records the GameBanana page and ID evidence for each
approved source record; `summary.json` gives counts and discovery coverage;
`discovery.log` explains every accepted or omitted candidate file. To
publish, merge these records with other approved manifests, check unique
IDs and the combined 512-record/1-MiB limits, run `save_list_check`, and
serve the combined list at the existing catalogue endpoint. This command
does not publish or change the Wii's URL.

## Published format

Schema 1, UTF-8 JSON, at most 1 MiB and 512 records:

```json
{
  "schema": 1,
  "saves": [
    {
      "id": "an-approved-save",
      "game_id": "GAAE01",
      "platform": "gamecube",
      "title": "All stages unlocked",
      "description": "Describe progress, slots, revision and restrictions here.",
      "author": "The player who made it",
      "page": "https://example.org/saves",
      "format": "gci",
      "url": "https://example.org/approved.gci",
      "size": 8256,
      "sha256": "<64 lowercase hexadecimal digits>"
    }
  ]
}
```

That is a structural illustration, not a real downloadable save. Real,
size- and hash-pinned research candidates are in `tools/saves-example/`.

| Field | Rules |
| --- | --- |
| `id` | 1–47 ASCII letters, digits, `_` or `-`; unique ignoring case. |
| `game_id` | Exactly six uppercase ASCII letters/digits. Wii disc title IDs use the first four bytes; GameCube checks all six against the GCI header too. |
| `game_ids` | Optional array of 1–8 unique full IDs, including `game_id`. All must share the three-byte title family and two-byte maker. Wii only may list multiple regions, with explicit portability evidence. The menu binds installation to the selected ID; unlisted regions never match. GameCube permits only its one exact header ID. |
| `platform` | `wii` or `gamecube`. |
| `title`, `author`, `description` | Nonempty UTF-8; respectively at most 79, 63, 239 bytes. No layout commands; control characters are cleaned by the Wii parser. |
| `page`, `url` | HTTPS, at most 299 ASCII bytes; plain hostname, no user, port, numeric IP, backslash, fragment or whitespace. File redirects may stay on HTTPS across hosts; list redirects must stay on the same host. |
| `format` | `gci` for GameCube; `wii-zip` for extracted Wii saves; `data.bin` for a Wii entry that is shown as **Get it on a PC** and is never downloaded or installed by the Wii. |
| `root` | Optional for `wii-zip`: exact folder prefix ending in `/`, at most 127 ASCII bytes; `""` means the archive root. Absent means `data/` for older catalogues. It is stripped before applying the NAND filename rules. Never a destination path. |
| `member` | Optional for `gci`: exact path of the sole GCI inside the source ZIP, at most 199 ASCII bytes. Without it the source must be a loose GCI. No other format uses it. |
| `size`, `sha256` | Exact approved byte length (1–32 MiB) and 64 lowercase hex digits. An unpacked GCI must be 64 bytes plus 1–2043 blocks of 8192 bytes. ZIP pins describe the whole source ZIP. The byte length and digest must both match before installation. |
| `files`, `unpacked_size` | Required for `wii-zip`, respectively 1–512 and 1–64 MiB. Exact totals of regular files. Must be absent or zero for other formats. |

More than eight region IDs rejects the list before reading further.
Unknown fields are skipped with bounded nesting; duplicate JSON keys and
structurally broken lists are rejected. Bad individual records are left
out and logged. Repeated record IDs keep the first. More than 512 records
rejects the list. Records stay in one fixed-size MEM2 session cache.

The catalogue's HTTPS connection is the trust root. SHA-256 protects the
approved file, not a compromised catalogue; protect publishing access,
require reviews and run the checks in CI.

### Extracted Wii ZIPs

Use stored or deflated ordinary ZIPs, not encrypted, split or ZIP64:

```text
data/banner.bin
data/<the game's actual save files>
data/<optional subfolders>/<files>
```

An explicit `root`, such as `private/wii/title/RMGE/`, can select the
folder holding banner.bin instead. Generic archive paths must be relative,
printable ASCII, under 200 bytes and at most sixteen components, with no
empty/dot/hidden components, backslashes, drive prefixes, FAT special
characters or trailing dots/spaces. These rules apply to roots, members
and all archive entries, including safe unused readmes. Root and member
strings are read exactly, without trimming or cleaning control characters.
Case aliases, duplicate entries and file/directory conflicts are rejected
throughout the archive. At most 1024 entries, 512 regular files and 64 MiB
total expansion are permitted, even for files outside a selected root.

Include the complete save folder, with its original banner.bin (WIBN
header) and save files. The selected folder must contain no title.tmd,
tickets, UID list, shared contents,
NAND settings or other titles. A path component is 1–12 printable ASCII
bytes, cannot start with a dot, end in a dot/space, or contain separators,
drive prefixes or FAT special characters. The full NAND path must fit
63 bytes, at most eight components below data. Paths, case aliases,
duplicate entries, file/directory conflicts, totals, ZIP CRCs and banner
magic are checked before the save becomes live. All entry paths and
metadata are checked, including entries that are
not installed. CRC and actual expansion checks apply to installed files.

For an approved data.bin submission, have the author extract their own
save using a tool they already use and package the resulting data folder.
The catalogue builder does not supply decryption keys. A data.bin-only
entry can point players to its source page for PC conversion instead.

## Installation and backups

The download streams to `sd:/riftwii/save-download/download.tmp`. No
archive-sized buffer is allocated. The network worker checks the exact
length and SHA-256, then installs while the menu keeps drawing. Card write
protection holds HOME/power exits until the worker has finished. Cancel
is checked while downloading and extracting, and just before committing;
once the short final moves begin they finish or roll back. Space checks
reserve room for the download and the new files beside existing ones.

**GameCube:** use the current game's RiftWii SD card (own, shared, named,
or SD slot A with a real card in B). Other card modes ask the player to
choose a RiftWii card first. A ZIP GCI is streamed into a checked, bounded buffer, checked for its
exact length and CRC, then follows the same import flow as a loose GCI.
The GCI must match the selected game and the card's text encoding. The card is changed in memory first; a full card or
malformed GCI leaves it unchanged. An entry with the same game, maker and
internal filename is replaced; other entries are kept. Confirmation is
always required. The old card is backed up through the existing backup
system even when automatic backups are disabled. Its usual five-backup
retention applies. The new card is staged and committed using the existing
recoverable `.old` card replacement. These backups appear in **Restore an
older save**. A fresh card uses RiftWii's ordinary card format; the loader
restores its flash identity on launch as it does for imported cards.

**Wii:** resolve the current save choice, including Settings' default.
Separate and Fresh use their existing `sd:/riftwii/saves/<ID>/clone` or
`fresh` emulated NAND root. Emulated NAND uses that game's shared SD/USB
root, through the writable USB partition mount when needed. For a game
ID such as RAAE01, the only writable title subtree is
`<root>/title/00010000/52414145/`. Files go first to `data.download/`.
After validation the old `data/` is renamed to
`data.before-YYYYMMDD-HHMMSS-N/` alongside it, then the staged folder takes
the name `data/`. A failed second move restores the first. Backup names
never overwrite one another, even with an incorrect clock or repeated
installs in one second. Backups are retained until the player removes
them. To restore one, exit RiftWii and rename the current data folder out
of the way, then rename the desired backup to `data` on a PC.

Downloads never write real NAND. With **Saves: Wii**, the menu explains
this and asks the player to choose an SD/emulated location first; no
setting is silently changed. The game still creates/prepares its title
metadata through the normal launch path. A pack with its own save folder
is not silently redirected to the stock game's save.

After a power loss, keep backups. An abandoned `data.download` is reported
and retained, so it cannot be mistaken for a completed save. If the cut
was between the two Wii folder renames, restore the latest `data.before-*`
to `data` **before launching**; otherwise the launch may create a new save.
A failed rollback logs the exact backup path. No claim of power-loss
atomicity across two FAT renames is made.

## The catalogue repository to create

Create a separate RiftWii organization repository for saves; no repository
or site was created by this task. It should contain:

- `manifests/<id>.json`, one reviewed entry per exact game/version and save.
  Use the published fields above plus the private review block below.
- Optional `manifests/files/<id>.gci`, `.zip` or `.bin` only with explicit
  permission and a review of what the file contains. Use `file` instead of
  `url` in such a manifest. Do not store unreviewed uploads in the code repo.
- `README.md` with contributor instructions, attribution, permission and
  personal-information policy, takedown contact and backup warning.
- A reviewed CI workflow that runs the tool from a pinned RiftWii commit,
  builds `save_list_check`, runs `check` and `build`, then publishes the
  resulting site directory at `https://riftwii.com/saves/`. The domain,
  TLS and publishing route need to be set up by the maintainer.
- Branch protection and human review before publishing changes to pins,
  approval or workflow files. Save payload licenses are recorded per
  submission; the tool's GPL license is not a blanket payload license.

Manifest-only review fields, omitted from the downloaded catalogue:

```json
"approval": {
  "approved": false,
  "evidence": "https://example.org/author-permission",
  "reviewer": "",
  "note": "Record permission, authorship, privacy checks and in-game verification."
}
```

Set approved to true and name the reviewer only after the review happened.
The builder requires true, an evidence URL and a reviewer; it cannot judge
the legal sufficiency of that evidence. The three bundled examples keep
approved false deliberately. A code reviewer must not promote them just
to make `build` pass.

```text
py -3.13 tools/save_list.py pin <manifests/id.json>
py -3.13 tools/save_list.py --check-tool build-host/save_list_check.exe check <manifests>
py -3.13 tools/save_list.py --check-tool build-host/save_list_check.exe build <manifests> <site>
py -3.13 tools/save_list.py --check-tool build-host/save_list_check.exe check tools/saves-example --allow-pending
```

`pin` fetches and checks a payload, writes size/SHA-256 and ZIP totals,
and leaves approval unchanged. `check` fetches again and verifies all
pins and package rules. `build` does the same and emits saves.json plus
explicitly hosted files; it refuses pending approvals. Pass `--check-tool`
in CI so the generated list also passes the Wii's actual parser. The
default site base is `https://riftwii.com/saves/`; `--base-url` can change
asset URLs for a staging site. A bad pin never silently advances.

## Checks and limits

`tests/saves_tests.cpp` covers catalogue rejection, region matching, unsafe
paths, duplicate/colliding ZIP entries, GCI replacement and unchanged cards
on failure, mock move/rollback failures, Wii placement, retained backups,
same-second backup naming, cancellation, CRC failure and other-title
isolation. All save bytes in those tests are generated synthetic fixtures.

Do not describe real-Wii behavior as tested by host checks. Check first
install and replacement on own/shared/named GameCube cards, NAND clone and
Fresh saves, shared SD and USB emulated NAND, cancellation, full/missing
cards, invalid digest, disabled emulation, backup restoration and a real
game reading the result. Check both 4:3 and 16:9 and overscan. Dolphin was
explicitly forbidden for this task; it was not started. Some games bind
saves to card identity or use multiple save files; a generic GCI import
cannot promise every game's portability. Catalogue reviewers must test
each save's advertised progress and supported revision.
