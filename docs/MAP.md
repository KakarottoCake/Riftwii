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
| A menu screen (Home, a game's page, Mods, Cheats, Settings) | `wii/menu/rift_menu.cpp` |
| How a list or the tile grid looks or scrolls | `wii/menu/gui_flowlist.cpp`, `wii/menu/gui_gamegrid.cpp`, `wii/menu/skin.cpp` |
| The game page's and Settings' tiles, the prompts bar | `wii/menu/gui_tiles.cpp`, `wii/menu/gui_prompts.cpp`, `wii/menu/rift_menu.cpp` (`MenuHome`, `SettingsHub`), `src/art/skinpaint.cpp` (tile frames and icons) |
| Home's shelf, carousel and 3D covers ring; their boxes and 3D cover pictures | `wii/menu/gui_shelf.cpp`, `wii/menu/boxart.cpp`, `wii/menu/cover3d.cpp`, `src/art/coverart.cpp` |
| How screens, pages and popups come and go (fades, zooms, slides) | `wii/menu/transition.cpp`, the camera in `vendor-libgui/source/video.cpp` |
| Menu themes (colours, pictures, music) | `src/art/theme.cpp` (theme.ini), `src/art/skinpaint.cpp` (the painted pictures), `wii/menu/menutheme.cpp`, `wii/menu/skin.cpp`, `themes/`, [THEMES.md](THEMES.md) |
| The theme kit download | `tools/theme_kit.cpp`, `tools/make_theme_kit.py`, `themes/kit/README.txt` |
| Menu text and its translations | `tools/lang_source.py` (the table), `wii/core/i18n.cpp`, `wii/lang/*.po` |
| Settings and what they mean | `src/data/settingsfile.cpp`, `wii/core/loadersettings.cpp`, the Settings screen in `wii/menu/rift_menu.cpp` |
| Finding games on the SD card and USB drives | `wii/storage/usbcatalog.cpp`, `src/fs/usbgame.cpp`, `src/fs/imagevolume.cpp`, `src/fs/fat32.cpp`, `src/fs/ntfs.cpp`, `src/fs/wbfspart.cpp` |
| Riivolution packs: reading the XML | `src/patching/patch.cpp` |
| Riivolution packs: what a launch does with them | `wii/launch/modplan.cpp`, `src/patching/apply.cpp`, `src/patching/expand.cpp`, `src/patching/redirect.cpp`, `src/patching/mempatch.cpp` |
| Starting a game (IOS, disc, apploader, patches) | `wii/launch/boot.cpp` |
| What runs inside a game and serves the packs | `runtime/resident/rt_hook.c`, `runtime/rtable.c`, `wii/launch/resident.cpp` |
| Saves on the SD card instead of the NAND | `runtime/rtfs.c`, `runtime/rtfat.c`, `prepare_savegame` in `wii/launch/boot.cpp` |
| Code builds (Project+, REX) and their `gameconfig.txt` | `wii/launch/codebuilds.cpp`, `src/patching/gameconfig.cpp`, `src/patching/codehook.cpp`, `vendor-gecko/`; cheats in a second executable: `wii/launch/dolswitch_stub.S` |
| The virtual SD card (`sd.raw`) | `runtime/rtvsd.c` (the card), `runtime/vsd/` (the in-game blob), `wii/hooks/vsdhook.cpp` (install), `wii/hooks/vsdimage.cpp` (the menu's view) |
| Cheats | `src/data/cheats.cpp`, `wii/menu/gameextras.cpp` |
| RVZ games | `src/fs/rvz.cpp`, `runtime/rtrvz.c`, `runtime/resident/rt_zstd.c`, `docs/RVZ.md` |
| The GameCube controller adapter | `runtime/rtgcad.c` (the driver), `wii/controllers/gcadapter.cpp` (menu), `runtime/pad/` and `wii/hooks/padhook.cpp` (games) |
| Screenshots | `wii/menu/screenshot.cpp`, `runtime/shot/`, `wii/hooks/shothook.cpp`, `src/data/shotfile.cpp`, `src/art/pngencode.cpp` |
| Problem reports and the QR code | `wii/online/reportsend.cpp`, `src/data/problemreport.cpp`, `src/art/qrcode.cpp` |
| A game's crash record | `runtime/fault/`, `runtime/rtfault.c`, `wii/hooks/faulthook.cpp`, `src/data/gamefault.cpp` |
| The Wii Remote log (a Wii U Pro Controller diagnostic) | `runtime/wlog/`, `runtime/rtwlog.h`, `wii/hooks/wpadhook.cpp`, `src/patching/wpadlog.cpp` |
| RiftWii's own crash screen | `wii/core/crash.cpp` |
| Online play (Wiimmfi, WiiLink WFC) | `src/patching/wfcpatch.cpp`, `wii/launch/wfc.cpp`, `vendor-wwfc/` |
| Video mode, borders, deflicker | `src/patching/videopatch.cpp` |
| Game language | `src/patching/gamelang.cpp` |
| Fixes for particular games, automatic cIOS choice (from USB Loader GX) | `src/patching/gxpatches.cpp`, `src/patching/gxkirby.inc` (`tools/gx_kirby_table.py`), the GX lines in `wii/launch/boot.cpp`, `image_cios_order` in `wii/storage/usbcatalog.cpp` |
| A game's About page (GameTDB's details) | `wii/menu/rift_menu.cpp` (`MenuAbout`), `wii/online/gameinfo.cpp`, `src/data/gametdb.cpp` |
| Downloads: game names, covers, cheats, updates | `wii/online/online.cpp`, `src/net/http.cpp`, `wii/online/netsock.cpp`, `wii/online/tls.cpp`, `src/data/titles.cpp`, `src/art/coverart.cpp`, `src/net/update.cpp` |
| Packs served from a PC (RiiFS) | `wii/online/netpacks.cpp`, `src/net/riifs.cpp`, `src/net/riifs_sync.cpp`, `docs/RIIFS.md` |
| Returning to RiftWii (or another title) when a game exits | `src/patching/returnto.cpp`, `apply_return_to` in `wii/launch/boot.cpp`, `wii/core/restart.cpp` |
| Being started by another loader | `wii/launch/headless.cpp`, `src/patching/launchargs.cpp`, `docs/HEADLESS.md` |
| The Wii Menu channel | `channel/`, `wii/launch/channel.cpp`, `tools/make_channel.py`, `docs/CHANNEL.md` |
| Memory the menu may use | `wii/core/memlimits.cpp` |

## Top level

| File | What |
| --- | --- |
| `README.md` | The project page: what it is, installing, credits |
| `NOTICE.md` | Every third-party piece RiftWii contains or follows: origin, license, what was used |
| `LICENSE` | GNU GPL version 3 |
| `CMakeLists.txt` | The host build: the portable library, the runtime's C code and the test suites (`ctest`) |
| `Makefile.wii` | The Wii app (`riftwii.dol`); builds the runtime blobs and BearSSL first and embeds them |
| `Makefile.runtime` | The in-game blobs (resident runtime, RVZ runtime, adapter, screenshot, crash and virtual SD card), each checked position independent |
| `Makefile.bearssl` | BearSSL for the Wii (https for the update check and problem reports) |
| `Makefile.channel` | The Wii Menu channel and its installer |
| `hbc/` | The Homebrew Channel files: `meta.xml` (name, version), `icon.png`, `music.ogg` (the menu's music) |

## `wii/`: the Wii app

The menu, the launch and everything that talks to IOS. Not host-tested:
it links libogc. Each `.cpp` has a `.hpp` beside it that describes it.
The build compiles every folder into one, so no two files share a name.

| File | What |
| --- | --- |
| `main.cpp` | Start-up: video, pads, SD card, crash handler, the menu, and what happens when the player leaves |
| `lang/*.po` | The translations, written by `tools/lang_source.py` |
| `font/` | The menu font's source (`rounded.ttf`, OFL) |
| `font/` | The cut-down menu font (`tools/make_menu_font.py`), packed by the build |

### `wii/core/`: logging, settings, text files, memory limits, restarting, the crash screen, translations

| File | What |
| --- | --- |
| `i18n.cpp` | Translations (the built-in `.po` files, or one on the card) |
| `console.cpp` | libogc's text console, for the launch screen and errors |
| `crash.cpp` | RiftWii's crash screen and `crash.txt` |
| `log.cpp` | The log (`session.log`, `boot.log`) |
| `loadersettings.cpp` | Settings as the app reads and writes them |
| `memlimits.cpp` | Keeps the app's heap out of memory a launch overwrites |
| `restart.cpp` | Starting RiftWii again without the Homebrew Channel (after a game, or the channel) |

### `wii/menu/`: the menu: screens, lists, artwork, themes, music, banners, covers

| File | What |
| --- | --- |
| `rift_menu.cpp` | Every menu screen, the GUI thread, the HOME menu, the screen dimming |
| `menu.h` | The menu's entry points for `main.cpp` |
| `gui_flowlist.cpp` | The row lists (a game's Options, Mods, Cheats, Settings' sections) |
| `gui_tiles.cpp` | The tiles of the game page and the Settings hub: D-pad moves to the nearest, focus ring and lift |
| `gui_prompts.cpp` | The button prompts along the bottom of a screen (A Select, B Back, + Play) |
| `gui_gamegrid.cpp` | The Home screen's tile grid and its cover textures |
| `skin.cpp` | The menu's artwork (painted by `src/art/skinpaint.cpp`) or a theme's pictures, as textures |
| `menutheme.cpp` | The chosen theme: its theme.ini, pictures and music on the SD card |
| `menumusic.cpp` | The background music |
| `ftnopng.c` | Keeps FreeType from pulling in libpng |
| `credits.cpp` | Settings > Credits and license |
| `frontend.cpp` | The menu's state for a game: its packs, code builds and saved choices; Start's checks |
| `gameextras.cpp` | The game page's extras: cheats, video, language, online server, code builds, what a launch carries |
| `screenshot.cpp` | Menu screenshots and importing the games' ones |
| `covers.cpp` | Cover art: download, cache, textures |
| `guiscript.cpp` | Scripted menu input for tests (`sd:/riftwii/guiscript.txt`) |

### `wii/launch/`: starting a game: IOS, the disc, the apploader, patches, the runtime, the jump

| File | What |
| --- | --- |
| `progress.cpp` | The launch screen's progress line |
| `menuios.cpp` | Which IOS the menu runs under (58 or a d2x cIOS slot) |
| `ios_reload.cpp` | IOS reloads, and telling Dolphin apart from a Wii |
| `channel.cpp` | Offering and installing the Wii Menu channel |
| `codebuilds.cpp` | Finding code builds (on the card and inside `sd.raw`) and preparing their codes |
| `modplan.cpp` | Compiling the chosen packs into boot options |
| `di.cpp` | The disc drive (`/dev/di`) |
| `boot.cpp` | The launch: IOS, the disc or image, the apploader, patches, the blobs, low memory, the jump |
| `resident.cpp` | Installing the resident runtime |
| `wfc.cpp` | Patching the game for an online server |
| `headless.cpp` | Launches another loader asks for |
| `autorun.cpp` | Unattended test runs (`sd:/riftwii/autorun.txt`) |

### `wii/hooks/`: installing the blobs that run inside the game (the adapter, screenshots, crashes, the virtual SD card)

| File | What |
| --- | --- |
| `padhook.cpp` | Installing the GameCube adapter blob |
| `shothook.cpp` | Installing the screenshot blob |
| `faulthook.cpp` | Installing the crash blob (and its retail BCA answer) |
| `wpadhook.cpp` | Installing the Wii Remote log blob |
| `vsdhook.cpp` | Finding `sd.raw` for a launch and installing the virtual SD card blob |
| `vsdimage.cpp` | `sd.raw` as the menu's read-only `vsd:` drive |

### `wii/controllers/`: the GameCube adapter in the menu and the Wii Remote queues

| File | What |
| --- | --- |
| `gcadapter.cpp` | The GameCube adapter in the menu and on its test page |

### `wii/storage/`: sD cards and USB drives: game images, raw access, d2x devices, saves

| File | What |
| --- | --- |
| `usbcatalog.cpp` | Game images on the SD card and USB drives, the USB drive's volume, RVZ serving, the menu's file reads there |
| `umsdev.cpp` | d2x's USB device (`/dev/usb2`), read by the launch and handed to the runtime |
| `sdio.cpp` | Raw SD card access (`/dev/sdio/slot0`) after libogc's driver is gone |
| `d2xsd.cpp` | d2x's SD device (`/dev/sdio/sdhc`), when the game itself is on the card |
| `sdfile.cpp` | Where a file's bytes lie on the card |

### `wii/online/`: network, downloads, https, problem reports, RiiTag

| File | What |
| --- | --- |
| `netpacks.cpp` | Packs from a RiiFS server |
| `online.cpp` | Downloads (names, covers, cheats, the update check) |
| `netsock.cpp` | Network sockets |
| `tls.cpp`, `tlsroots.c` | https with BearSSL, and the certificates it trusts |
| `reportsend.cpp` | Gathering and sending problem reports; log rotation; importing a game's crash |
| `gameinfo.cpp` | Game details for the About page: GameTDB's database on the card, its download, the cache |

## `src/` and `include/riftwii/`: portable code

Plain C++17 with no Wii headers, host-tested. Each `src/<area>/x.cpp`
implements `include/riftwii/x.hpp` (one flat folder), whose comments
document it.

| Name | What |
| --- | --- |

### `src/patching/`: riivolution packs and patches, hooks, game fixes, launch arguments

| Name | What |
| --- | --- |
| `patch` | Riivolution XML: parsing, and a plan for one disc (choices, params, placeholders) |
| `apply`, `overlay`, `source` | Composing replaced files from byte sources |
| `expand` | Folder patches into file patches against the disc's file table |
| `redirect` | The table of where each replaced file's bytes come from |
| `mempatch` | Memory patches (plain, search, ocarina) and the regions they may touch |
| `launch` | `LaunchModel`: the packs and code builds for a game and their choices |
| `symsearch` | Finding SDK functions (IPC, PAD, the crash handler) in a game's code |
| `hook` | Branch encoding and placing the resident runtime |
| `codehook` | Where the Gecko code handler is called from |
| `gameconfig` | `gameconfig.txt` / `gc.txt` for code builds |
| `wfcpatch` | Online server patches |
| `videopatch` | Video settings for games |
| `gamelang` | The game language |
| `wpadlog` | The Wii Remote log: the game's empty print functions, its HOME Menu exit, and the saved log as text |
| `gxpatches` | USB Loader GX's per-game fixes and cIOS choice |
| `returnto` | Returning to RiftWii from a game |
| `launchargs` | Arguments from another loader |

### `src/disc/`: wii discs, executables, banners and fonts

| Name | What |
| --- | --- |
| `fst` | The disc's file system table |
| `disc` | Wii disc structures (header, partitions, apploader) |
| `dol` | The DOL executable header |

### `src/fs/`: file systems and game images: FAT32, NTFS, WBFS, RVZ

| Name | What |
| --- | --- |
| `fat32`, `ntfs`, `imagevolume` | Read-only FAT32 and NTFS walkers, and a drive's volume |
| `usbgame` | WBFS, split WBFS and ISO images; the d2x fragment list |
| `wbfspart` | USB drives formatted as WBFS: finding the partition, its discs, their fragments |
| `rvz`, `sha1` | RVZ images and their hashes |

### `src/net/`: hTTP, RiiFS, the update check, RiiTag

| Name | What |
| --- | --- |
| `http` | HTTP GET and POST |
| `update` | The update check |
| `riifs`, `riifs_sync` | The RiiFS client and its mirror |

### `src/art/`: pictures: the canvas, PNG, QR codes, themes, covers

| Name | What |
| --- | --- |
| `coverart`, `pngdecode`, `canvas` | Cover art, a PNG reader, the software canvas the artwork is painted on |
| `theme` | `theme.ini`: a theme's colours, corners and backdrop; the pictures a theme may replace |
| `skinpaint` | The menu's pictures painted in a theme's colours (the Wii menu and the theme kit) |
| `qrcode` | QR codes |

### `src/data/`: RiftWii's own files: settings, history, cheats, titles, reports

| Name | What |
| --- | --- |
| `cheats` | Cheat files and GCT building |
| `titles` | Game names |
| `settingsfile` | `settings.txt` |
| `langfile` | Translation files |
| `playhistory` | `history.txt` |
| `gametdb` | GameTDB's `wiitdb.xml` read a game at a time, the game details cache, wrapping their text |
| `cardlog` | The runtime's SD card log, as text |
| `shotfile`, `pngencode` | Screenshot files and a PNG writer |
| `problemreport` | A problem report's text |
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
| `wlog/`, `rtwlog.h` | The Wii Remote log blob: the game's Bluetooth and Wii Remote debug prints, saved to the NAND at HOME > Wii Menu |
| `vsd/`, `rtvsd.c` | The virtual SD card blob and the card it answers as |

Each blob directory holds `<name>_entry.S` (a header of offsets the
loader reads, the trampolines), `<name>_hook.c`, `<name>_hook.h` (the
header and context layout) and `<name>.ld`. The Wii side that installs
each one is the matching `wii/*hook.cpp`.

## `tests/`

One suite per area, `tests/<name>_tests.cpp`, registered in
`CMakeLists.txt` and run with `ctest`. `fat32_image.hpp` builds FAT32
images in memory, `fixtures/` holds XML and RVZ inputs, `qrcode_ref.inc`
is reference data from python-qrcode. `hook_tests.cpp` and
`vsdblob_tests.cpp` run the resident runtime's and the virtual SD card
blob's C code on the host against a fake IOS (the blobs keep addresses
as 32-bit words, so their memory is placed below 4 GiB; the virtual SD
card's at the Wii's own MEM1 and MEM2 addresses).

## `tools/`

| File | What |
| --- | --- |
| `lang_source.py` | The translation table: writes `wii/lang/*.po` and checks every string still appears in the code |
| `make_menu_font.py`, `make_sounds.py` | Build-time assets |
| `make_channel.py`, `make_channel_art.py`, `preview_channel.py` | The Wii Menu channel's WAD and art |
| `rtreloc.py` | Relocations for the RVZ runtime blob |
| `ipcscan.cpp`, `rvzstub.cpp` | Host tools: the IPC search on a game's DOL; an RVZ's stub disc for Dolphin |
| `theme_kit.cpp`, `make_theme_kit.py` | The theme kit zip: template pictures, a starter theme.ini, the guide |
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

Licenses and what RiftWii changed are in [NOTICE.md](../NOTICE.md).

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
