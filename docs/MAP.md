# Code map

Every file in the repository, where it sits and what it is for, so a
fork can find its way around without reading everything first. For how
the pieces work together (a launch from menu to game, the memory map),
read [ARCHITECTURE.md](ARCHITECTURE.md); for building and testing,
[DEVELOPING.md](DEVELOPING.md).

## Start here

1. [README.md](../README.md): what RiftWii is.
2. [GUIDE.md](GUIDE.md): what it does, from the player's side. Most
   features in the code have a section there.
3. [ARCHITECTURE.md](ARCHITECTURE.md): the four phases of a launch and
   the memory map.
4. This page: where each piece lives.
5. [DEVELOPING.md](DEVELOPING.md): building, the host tests, Dolphin
   runs, releasing, and the ground rules.

The layout in one sentence: `src/` and `include/riftwii/` are portable
C++ that the host tests cover, `wii/` is the Wii app that uses them,
`runtime/` is the small freestanding code that runs inside the game,
and `vendor-*` is third-party code.

## I want to change...

| Topic | Start in |
| --- | --- |
| A menu screen (Home, a game's page, Mods, Cheats, Settings) | `wii/rift_menu.cpp` |
| How a list or the tile grid looks or scrolls | `wii/gui_flowlist.cpp`, `wii/gui_gamegrid.cpp`, `wii/skin.cpp` |
| Menu text and its translations | `tools/lang_source.py` (the table), `wii/i18n.cpp`, `wii/lang/*.po` |
| Settings and what they mean | `src/settingsfile.cpp`, `wii/loadersettings.cpp`, the Settings screen in `wii/rift_menu.cpp` |
| Finding games on the SD card and USB drives | `wii/usbcatalog.cpp`, `src/usbgame.cpp`, `src/imagevolume.cpp`, `src/fat32.cpp`, `src/ntfs.cpp` |
| Riivolution packs: reading the XML | `src/patch.cpp` |
| Riivolution packs: what a launch does with them | `wii/modplan.cpp`, `src/apply.cpp`, `src/expand.cpp`, `src/redirect.cpp`, `src/mempatch.cpp` |
| Starting a game (IOS, disc, apploader, patches) | `wii/boot.cpp` |
| What runs inside a game and serves the packs | `runtime/resident/rt_hook.c`, `runtime/rtable.c`, `wii/resident.cpp` |
| Saves on the SD card instead of the NAND | `runtime/rtfs.c`, `runtime/rtfat.c`, `prepare_savegame` in `wii/boot.cpp` |
| Code builds (Project+, REX) and their `gameconfig.txt` | `wii/codebuilds.cpp`, `src/gameconfig.cpp`, `src/codehook.cpp`, `vendor-gecko/` |
| The virtual SD card (`sd.raw`) | `runtime/rtvsd.c` (the card), `runtime/vsd/` (the in-game blob), `wii/vsdhook.cpp` (install), `wii/vsdimage.cpp` (the menu's view) |
| Cheats | `src/cheats.cpp`, `wii/gameextras.cpp` |
| RVZ games | `src/rvz.cpp`, `runtime/rtrvz.c`, `runtime/resident/rt_zstd.c`, `docs/RVZ.md` |
| The GameCube controller adapter | `runtime/rtgcad.c` (the driver), `wii/gcadapter.cpp` (menu), `runtime/pad/` and `wii/padhook.cpp` (games) |
| Screenshots | `wii/screenshot.cpp`, `runtime/shot/`, `wii/shothook.cpp`, `src/shotfile.cpp`, `src/pngencode.cpp` |
| Problem reports and the QR code | `wii/reportsend.cpp`, `src/problemreport.cpp`, `src/qrcode.cpp` |
| A game's crash record | `runtime/fault/`, `runtime/rtfault.c`, `wii/faulthook.cpp`, `src/gamefault.cpp` |
| RiftWii's own crash screen | `wii/crash.cpp` |
| Online play (Wiimmfi, WiiLink WFC) | `src/wfcpatch.cpp`, `wii/wfc.cpp`, `vendor-wwfc/` |
| Video mode, borders, deflicker | `src/videopatch.cpp` |
| Game language | `src/gamelang.cpp` |
| Downloads: game names, covers, cheats, updates | `wii/online.cpp`, `src/http.cpp`, `wii/netsock.cpp`, `wii/tls.cpp`, `src/titles.cpp`, `src/coverart.cpp`, `src/update.cpp` |
| Packs served from a PC (RiiFS) | `wii/netpacks.cpp`, `src/riifs.cpp`, `src/riifs_sync.cpp`, `docs/RIIFS.md` |
| Returning to RiftWii (or another title) when a game exits | `src/returnto.cpp`, `apply_return_to` in `wii/boot.cpp`, `wii/restart.cpp` |
| Being started by another loader | `wii/headless.cpp`, `src/launchargs.cpp`, `docs/HEADLESS.md` |
| The Wii Menu channel | `channel/`, `wii/channel.cpp`, `tools/make_channel.py`, `docs/CHANNEL.md` |
| Memory the menu may use | `wii/memlimits.cpp` |

## Top level

| File | What |
| --- | --- |
| `README.md` | The project page: what it is, installing, credits |
| `NOTICE.md` | Every third-party piece RiftWii contains or follows: origin, licence, what was used |
| `LICENSE` | GNU GPL version 3 |
| `CMakeLists.txt` | The host build: the portable library, the runtime's C code and the test suites (`ctest`) |
| `Makefile.wii` | The Wii app (`riftwii.dol`); builds the runtime blobs and BearSSL first and embeds them |
| `Makefile.runtime` | The in-game blobs (resident runtime, RVZ runtime, adapter, screenshot, crash and virtual SD card), each checked position independent |
| `Makefile.bearssl` | BearSSL for the Wii (https for the update check and problem reports) |
| `Makefile.channel` | The Wii Menu channel and its installer |
| `hbc/` | The Homebrew Channel files: `meta.xml` (name, version), `icon.png`, `music.ogg` (the menu's music) |

## `wii/`: the Wii app

The menu, the launch and everything that talks to IOS. Not host-tested:
it links libogc. Each `.cpp` has a `.hpp` that describes it.

| File | What |
| --- | --- |
| `main.cpp` | Start-up: video, pads, SD card, crash handler, the menu, and what happens when the player leaves |
| `rift_menu.cpp` | Every menu screen, the GUI thread, the HOME menu, the screen dimming |
| `menu.h` | The menu's entry points for `main.cpp` |
| `gui_flowlist.cpp` | The row lists (game page, Mods, Cheats, Settings) |
| `gui_gamegrid.cpp` | The Home screen's tile grid and its cover textures |
| `skin.cpp` | The menu's artwork, painted at start (no image files) |
| `menumusic.cpp` | The background music |
| `i18n.cpp` | Translations (the built-in `.po` files, or one on the card) |
| `lang/*.po` | The translations, written by `tools/lang_source.py` |
| `font/` | The menu font's source (`rounded.ttf`, OFL) |
| `assets/` | Built-in binaries: the cut-down menu font and the GPL text (`tools/make_menu_font.py`, `tools/make_licence.py`) |
| `ftnopng.c` | Keeps FreeType from pulling in libpng |
| `console.cpp` | libogc's text console, for the launch screen and errors |
| `progress.cpp` | The launch screen's progress line |
| `crash.cpp` | RiftWii's crash screen and `crash.txt` |
| `credits.cpp` | Settings > Credits and licence |
| `log.cpp` | The log (`session.log`, `boot.log`) |
| `loadersettings.cpp` | Settings as the app reads and writes them |
| `memlimits.cpp` | Keeps the app's heap out of memory a launch overwrites |
| `menuios.cpp` | Which IOS the menu runs under (58 or a d2x cIOS slot) |
| `ios_reload.cpp` | IOS reloads, and telling Dolphin apart from a Wii |
| `restart.cpp` | Starting RiftWii again without the Homebrew Channel (after a game, or the channel) |
| `channel.cpp` | Offering and installing the Wii Menu channel |
| `frontend.cpp` | The menu's state for a game: its packs, code builds and saved choices; Start's checks |
| `gameextras.cpp` | The game page's extras: cheats, video, language, online server, code builds, what a launch carries |
| `codebuilds.cpp` | Finding code builds (on the card and inside `sd.raw`) and preparing their codes |
| `modplan.cpp` | Compiling the chosen packs into boot options |
| `netpacks.cpp` | Packs from a RiiFS server |
| `usbcatalog.cpp` | Game images on the SD card and USB drives, the USB drive's volume, RVZ serving, the menu's file reads there |
| `umsdev.cpp` | d2x's USB device (`/dev/usb2`), read by the launch and handed to the runtime |
| `sdio.cpp` | Raw SD card access (`/dev/sdio/slot0`) after libogc's driver is gone |
| `d2xsd.cpp` | d2x's SD device (`/dev/sdio/sdhc`), when the game itself is on the card |
| `sdfile.cpp` | Where a file's bytes lie on the card |
| `di.cpp` | The disc drive (`/dev/di`) |
| `boot.cpp` | The launch: IOS, the disc or image, the apploader, patches, the blobs, low memory, the jump |
| `resident.cpp` | Installing the resident runtime |
| `padhook.cpp` | Installing the GameCube adapter blob |
| `shothook.cpp` | Installing the screenshot blob |
| `faulthook.cpp` | Installing the crash blob (and its retail BCA answer) |
| `vsdhook.cpp` | Finding `sd.raw` for a launch and installing the virtual SD card blob |
| `vsdimage.cpp` | `sd.raw` as the menu's read-only `vsd:` drive |
| `gcadapter.cpp` | The GameCube adapter in the menu and on its test page |
| `screenshot.cpp` | Menu screenshots and importing the games' ones |
| `covers.cpp` | Cover art: download, cache, textures |
| `online.cpp` | Downloads (names, covers, cheats, the update check) |
| `netsock.cpp` | Network sockets |
| `tls.cpp`, `tlsroots.c` | https with BearSSL, and the certificates it trusts |
| `reportsend.cpp` | Gathering and sending problem reports; log rotation; importing a game's crash |
| `wfc.cpp` | Patching the game for an online server |
| `headless.cpp` | Launches another loader asks for |
| `autorun.cpp` | Unattended test runs (`sd:/riftwii/autorun.txt`) |
| `guiscript.cpp` | Scripted menu input for tests (`sd:/riftwii/guiscript.txt`) |

## `src/` and `include/riftwii/`: portable code

Plain C++17 with no Wii headers, host-tested. Each `src/x.cpp`
implements `include/riftwii/x.hpp`, whose comments document it.

| Name | What |
| --- | --- |
| `patch` | Riivolution XML: parsing, and a plan for one disc (choices, params, placeholders) |
| `apply`, `overlay`, `source` | Composing replaced files from byte sources |
| `expand` | Folder patches into file patches against the disc's file table |
| `redirect` | The table of where each replaced file's bytes come from |
| `mempatch` | Memory patches (plain, search, ocarina) and the regions they may touch |
| `launch` | `LaunchModel`: the packs and code builds for a game and their choices |
| `fst` | The disc's file system table |
| `disc` | Wii disc structures (header, partitions, apploader) |
| `dol` | The DOL executable header |
| `symsearch` | Finding SDK functions (IPC, PAD, the crash handler) in a game's code |
| `hook` | Branch encoding and placing the resident runtime |
| `codehook` | Where the Gecko code handler is called from |
| `gameconfig` | `gameconfig.txt` / `gc.txt` for code builds |
| `cheats` | Cheat files and GCT building |
| `fat32`, `ntfs`, `imagevolume` | Read-only FAT32 and NTFS walkers, and a drive's volume |
| `usbgame` | WBFS, split WBFS and ISO images; the d2x fragment list |
| `rvz`, `sha1` | RVZ images and their hashes |
| `titles` | Game names |
| `coverart`, `pngdecode`, `canvas` | Cover art, a PNG reader, the software canvas the artwork is painted on |
| `http` | HTTP GET and POST |
| `update` | The update check |
| `riifs`, `riifs_sync` | The RiiFS client and its mirror |
| `wfcpatch` | Online server patches |
| `videopatch` | Video settings for games |
| `gamelang` | The game language |
| `settingsfile` | `settings.txt` |
| `langfile` | Translation files |
| `playhistory` | `history.txt` |
| `returnto` | Returning to RiftWii from a game |
| `launchargs` | Arguments from another loader |
| `cardlog` | The runtime's SD card log, as text |
| `shotfile`, `pngencode` | Screenshot files and a PNG writer |
| `problemreport` | A problem report's text |
| `qrcode` | QR codes |
| `gamefault` | A game's crash record, as text |

## `runtime/`: code that runs inside the game

Freestanding C99 and assembly: no libc, no data sections, position
independent (each blob is linked at two addresses and compared). The
shared C parts (`runtime/*.c`) are also built into the host tests.

| Path | What |
| --- | --- |
| `resident/` | The resident runtime: IPC hooks, serving pack files from the card, USB drive or memory, save redirection, Riivolution's `file` device, RVZ reading. `rt_entry.S` (header, trampolines), `rt_hook.c`, `rt_hook.h` (the layout the loader fills), `rt.ld`, `rt_rvz.ld` and `rt_zstd.c` for the RVZ build |
| `rtable.c` | The redirect table the resident walks |
| `rtfat.c`, `rtfs.c` | A resumable FAT32 engine, and the NAND (ISFS) requests on top of it, for saves on the card |
| `rtrvz.c` | Unpacking RVZ groups |
| `pad/`, `rtgcad.c` | The GameCube adapter blob and its WUP-028 driver (also used by the menu) |
| `shot/`, `rtshot.c` | The in-game screenshot blob |
| `fault/`, `rtfault.c` | The crash blob: the crash record to the NAND; also answers the BCA read |
| `vsd/`, `rtvsd.c` | The virtual SD card blob and the card it answers as |

Each blob directory holds `<name>_entry.S` (a header of offsets the
loader reads, the trampolines), `<name>_hook.c`, `<name>_hook.h` (the
header and context layout) and `<name>.ld`. The Wii side that installs
each one is the matching `wii/*hook.cpp`.

## `tests/`

One suite per area, `tests/<name>_tests.cpp`, registered in
`CMakeLists.txt` and run with `ctest`. `fat32_image.hpp` builds FAT32
images in memory, `fixtures/` holds XML and RVZ inputs, `qrcode_ref.inc`
is reference data from python-qrcode.

## `tools/`

| File | What |
| --- | --- |
| `lang_source.py` | The translation table: writes `wii/lang/*.po` and checks every string still appears in the code |
| `make_menu_font.py`, `make_licence.py`, `make_sounds.py` | Build-time assets |
| `make_channel.py`, `make_channel_art.py`, `preview_channel.py` | The Wii Menu channel's WAD and art |
| `rtreloc.py` | Relocations for the RVZ runtime blob |
| `ipcscan.cpp`, `rvzstub.cpp` | Host tools: the IPC search on a game's DOL; an RVZ's stub disc for Dolphin |
| `rvz/make_test_disc.py` | The RVZ test fixtures |
| `dolphin/` | Dolphin test harness: `run.sh`, the USB Gecko log reader, a RiiFS test server, movie and screenshot helpers (see `docs/HARNESS.md`) |

## `channel/`

The Wii Menu channel (`docs/CHANNEL.md`): `forwarder/` (the channel that
starts RiftWii), `loader/` (its boot program), `installer/` (the app
that installs it), `common/` (starting a DOL), `art/` (its artwork,
drawn by `tools/make_channel_art.py`).

## `docs/`

Listed in [docs/README.md](README.md), with the history documents that
code comments cite.

## `vendor-*`: third-party code

| Folder | What |
| --- | --- |
| `vendor-libgui/` | libwiigui (menu widgets, video, input), with RiftWii's changes; `wiidrc.*` is the Wii U GamePad |
| `vendor-pugixml/` | XML parsing |
| `vendor-bearssl/` | TLS |
| `vendor-zstd/` | The Zstandard decoder (RVZ) |
| `vendor-gecko/` | The Gecko code handler, with its source |
| `vendor-wwfc/` | WiiLink WFC's patcher pieces |

Licences and what RiftWii changed are in [NOTICE.md](../NOTICE.md).

## Not in the repository

Build output, all git-ignored: `build-host/` (CMake), `build_wii/` and
`riftwii.dol` / `.elf` (`Makefile.wii`), `build-runtime/` (the blobs),
`build-bearssl/`, `build-channel/`. `build-dolphin/` is the local Dolphin
test setup (`docs/HARNESS.md`).

## On the SD card

What RiftWii keeps in `sd:/riftwii/` is listed in
[ARCHITECTURE.md](ARCHITECTURE.md#files-on-the-sd-card).

## Conventions

- No two files share a base name across `src/`, `wii/` and `runtime/`
  (devkitPPC builds into one flat folder).
- Logic that can run on a PC goes in `src/` with tests; `wii/` is glue
  to IOS and the screen.
- A new in-game blob follows the existing ones: its own `runtime/<name>/`,
  rules in `Makefile.runtime`, a `wii/<name>hook.cpp`, an entry in
  `ARCHITECTURE.md`'s memory map.
- New third-party code, or code written from someone else's, gets a row
  in `NOTICE.md`.
