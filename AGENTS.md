# AGENTS.md

Instructions for coding agents working on RiftWii. People start with
[README.md](README.md); this file is the short version of what an agent needs
to build, test and change the code without breaking the rules below. The
details live in `docs/`, and this file points there rather than repeating them.

## Project overview

RiftWii is a Wii homebrew loader for Riivolution-format mod packs, written
from scratch (clean room). It starts games from the disc, a USB drive or the
SD card (WBFS, ISO or Dolphin's RVZ), patches them while they load and keeps a
small runtime inside the running game to serve the pack's files. It is
GPL-3.0-or-later.

Where things are (the full map is [docs/MAP.md](docs/MAP.md), the design
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)):

| Path | What it holds |
| --- | --- |
| `src/`, `include/riftwii/` | Platform-free library, built for the host (tests) and the Wii. `src/` is sorted by area: `patching/` (XML parser, patch planner, hooks, game fixes), `disc/`, `fs/` (FAT32, NTFS, WBFS, RVZ), `net/`, `art/`, `data/`; the headers stay in one flat `include/riftwii/`. |
| `wii/` | The Wii app, sorted by area: `core/`, `menu/` (libwiigui), `launch/` (`boot.cpp`), `hooks/`, `controllers/`, `storage/` (USB/SD), `online/`; translations in `wii/lang/`. |
| `runtime/` | C code that runs inside the game (resident runtime, pad hook, screenshots, crash recorder, virtual SD). Position-independent blobs. |
| `channel/` | The Wii Menu channel: forwarder, loader, installer. |
| `tests/` | One host test suite per area (`<area>_tests.cpp`). |
| `tools/` | Python and C++ helpers (translations, fonts, theme kit, update pack, Dolphin harness). |
| `themes/` | Built-in themes and the theme kit. |
| `vendor-*/` | Third-party code, kept whole with its own license headers. Don't edit unless the change is recorded in `NOTICE.md`. |
| `build-*/`, `riftwii.dol`, `riftwii.elf` | Build output. Never commit it. |

## Build and test commands

Everything builds on Windows inside devkitPro's MSYS2 shell. Plain Git Bash
can't find the C++ headers, so wrap commands like this:

```bash
MSYSTEM=MSYS /c/devkitPro/msys2/usr/bin/bash -lc "cd '/d/AI Projects/Riftwii' && <command>"
```

Host library and tests (run them after every change to `src/`, `include/`,
`runtime/` or `tests/`):

```bash
cmake -S . -B build-host -G Ninja        # once
cmake --build build-host
ctest --test-dir build-host -j8 --output-on-failure
```

The Wii app (`riftwii.dol`); build it after every change to `wii/`, `runtime/`
or anything the DOL embeds:

```bash
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
make -f Makefile.wii -j8
```

The channel: `make -f Makefile.channel`. Translations:
`py -3.13 tools/lang_source.py` checks the tables against the sources and
rewrites `wii/lang/*.po`.

Expected noise: pugixml's `-Wmaybe-uninitialized` warning and an unused
variable in `vendor-libgui/source/video.cpp`. Any other new warning is yours
to fix.

CI ([.github/workflows/ci.yml](.github/workflows/ci.yml)) runs the host tests
on Linux, the Wii and channel builds, and the translation checks on every push
and pull request. A change is ready when all three would pass.

There is no configured formatter or linter (no `.clang-format`). Match the
surrounding code by hand, and say so when you hand work back.

## Testing in Dolphin

Host tests prove the logic. Anything a player could see or a game could feel
also gets a run in Dolphin before it counts as done: the menu, a launch, a
pack's patches, the runtime, saves, controllers.

- The isolated setup and scripts are in `build-dolphin/`
  ([docs/HARNESS.md](docs/HARNESS.md)). `felk-run.sh <card> <dol> [disc]
  [sd bytes]` starts Felk's scriptable Dolphin. `felk-stop.sh` stops it
  gracefully, which matters because the SD folder only syncs on a clean close.
  `play.py` drives the Wii Remote, and `dolbridge.py` reads and writes memory.
- A test card is a folder, `build-dolphin/user/Load/WiiSDSync-<card>/`.
  Dolphin rebuilds the SD image from it at every boot.
  `sd:/riftwii/autorun.txt` skips the menu (`disc`, `xml <path>`, `boot`,
  `launch`, ... see [docs/DEVELOPING.md](docs/DEVELOPING.md)) and logs to
  `sd:/riftwii/autorun.log`.
- Give a test card a `riftwii/titles-en.txt`. Without one, the game-names
  download can overlap an autorun boot.
- Report evidence, not impressions: log lines, memory reads, screenshots.
- Dolphin can't run a d2x cIOS. SD and USB game images, the GameCube adapter
  and real-hardware timing can only be checked on a Wii; say so instead of
  calling them tested.

## Code style

Readability beats brevity. For all code you write or change:

- Follow the file's own conventions. `wii/menu/rift_menu.cpp` and
  `vendor-libgui/` indent with tabs (libwiigui's style); everything else uses
  four spaces. C++ is in `namespace riftwii` (`riftwii::wii` in `wii/`);
  runtime code is C, prefixed by its module (`rtfs_`, `rtfat_`, `rtvsd_`).
- Use descriptive names, plain control flow and focused functions. Name the
  intermediate values that explain a step. Avoid dense one-liners and several
  statements on one line.
- Comments explain why: intent, assumptions, hardware facts, which game
  needed it. Keep them true when the code changes, and don't restate the code.
- Runtime code runs inside someone else's game. It gets no heap and no libc
  beyond what is already there, and stays small and position-independent.
  `Makefile.wii` checks the blobs.
- `Makefile.wii` compiles every source into one flat folder, so no two
  sources may share a base name.
- Nothing hardcoded for one mod. Per-game fixes live in the game lists of
  `src/patching/gxpatches.cpp`, with the reason in a comment.
- Write RiftWii's own implementation of an idea rather than porting someone
  else's code, and use RiftWii's own names in the UI and logs.

## Player-facing text

- Every menu string is English in the source, goes through `tr()`, and
  needs its six translations in `tools/lang_source.py` (Spanish, Japanese,
  Portuguese, Italian, Korean, French). Run the script so the `.po` files
  follow. New Japanese text must exist in the menu font
  (`tools/make_menu_font.py`).
- US spelling "license" in anything players read (existing code identifiers
  such as `licence_zst` stay as they are).
- Log lines (`logf`) are read by testers and by us in problem reports. Write
  them as plain sentences that say what happened and why.

## Clean room, licensing and security

- No Riivolution source code, ever: not copied, not read. Riivolution's
  behaviour comes from its public patch-format wiki, Dolphin's independent
  implementation and testing, as listed under "Design provenance" in
  [NOTICE.md](NOTICE.md).
- No Nintendo keys or assets in the repository. Keys are read from the
  player's own Wii at run time and kept in memory only.
- Every new source file starts with SPDX lines:
  `SPDX-FileCopyrightText: 2026 RiftWii contributors` (plus one line per
  other holder whose work the file follows) and
  `SPDX-License-Identifier: GPL-3.0-or-later`.
- Third-party code or a design learned from someone's code gets a row in
  `NOTICE.md` (what, where from with the commit, license), a line in the
  README's Credits and in `wii/menu/credits.cpp`. `tools/attribution_audit.py
  <other tree>` compares RiftWii's code with another project's.
- Anything RiftWii downloads or reads from a network (RiiFS servers, update
  packs, GameTDB, cheats) is untrusted: bound every size and count, and never
  let a name climb out of its folder.
- Never commit secrets (webhooks, tokens) or files from a tester's or the
  maintainer's console. Problem reports and console dumps stay out of the
  repository.

## Commits and pull requests

- Commit as RiftWii: `git -c user.name=riftwii -c user.email=riftwii@localhost commit ...`.
  Add no AI attribution or co-author lines.
- The subject is a full sentence or two: what changed for players or for the
  code, why, and how it was checked, e.g. "(Dolphin: ...)". Match
  `git log` in tone.
- Several agents and people may work in this tree at once. Check
  `git status` before you start, never `git stash`, never reset or revert
  someone else's changes, and commit only the files you changed (`git add
  <paths>`, never `git add -A`).
- Branch for anything larger than a fix; don't push or publish unless asked.
- Releases (version scheme `YYMM-N`, three release assets, notes with
  screenshots, the stable build flag) follow
  [docs/DEVELOPING.md](docs/DEVELOPING.md#releasing). Agents never publish a
  release on their own.

## More documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how a launch works, memory
  layout, the runtime.
- [docs/DEVELOPING.md](docs/DEVELOPING.md): build, tests, Dolphin, logs,
  `debug_off` bisecting, releasing.
- [docs/MAP.md](docs/MAP.md): every file and folder.
- [docs/TESTING.md](docs/TESTING.md): the release-candidate test plan.
- [docs/RVZ.md](docs/RVZ.md), [docs/RIIFS.md](docs/RIIFS.md),
  [docs/THEMES.md](docs/THEMES.md), [docs/HEADLESS.md](docs/HEADLESS.md),
  [docs/CHANNEL.md](docs/CHANNEL.md): one subsystem each.
