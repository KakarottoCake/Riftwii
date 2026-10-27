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
and `vendor-*` is third-party code. Related headers sit beside Wii sources;
portable headers are in `include/riftwii/`. The tables group companion
headers, generated data and assets with the code that uses them.

## I want to change...

| Topic | Start in |
| --- | --- |
| A menu screen (Home, a game's page, Mods, Cheats, Settings) | `wii/menu/rift_menu.cpp` |
| How a list or the tile grid looks or scrolls | `wii/menu/gui_flowlist.cpp`, `wii/menu/gui_gamegrid.cpp`, `wii/menu/skin.cpp` |
| The game page's and Settings' tiles, the prompts bar | `wii/menu/gui_tiles.cpp`, `wii/menu/gui_prompts.cpp`, `wii/menu/rift_menu.cpp` (`MenuHome`, `SettingsHub`), `src/art/skinpaint.cpp` (tile frames and icons) |
| Home's shelf, carousel and 3D covers ring; their boxes and 3D cover pictures | `wii/menu/gui_shelf.cpp`, `wii/menu/boxart.cpp`, `wii/menu/cover3d.cpp`, `src/art/coverart.cpp` |
| How screens, pages and popups come and go (fades, zooms, slides) | `wii/menu/transition.cpp`, the camera in `vendor-libgui/source/video.cpp` |
| A menu that freezes on a console only (the GUI watch, the GX trace, `debug_gx`) | `wii/menu/drawtrace.cpp`, `LogGuiWatch` and `UpdateGUI` in `wii/menu/rift_menu.cpp` |
| Menu themes (colours, pictures, music) | `src/art/theme.cpp` (theme.ini), `src/art/skinpaint.cpp` (the painted pictures), `wii/menu/menutheme.cpp`, `wii/menu/skin.cpp`, `themes/`, [THEMES.md](THEMES.md) |
| Downloading themes (Settings > Theme > Get more themes) | `wii/menu/themestore.cpp`, `wii/online/httpmem.cpp`, `src/art/themelist.cpp` (themes.json), `src/fs/zipread.cpp` (the zip), `tests/themebrowse_tests.cpp`, [THEMES.md](THEMES.md) |
| The theme kit download | `tools/theme_kit.cpp`, `tools/make_theme_kit.py`, `themes/kit/README.txt` |
| Menu text and its translations | `tools/lang_source.py` (the table), `wii/core/i18n.cpp`, `wii/lang/*.po` |
| The menu font: the core in the DOL, the rest from `font.ttf` on the card | `tools/make_menu_font.py`, `wii/menu/fullfont.cpp`, `SetMissingGlyphSource` in `vendor-libgui/source/FreeTypeGX.cpp`, `tests/menufont_tests.cpp` |
| Settings and what they mean | `src/data/settingsfile.cpp`, `wii/core/loadersettings.cpp`, the Settings screen in `wii/menu/rift_menu.cpp` |
| Finding games on the SD card and USB drives | `wii/menu/homescan.cpp` (Home's scan thread), `wii/storage/usbcatalog.cpp`, `src/fs/usbgame.cpp`, `src/fs/imagevolume.cpp`, `src/fs/fat32.cpp`, `src/fs/ntfs.cpp`, `src/fs/wbfspart.cpp` |
| Riivolution packs: reading the XML | `src/patching/patch.cpp` |
| Riivolution packs: what a launch does with them | `wii/launch/modplan.cpp`, `src/patching/apply.cpp`, `src/patching/expand.cpp`, `src/patching/redirect.cpp`, `src/patching/mempatch.cpp` |
| Starting a game (IOS, disc, apploader, patches) | `wii/launch/boot.cpp` |
| What runs inside a game and serves the packs | `runtime/resident/rt_hook.c`, `runtime/rtable.c`, `wii/launch/resident.cpp` |
| Saves on the SD card instead of the NAND | `runtime/rtfs.c`, `runtime/rtfat.c`, `prepare_savegame` in `wii/launch/boot.cpp`, `wii/storage/d2xsave.cpp` and `wii/storage/d2xnandemu.cpp` (d2x's NAND emulation); the shared emulated NAND: `src/data/sharednand.cpp`, `wii/storage/sharednandfind.cpp` |
| Code builds (Project+, REX) and their `gameconfig.txt` | `wii/launch/codebuilds.cpp`, `src/patching/gameconfig.cpp`, `src/patching/codehook.cpp`, `vendor-gecko/`; cheats in a second executable: `wii/launch/dolswitch_stub.S` |
| The virtual SD card (`sd.raw`) | `runtime/rtvsd.c` (the card), `runtime/vsd/` (the in-game blob), `wii/hooks/vsdhook.cpp` (install), `wii/hooks/vsdimage.cpp` (the menu's view) |
| Cheats | `src/data/cheats.cpp`, `wii/menu/gameextras.cpp`; GameHacking.org as the fallback when rc24 has none: `src/data/ghcheats.cpp`, `wii/online/ghfallback.cpp`, `wii/online/zipinflate.cpp`, `tests/ghcheats_tests.cpp` |
| A game's extra Home tiles, each with its own mods | `src/data/profiles.cpp`, `wii/menu/modtiles.cpp`, the Mods page and `BuildHome` in `wii/menu/rift_menu.cpp` |
| WiiWare and Virtual Console games (the Wii's channels, and those in an emulated NAND on the card) | `src/data/nandchannel.cpp` (which titles, their TMD, the program to load), `wii/storage/nandchannels.cpp` (listing, reading their files), `boot_channel` in `wii/launch/boot.cpp` (the start); those the Wii Menu moved to the SD card: `src/data/contentbin.cpp` (the content.bin layout), `wii/storage/movedchannels.cpp` (reading it, copying the contents to the Wii for the game), `tests/contentbin_tests.cpp` |
| Dragon Quest X on its WFS USB drive ([WFS.md](WFS.md)) | `src/fs/wfs.cpp` (the drive), `src/fs/wfstitle.cpp` (what is installed, the disc's installer partition), `wii/storage/wfsdrive.cpp` (the USB scan's look at the drive), `wii/menu/wfskey.cpp` (the Wii's WFS key), `StartWfsTitle` in `wii/main.cpp` (the start), `tests/wfs_tests.cpp`, `tests/wfstitle_tests.cpp` |
| RVZ games | `src/fs/rvz.cpp`, `runtime/rtrvz.c`, `runtime/resident/rt_zstd.c`, `runtime/gc/gc_rvz.c` (GameCube games), `docs/RVZ.md` |
| Plugins (optional code loaded from the card into MEM2 on first use) | `docs/PLUGINS.md`, `include/riftwii/plugin_api.h` (the tables), `wii/core/plugins.cpp` (the loader), `src/data/pluginfile.cpp` (the file's checks), `plugins/`, `Makefile.plugin`, `tools/make_plugin.py`; the in-app update writes them before the DOL (`plan_update_writes` in `src/net/update.cpp`, `wii/online/online.cpp`) |
| What takes MEM1, and the start-up stash (the menu font, translations and GPL copied to MEM2 at start) | `docs/MEMORY.md`, `tools/dol_stash.py`, `wii/core/stash.cpp`, `src/data/stashimage.cpp` |
| The GameCube controller adapter | `runtime/rtgcad.c` (the driver), `wii/controllers/gcadapter.cpp` (menu), `runtime/pad/` and `wii/hooks/padhook.cpp` (games) |
| USB keyboards, USB mice, pen tablets and the uDraw GameTablet in the menu | `wii/controllers/usbinput.cpp` (into player 1's controls, called from `UpdatePads` in `vendor-libgui/source/input.cpp`), `wii/controllers/usbkeyboard.cpp`, `wii/controllers/pentablet.c` and `tablethid.c` (USB tablets), `wii/controllers/udraw.c`; typing in `wii/menu/gui_searchkeys.cpp` |
| The way back from a Wii game by a GameCube controller (L, R, Z and Start held) | `runtime/rtleave.c` (when it goes: the hold, the save guard; `tests/padleave_tests.cpp`), `runtime/pad/` (the submit, OSSleepThread and OSSuspendThread hooks), `find_way_back` and `install_pad_hook` in `wii/hooks/padhook.cpp`, `find_os_return_to_menu` in `src/patching/symsearch.cpp` |
| Screenshots | `wii/menu/screenshot.cpp`, `runtime/shot/`, `wii/hooks/shothook.cpp`, `src/data/shotfile.cpp`, `src/art/pngencode.cpp` |
| Problem reports and the QR code | `wii/online/reportsend.cpp`, `src/data/problemreport.cpp`, `src/art/qrcode.cpp` |
| A game's crash record | `runtime/fault/`, `runtime/rtfault.c`, `wii/hooks/faulthook.cpp`, `src/data/gamefault.cpp` |
| The Wii Remote log, the Bluetooth trace and the Wii U Pro Controller translator | `runtime/wlog/`, `runtime/rtwlog.h`, `runtime/rtpro.c`, `wii/hooks/wpadhook.cpp`, `src/patching/wpadlog.cpp`, `docs/BLUETOOTH.md` |
| RiftWii's own crash screen | `wii/core/crash.cpp` |
| Online play (Wiimmfi, WiiLink WFC) | `src/patching/wfcpatch.cpp`, `wii/launch/wfc.cpp`, `vendor-wwfc/` |
| Video mode, borders, deflicker | `src/patching/videopatch.cpp` |
| Game language | `src/patching/gamelang.cpp` |
| Fixes for particular games, automatic cIOS choice (from USB Loader GX) | `src/patching/gxpatches.cpp`, `src/patching/gxkirby.inc` (`tools/gx_kirby_table.py`), the GX lines in `wii/launch/boot.cpp`, `image_cios_order` in `wii/storage/usbcatalog.cpp` |
| A game's About page (GameTDB's details) | `wii/menu/rift_menu.cpp` (`MenuAbout`), `wii/online/gameinfo.cpp`, `src/data/gametdb.cpp` |
| Downloads: game names, covers, cheats, updates | `wii/online/online.cpp`, `src/net/http.cpp`, `wii/online/netsock.cpp`, `wii/online/tls.cpp`, `src/data/titles.cpp`, `src/art/coverart.cpp`, `src/net/update.cpp` |
| Packs served from a PC (RiiFS) | `wii/online/netpacks.cpp`, `src/net/riifs.cpp`, `src/net/riifs_sync.cpp`, `docs/RIIFS.md` |
| A Wii game's own Reset (HOME Menu > Reset) starting the game again through RiftWii | `prepare_restart_request` and `plan_restart` in `wii/launch/boot.cpp`, `RestartArguments` in `wii/menu/gameextras.cpp`, `src/patching/launchargs.cpp` (the request), `src/patching/osrestart.cpp` (OSRestart), `rt_reset_*` in `runtime/resident/`, `HeadlessArguments` in `wii/launch/headless.cpp`, [HEADLESS.md](HEADLESS.md) |
| Returning to RiftWii (or another title) when a game exits | `src/patching/returnto.cpp`, `apply_return_to` in `wii/launch/boot.cpp`, `wii/core/restart.cpp` |
| Returning to RiftWii when a homebrew app it started exits (the return stub) | `runtime/stub/`, `include/riftwii/returnstub.h` (its layout), `src/patching/returnstub.cpp`, `plant_return_stub` in `wii/launch/channel.cpp` |
| GameCube games (RiftWii's own loader, in Wii mode) | `wii/launch/gclaunch.cpp`, `wii/launch/gcplugin.cpp` (the DOL's side: the request, the way back, the crash screen, the card's backup), the `gamecube` plugin (`plugins/gamecube/gcnative.cpp`, `gcentry.cpp`, `gchost.cpp`; `include/riftwii/plugin_gamecube.h` and `src/patching/gcrequest.cpp` for its request), `wii/launch/gcpatch.cpp`, `runtime/gc/`, `src/patching/vcdiff.cpp`, `src/patching/gcoverlay.cpp`, `src/patching/gclayout.cpp`, `src/patching/gcclock.cpp`, `src/patching/gcvideo.cpp`, `src/patching/gcreset.cpp`, `src/patching/gcaram.cpp`, `src/patching/gcexi.cpp`, `src/disc/gctgc.cpp` (TGC images on bonus discs), `src/disc/gcdiscs.cpp` (a two-disc game's second disc), `src/fs/gccard.cpp`, `src/fs/gcbackup.cpp`; their Channels banners: `src/disc/gcbanner.cpp`, `wii/menu/banners.cpp` |
| Wii Remote controls in GameCube games (the map, `sd:/riftwii/gc/controls/` files, shake) | `src/data/gccontrols.cpp`, `load_gc_controls` in `plugins/gamecube/gcnative.cpp`, `runtime/gc/gc_controls.h`, `runtime/gc/gc_pro.h` (a Wii U Pro Controller's reports), `runtime/gc/gc_bt.c` |
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
| `Makefile.plugin` | The plugins (`plugins/<name>/`), run by `Makefile.wii` with the DOL's version and build stamp; each linked at two bases and checked relocatable |
| `plugins/` | The plugins' sources (`docs/PLUGINS.md`): `plugin.ld` (their link), `common/` (a C++ plugin's runtime: `new`, `delete`, the C library's names on the host table, the throws that cannot be), `selftest/` (stage (a)'s test plugin, loaded only by the test switch), `gamecube/` (the GameCube loader, with the `runtime/gc` blobs it carries) |
| `Makefile.bearssl` | BearSSL for the Wii (https for the update check and problem reports) |
| `Makefile.channel` | The Wii Menu channel and its installer |
| `hbc/` | The Homebrew Channel files: `meta.xml` (name, version), `icon.png`, `music.ogg` (the menu's music) |
| `release/` | The next release's texts, filled in by `.github/workflows/release.yml` (`docs/DEVELOPING.md`, Releasing): `notes.md` (GitHub's release notes), `discord.md` (the testers' message), `zip/` (the zip's README.txt, SOURCE.txt and folder notes) |
| `AGENTS.md` | Build, test, style and coordination rules for agents |
| `.gitignore` | Files kept out of Git, including build output and local test data |
| `.coderabbit.yaml` | Automated review settings |
| `.github/workflows/ci.yml`, `.github/workflows/release.yml` | The build/test jobs and release packaging |
| `themes/` | Built-in themes and their artwork, `README.txt`, and the theme kit: `kit/MyTheme/` (starter theme), `kit/templates/` (editable pictures), `kit/sounds/` (sound names), `kit/guide/` (instructions) |

## `wii/`: the Wii app

The menu, the launch and everything that talks to IOS. It links libogc;
some files also have host tests with hardware calls replaced. Most `.cpp` files have a `.hpp` beside them that describes them.
The build compiles every folder into one, so no two files share a name.

| File | What |
| --- | --- |
| `main.cpp` | Start-up: video, pads, SD card, crash handler, the menu, and what happens when the player leaves |
| `lang/*.po` | The translations, written by `tools/lang_source.py` |
| `font/` | The menu font (`tools/make_menu_font.py`): `core.ttf`, packed into the DOL by the build, and the whole `rounded.ttf`, shipped as `apps/riftwii/font.ttf` (`wii/menu/fullfont.cpp` reads it) |
| `assets/` | Small pictures the build embeds in the DOL: `ciso_face.png`, the CISO popup's face (drawn by KakarottoCake); `rating_*.png`, the age rating boards' marks for the About page (NOTICE.md) |

### `wii/core/`: logging, settings, text files, memory limits, restarting, the crash screen, translations

| File | What |
| --- | --- |
| `i18n.cpp` | Translations (the built-in `.po` files, or one on the card) |
| `console.cpp` | libogc's text console, for the launch screen and errors |
| `crash.cpp` | RiftWii's crash screen and `crash.txt` |
| `log.cpp` | The log (`session.log`, `boot.log`) |
| `nanduse.cpp` | The NAND (`/dev/fs`) opened and closed for everyone, counted |
| `loadersettings.cpp` | Settings as the app reads and writes them |
| `memlimits.cpp` | Keeps the app's heap out of memory a launch overwrites |
| `restart.cpp` | Starting RiftWii again without the Homebrew Channel (after a game, or the channel) |
| `stash.cpp` | The start-up stash: crt0's copy to MEM2, and finding the font, translations and GPL in it |
| `startuptrace.cpp` | The start-up note: the step each start reached before its log opened, reported by the next start as "The last start stopped at" (`include/riftwii/startupnote.hpp`, `tests/startupnote_tests.cpp`) |
| `plugins.cpp` | The plugin loader: the file's checks, the arena at the top of MEM2, relocation, the table RiftWii gives a plugin, refusals, crash ranges, the test switch |
| `textfile.cpp` | Bounded text-file reads shared by settings and the menu |

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
| `themestore.cpp` | The theme browser's engine (Settings > Theme > Get more themes): the list, the previews, the download and safe install, all in MEM2 on the network's background thread; the page is `MenuThemeBrowser` in `rift_menu.cpp` |
| `riitagview.cpp` | Settings > Online > My RiiTag: the player's tag picture (`riitag_id`) downloaded on the network's background thread, decoded and shrunk in MEM2; the page is `MenuRiiTag` in `rift_menu.cpp` |
| `menumusic.cpp` | The background music: theme/card Ogg or the selected Wii Menu source |
| `wiimenusounds.cpp` | Reads the console's System Menu sound archive through IOS, renders bounded effects in MEM2 and streams sequence music; no disk cache |
| `themesounds.cpp` | Playback-time theme WAV replacements, then the selected Wii Menu sounds |
| `menusounds.cpp` | RiftWii's own effects, decoded once at startup |
| `ftnopng.c` | Keeps FreeType from pulling in libpng |
| `credits.cpp` | Settings > Credits and license |
| `wfskey.cpp` | The Wii's WFS key (`ConsoleWfsKey`, `otpkey.hpp`) for Dragon Quest X's drive, from the dump on the card (`sd:/riftwii/wfskey.bin`, raw or hex; `parse_wfs_key`) |
| `frontend.cpp` | The menu's state for a game: its packs, code builds and saved choices; Start's checks |
| `packcatalog.cpp` | The pack text and code mod list shared by Home's scan and each game's page |
| `modtiles.cpp` | A game's extra Home tiles with mods of their own (profiles): their list and choices on the card |
| `gameextras.cpp` | The game page's extras: cheats, video, language, online server, code builds, what a launch carries |
| `screenshot.cpp` | Menu screenshots and importing the games' ones |
| `covers.cpp` | Cover art: download, cache, textures |
| `homescan.cpp` | Home's look for games on a thread of its own: the SD card's games first, then the USB drive's |
| `guiscript.cpp` | Scripted menu input for tests (`sd:/riftwii/guiscript.txt`) |
| `drawtrace.cpp` | Where the GUI thread is drawing, for a freeze on a console: the GUI watch and the GX trace (`debug_gx`) |
| `fullfont.cpp`, `wiifont.cpp` | The full menu font from the card; the Wii Menu's font read from the console and cached on the card |
| `ratingbadge.cpp` | The About page's embedded age-rating pictures, selected by `src/art/ratingmark.cpp` |
| `boxart.cpp`, `cover3d.cpp` | Full cover scans for the shelf and carousel, and GameTDB's 3D case pictures |
| `gui_shelf.cpp`, `gui_searchkeys.cpp` | The shelf/carousel/3D ring and the search/name keyboard |
| `transition.cpp` | Screen changes, fades, slides and zooms |
| `appicon.cpp` | Homebrew app icons, with bounded PNG reads and cached textures |
| `banners.cpp`, `bannerplay.cpp`, `bannersound.cpp` | Reading/caching disc and channel banners, drawing their layouts and animation, playing their sound |
| `otpkey.cpp` | Reading the current console's keys for disc banners and moved SD channels; keys stay in memory |
| `modpicture.cpp` | Mod and saved-tile pictures loaded and fitted for the menu |
| `memorybudget.cpp` | The game page's estimate of memory used by RiftWii's extras |
| `vsdmake.cpp` | Planning and copying a code build into a virtual SD image, with progress and cancellation |
| `ftnobrotli.c` | Keeps FreeType from linking Brotli support |

### `wii/launch/`: starting a game: IOS, the disc, the apploader, patches, the runtime, the jump

| File | What |
| --- | --- |
| `progress.cpp` | The launch screen's progress line |
| `menuios.cpp` | Which IOS the menu runs under (58 or a d2x cIOS slot) |
| `ios_reload.cpp` | IOS reloads, and telling Dolphin apart from a Wii |
| `nandpermissions.cpp` | Opening and closing the running IOS's NAND permission check, with a note saved first so the next start closes a patch an interrupted run left open (`include/riftwii/nandcheck.hpp`, `tests/nandpermissions_tests.cpp`) |
| `channel.cpp` | Offering and installing the Wii Menu channel |
| `codebuilds.cpp` | Finding code builds (on the card and inside `sd.raw`) and preparing their codes |
| `modplan.cpp` | Compiling the chosen packs into boot options |
| `di.cpp` | The disc drive (`/dev/di`) |
| `boot.cpp` | The launch: IOS, the disc or image, the apploader, patches, the blobs, low memory, the jump |
| `resident.cpp` | Installing the resident runtime |
| `wfc.cpp` | Patching the game for an online server |
| `headless.cpp` | Launches another loader asks for |
| `launchrun.cpp`, `launchsession.hpp` | How a game is started: probe the disc or image, compile the packs, boot (`RunLaunch`, `BootCompiled`, `CompileSelection`...), shared by the menu, the headless entry and autorun |
| `autorun.cpp` | Unattended test runs (`sd:/riftwii/autorun.txt`); test builds only (`make -f Makefile.wii TEST=1`) |
| `gclaunch.cpp` | GameCube games: finding them on the card, planning their packs, starting them |
| `gcplugin.cpp` | GameCube games in Wii mode, the DOL's side: the launch handed to the `gamecube` plugin (`plugins/gamecube/gcnative.cpp` patches the game to read the SD card, its sound, controls and memory card through the `runtime/gc` blob), the way back, the crash screen, the memory card's backup |
| `gcpatch.cpp` | Patch-only GameCube mods (xdelta): listed as the game they make, prepared once |
| `dolswitch_stub.S` | Hooks the next executable a Wii game loads so cheats continue |
| `startfile_stub.S` | Copies and starts a homebrew DOL after RiftWii lets go of memory |

### `wii/hooks/`: installing the blobs that run inside the game (the adapter, screenshots, crashes, the virtual SD card)

| File | What |
| --- | --- |
| `padhook.cpp` | Installing the GameCube adapter blob |
| `hidsession.cpp` | Owning the adapter's USB HID handle, closing or forgetting it across IOS reloads |
| `shothook.cpp` | Installing the screenshot blob |
| `faulthook.cpp` | Installing the crash blob (and its retail BCA answer) |
| `wpadhook.cpp` | Installing the Wii Remote log blob |
| `vsdhook.cpp` | Finding `sd.raw` for a launch and installing the virtual SD card blob |
| `vsdimage.cpp` | `sd.raw` as the menu's read-only `vsd:` drive |

### `wii/controllers/`: the GameCube adapter, USB keyboards, mice and pen tablets in the menu, and the Wii Remote queues

| File | What |
| --- | --- |
| `gcadapter.cpp` | The GameCube adapter in the menu and on its test page |
| `usbinput.cpp` | A USB keyboard, mouse or pen tablet and the uDraw as player 1's controls in the menu; started after the first drive scan, stopped before an IOS reload and at the menu's end |
| `usbkeyboard.cpp` | The USB keyboard (libwiikeyboard on a thread of its own): keys held and pressed, characters typed |
| `pentablet.c`, `tablethid.c` | USB pen tablets (Wacom, Huion, XP-Pen and others) from their HID report descriptors; the link wrap that hides tablets and the GameCube adapter from libogc's own HID scans |
| `udraw.c` | The uDraw GameTablet on a Wii Remote, through two wiiuse link wraps |
| `usbprobe.cpp` | Check each cIOS: the adapter's USB checks across IOS 58 and installed d2x slots, and the saved results |
| `wpadqueue.cpp` | Wii Remote command queues in MEM2, avoiding libogc's small kernel heap |

### `wii/storage/`: SD cards and USB drives: game images, raw access, d2x devices, saves

| File | What |
| --- | --- |
| `homebrew.cpp`, `homebrew.hpp` | SD Homebrew app discovery, metadata and launch selection |
| `usbcatalog.cpp` | Game images on the SD card and USB drives, the USB drive's volume, RVZ serving, the menu's file reads there |
| `umsdev.cpp` | d2x's USB device (`/dev/usb2`), read by the launch and handed to the runtime |
| `sdio.cpp` | Raw SD card access (`/dev/sdio/slot0`) after libogc's driver is gone |
| `d2xsd.cpp` | d2x's SD device (`/dev/sdio/sdhc`), when the game itself is on the card |
| `gcrealcard.cpp`, `gcrealcard.hpp` | Read-only physical GameCube card sessions: early MEM1 DMA reservation, CARD mount, synchronous raw EXI reads, owned EXI cleanup, sticky removal detection and snapshot verification |
| `sdfile.cpp` | Where a file's bytes lie on the card |
| `d2xsave.cpp` | Saves of SD and USB games kept by d2x's NAND emulation in a folder of the card |
| `d2xnandemu.cpp` | Turning d2x's NAND emulation on: RiftWii lets go of the SD card (libfat, boot.log, libogc's driver) while d2x's FAT module mounts it, stays off it while the emulation is on, and takes it back when the emulation does not come on |
| `sharednandfind.cpp` | The Saves choice "SD, emulated NAND": the folder read from the other loader's settings files; the folder its channels are in |
| `wadinstall.cpp` | Settings > Install WAD files: `.wad` files from `sd:/wad` into the emulated NAND the channels are listed from |
| `nandchannels.cpp` | WiiWare and Virtual Console games on the Wii and in an emulated NAND on the card: listing them, their banners, the files a start needs |
| `movedchannels.cpp` | Channels the Wii Menu moved to the SD card (`sd:/private/wii/title/<code>/content.bin`): their contents read and decrypted with this Wii's key (or a BootMii `keys.bin`'s), copied to the title's NAND folder for the game and removed at the next start |
| `wfsdrive.cpp`, `wfsdrive.hpp` | Dragon Quest X's WFS drive as the USB scan finds it: opened with the Wii's WFS key, what is installed on it kept for Home and the start |

### `wii/online/`: network, downloads, https, problem reports, RiiTag

| File | What |
| --- | --- |
| `netpacks.cpp` | Packs from a RiiFS server |
| `online.cpp` | Downloads (names, covers, cheats, the update check) |
| `netsock.cpp` | Network sockets |
| `tls.cpp`, `tlsroots.c` | https with BearSSL, and the certificates it trusts |
| `httpmem.cpp` | One https GET read straight into a caller's block of memory (the theme browser's, MEM2) |
| `ghfallback.cpp` | Cheats from GameHacking.org when rc24 has none: the newest Wii zip fetched at most once in 30 days, kept in `sd:/riftwii/cheats/gamehacking/`, read from the card after that; its memory is MEM2 |
| `zipinflate.cpp` | zlib's raw inflate for the zip reader, working in MEM2 blocks the caller lends (theme browser, GameHacking.org cheats) |
| `entropy.cpp` | the random seed for https: the card's seed file, the console's ID, the Remotes' jitter |
| `reportsend.cpp` | Gathering and sending problem reports; log rotation; importing a game's crash |
| `gameinfo.cpp` | Game details for the About page: GameTDB's database on the card, its download, the cache |
| `riitagsend.cpp` | Sending the game being launched to the player's RiiTag servers |

## `src/` and `include/riftwii/`: portable code

Plain C++17 with no Wii headers, host-tested. Most modules pair
`src/<area>/<name>.cpp` with `include/riftwii/<name>.hpp` (one flat
header folder), whose comments document it. Generated data and private
reader helpers stay beside their source.

### `src/patching/`: riivolution packs and patches, hooks, game fixes, launch arguments

| Name | What |
| --- | --- |
| `patch` | Riivolution XML: parsing, and a plan for one disc (choices, params, placeholders) |
| `patchstream` | `read_package` from a `std::istream`, for the host tests only (left out of the Wii build so no stream code is linked) |
| `apply`, `overlay`, `source` | Composing replaced files from byte sources |
| `expand` | Folder patches into file patches against the disc's file table |
| `redirect` | The table of where each replaced file's bytes come from |
| `mempatch` | Memory patches (plain, search, ocarina) and the regions they may touch |
| `launch` | `LaunchModel`: the packs and code builds for a game and their choices |
| `symsearch` | Finding SDK functions (IPC and its submit routine, PAD, the crash handler, OSReturnToMenu) in a game's code |
| `hook` | Branch encoding and placing the resident runtime |
| `codehook` | Where the Gecko code handler is called from |
| `gameconfig` | `gameconfig.txt` / `gc.txt` for code builds |
| `wfcpatch` | Online server patches |
| `videopatch` | Video settings for games |
| `gamelang` | The game language |
| `wpadlog` | The Wii Remote log: the game's empty print functions, its HOME Menu exit, and the saved log as text |
| `gxpatches` | USB Loader GX's per-game fixes and cIOS choice |
| `osrestart` | A Wii game's own Reset: finding OSRestart and OSSleepThread, the `debug_reset` test switch's stub; finding OSSuspendThread (the way back in N64 Virtual Console games) |
| `returnto` | Returning to RiftWii from a game |
| `returnstub` | The return stub for homebrew apps: its image filled with the titles to try, RiftWii's stub told from the Homebrew Channel's, and apps that would load over it |
| `launchargs` | Arguments from another loader |
| `vcdiff` | Applying VCDIFF (xdelta) patches |
| `gcoverlay` | GameCube packs: their files laid out past the image, the new FST, and the runs the GameCube loader serves the disc from |
| `gcrequest` | The `gamecube` plugin's request: the packs' plan flattened for it and built again, and the plugin's check of what it was given |
| `gclayout` | The GameCube loader's MEM1 layout: the game's stack top and ArenaLo found in its code, RiftWii's pieces packed under the FST around the DI registers' stand-in (built into the `gamecube` plugin only, not the DOL: `Makefile.wii`'s `EXCLUDES`) |
| `gcclock` | The GameCube loader's clocks: the speed read from HW_CLOCKS, and GameCube clock values built into a game's code moved to the Wii's (built into the `gamecube` plugin only, not the DOL: `Makefile.wii`'s `EXCLUDES`) |
| `gcvideo` | GameCube games' picture settings: the Sharper picture's switch in GXSetCopyFilter, and the mode VIConfigure gets for 480p and the Picture width (`runtime/gc/gc_video.h`) |
| `gcload` | Luigi's Mansion's main-heap globals found structurally, and its callback heap policy (`runtime/gc/gc_load.h`): keep archive lifetimes while avoiding temporary model heaps |
| `gcreset` | GameCube games' Reset: OSResetSystem and the SDK's reset routine as the loader finds them, and the restart's copy of MEM1 laid out in MEM2 (built into the `gamecube` plugin only, not the DOL: `Makefile.wii`'s `EXCLUDES`) |
| `gcaram` | A sound engine's ARAM pool clamped to ARGetSize() in MusyX's shape, found so the GameCube loader can raise the limit with the game's ARAM addresses (built into the `gamecube` plugin only, not the DOL: `Makefile.wii`'s `EXCLUDES`) |
| `gcexi` | GameCube SDK EXI functions found through their control blocks and call relationships (`src/patching/gcexi.cpp`): both block sizes, an extra select variant, and missing or ambiguous functions rejected |
| `ctgpconfig` | CTGP channel configuration defaults, preserving values the player set |
| `extrasbudget` | Counting the memory a launch's extra code and state take from the game |
| `gcdsp` | Recognizing and patching GameCube sound programs for samples in MEM2 |
| `gcfont` | Finding Japanese games' font-encoding checks |
| `riiconfig` | Reading/writing Riivolution's saved option choices |
| `vsdbuild`, `vsdparts` | Planning/writing FAT32 virtual SD images and reading their numbered split parts |

### `src/disc/`: Wii discs, executables, banners, fonts and sounds

| Name | What |
| --- | --- |
| `wiisound`, `wiisound_read.hpp` | Bounded U8, BRSAR, RWSD/RWAR/RWAV and sample-bank readers; PCM8/PCM16 and the shared DSP-ADPCM decoder. Public API: `include/riftwii/wiisound.hpp`; synthetic tests: `tests/wiisound_tests.cpp` |
| `wiisequence` | Fixed-slot RSEQ/RBNK sample renderer; all bank allocations happen before playback |
| `fst` | The disc's file system table |
| `disc` | Wii disc structures (header, partitions, apploader) |
| `dol` | The DOL executable header |
| `gcbanner` | A GameCube disc's BNR1/BNR2 banner (picture and names), the frame the Channels view draws it in, and the checks on a downloaded animated banner |
| `bnr`, `brlyt`, `brlan`, `tpl` | Wii banner archives, layouts, animations and textures |
| `bnrsound` | Banner audio decoding, with limits on archive and sample sizes |
| `sysfont`, `brfnt` | Finding/extracting the Wii Menu's font content and reading its bitmap glyphs |
| `wiicrypt` | AES and Wii disc-content decryption, using keys supplied by the caller |
| `gcdiscs` | Validating a two-disc GameCube game's partner image and the file-table room both need |
| `gctgc` | TGC images inside GameCube bonus discs: bounded headers, executable sections and file tables |

### `src/fs/`: file systems and game images: FAT32, NTFS, WBFS, RVZ

| Name | What |
| --- | --- |
| `fat32`, `ntfs`, `imagevolume` | Read-only FAT32 and NTFS walkers, and a drive's volume |
| `cardvolume` | An SD card's partition and file system, described for boot.log before d2x's FAT module mounts the card |
| `fragments` | `place_on_fragments`: a range of a file mapped onto its sectors (on its own so the `gamecube` plugin links it without the redirect table's builder) |
| `usbgame` | WBFS, split WBFS and ISO images; the d2x fragment list |
| `wbfspart` | USB drives formatted as WBFS: finding the partition, its discs, their fragments |
| `rvz` | RVZ images and their hashes; `rvz_error.hpp` supplies error names when the decoder strips its strings, checked through the plugin shim by `tests/rwpzstd_tests.cpp` |
| `gccard` | GameCube memory card images: formatting one, listing its saves and their icons, deleting a save, .gci export and import |
| `gcreal` | Physical-card import preview: exact game/maker selection, GCI validation, collision/capacity/encoding checks, staged SD commits; `include/riftwii/gcreal.hpp`, `src/fs/gcreal.cpp`, `tests/gcreal_tests.cpp` |
| `gcbackup` | GameCube memory card backups: the copy made before a start, named by date and time, the newest five kept, none while the card is unchanged |
| `wfs`, `wfstitle` | Dragon Quest X's WFS drive (read-only, decrypted with the Wii's WFS key): its directories and files; the titles installed on it, the disc's installer partition |
| `gccard_text`, `sjis_table.inc` | A card's save names and comments as UTF-8 (Shift-JIS through the table `tools/make_sjis_table.py` makes, or Windows-1252) |
| `gcimport` | Importing GameCube saves from Dolphin folders and Nintendont card images |
| `sdhotplug` | Debounced SD insertion/removal decisions; mounting stays on the Wii side |
| `zipread`, `zipstream` | Bounded ZIP readers: entries from memory, or a streamed database with size/CRC checks |

### `src/net/`: HTTP, RiiFS, the update check, RiiTag

| Name | What |
| --- | --- |
| `http` | HTTP GET and POST |
| `update` | The update check |
| `riifs`, `riifs_sync` | The RiiFS client and its mirror |
| `riitag` | RiiTag: Wiinnertag.xml, the launch URL, the tag picture's URL for a Discord ID and its fit to the menu |

### `src/art/`: pictures: the canvas, PNG, QR codes, themes, covers

| Name | What |
| --- | --- |
| `coverart`, `pngdecode`, `canvas` | Cover art, a PNG reader, the software canvas the artwork is painted on |
| `theme` | `theme.ini`: a theme's colours, corners and backdrop; the pictures a theme may replace |
| `skinpaint` | The menu's pictures painted in a theme's colours (the Wii menu and the theme kit) |
| `qrcode` | QR codes |
| `pngencode` | Writing screenshot PNGs |
| `casecolour`, `casecolours.inc` | Game cases' colors and the generated game-ID table |
| `inputicons`, `inputicons_data.cpp` | Controller button pictures and their generated image data |
| `modart` | Candidate picture paths for XML packs, code builds and virtual SD images |
| `ratingmark` | Choosing the age-rating board's picture from a game's rating |
| `themelist`, `themepack` | Community theme listings and the built-in themes/apps update-pack format |

### `src/data/`: RiftWii's own files: settings, history, cheats, titles, reports

| Name | What |
| --- | --- |
| `appmeta` | Bounded Homebrew meta.xml fields and NUL-separated launch arguments (`tests/appmeta_tests.cpp`) |
| `cheats` | Cheat files and GCT building |
| `ghcheats` | GameHacking.org's archive: picking the newest Wii zip from its listing, the bounds on the zip, the game ID index and its file |
| `titles` | Game names |
| `settingsfile` | `settings.txt` |
| `langfile` | Translation files |
| `playhistory` | `history.txt` |
| `profiles` | `profiles.txt`: a game's extra Home tiles with their own mods; their names, keys and limits |
| `namekeys` | The Shift of the keyboard a tile's name is typed on: what each key types with Shift off, on by itself (capitals) or pressed (capitals and symbols), and how Shift follows the name |
| `homeorder` | `order.txt`: Home's own order (Settings > Home order: Your order) |
| `gametdb` | GameTDB's `wiitdb.xml` read a game at a time, the game details cache, wrapping their text |
| `cardlog` | The runtime's SD card log, as text |
| `shotfile` | Screenshot file headers and conversion |
| `problemreport` | A problem report's text |
| `gamefault` | A game's crash record, as text |
| `gccrash` | A GameCube game's crash, from its loader's trace, as text |
| `gccontrols` | A GameCube game's Wii Remote controls: the built-in map and `sd:/riftwii/gc/controls/` files that change it (built into the `gamecube` plugin only, not the DOL: `Makefile.wii`'s `EXCLUDES`) |
| `sharednand` | The shared emulated NAND's folder: the other loader's settings lines and the checks on the path |
| `wadfile` | `.wad` files: their layout, each content decrypted and checked against its TMD hash, `/shared1/content.map` |
| `contentbin` | A channel the Wii Menu moved to the SD card (`content.bin`): its "Bk" header found past the SD-key parts, which contents it holds and where, a BootMii `keys.bin` |
| `nandchannel` | WiiWare and Virtual Console titles: which titles are games, a stored TMD's contents, the content RiftWii loads, their cheats' code |
| `textlines` | Lines and words split from a text file by the rules of `std::getline` and `>>`, without the streams (which bring close to 500 KiB of libstdc++'s locale code into the DOL) |
| `pluginfile` | A plugin file (`.rwp`): its header and every refusal, its relocations and file names |
| `channelupdate` | Deciding whether the installed RiftWii channel needs an update and tracking installer attempts |
| `d2xversion` | Reading a cIOS's d2x identity, accepted versions and update reminders |
| `nandcheck`, `nandlist` | The saved NAND-permission repair note and bounded NAND folder listings |
| `offlinegames`, `offlinegames.inc` | GameTDB-derived game IDs with no online play |
| `sha1`, `sha256` | Hashes used for game data, plugins and downloads |
| `stashimage` | Validating/finding entries in the build's start-up stash |

Header-only helpers in `include/riftwii/`: `checkedalloc.hpp` checks
allocations before a container grows; `disccache.hpp` tracks disc identity
between drive probes; `homeinput.hpp` handles Home's controller paging;
`startupnote.hpp` records start-up progress. `plugin_api.h` and
`plugin_gamecube.h` define the loader/plugin boundary and GameCube request.

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
| `gc/` | The GameCube blob: `gc_rt.c` (disc reads, sound, the way back, the crash screen), `gc_bt.c` (Wii Remotes) with `gc_ports.h` (their ports), `gc_gcad.c` (the GameCube controller adapter for Wii U, driving `runtime/rtgcad.c`; its port filling and rumble are host-tested in `tests/gcgcad_tests.cpp`) and `gc_controls.h` (what their buttons and a shake press) and `gc_pro.h` (a Wii U Pro Controller's reports), all three also built into the host tests, `gc_video.h` (the mode VIConfigure gets, also built into the host tests), `gc_disc.h` (a two-disc game's disc change, also built into the host tests), `gc_card.c` (memory card), `gc_entry.S`, `gc.ld`; its RVZ build adds `gc_rvz.c` (the disc from an RVZ, also built into the host tests) and links with `gc_rvz.ld` |
| `pad/`, `rtgcad.c`, `rtleave.c` | The GameCube adapter blob and its WUP-028 driver (also used by the menu); with the way back, its hooks on the SDK's IPC submit routine and OSSleepThread, and `rtleave.c`'s decisions (host-tested in `tests/padleave_tests.cpp`) |
| `shot/`, `rtshot.c` | The in-game screenshot blob |
| `fault/`, `rtfault.c` | The crash blob: the crash record to the NAND; also answers the BCA read |
| `wlog/`, `rtwlog.h` | The Wii Remote log blob: the game's Bluetooth and Wii Remote debug prints and a trace of its Bluetooth traffic, saved to the NAND at HOME > Wii Menu; it runs the Pro Controller translator |
| `rtpro.c` | The Wii U Pro Controller translator: the Pro presented to the game as a Wii Remote with a Classic Controller (host-tested in `tests/pro_tests.cpp`) |
| `vsd/`, `rtvsd.c` | The virtual SD card blob and the card it answers as |
| `stub/` | Not a blob: the return stub for homebrew apps (`include/riftwii/returnstub.h` has the layout), a program of its own at `0x80001800`, where libogc's `exit()` looks; it launches the RiftWii channel, the Homebrew Channel or the Wii Menu through `/dev/es` |
| `dolpack/` | Not a blob: the packed `boot.dol`'s stub (`dolpack.h` has the format), a program of its own at `0x80900000` that unpacks RiftWii's sections with the vendored Zstandard decoder and starts it (`tools/pack_dol.py` makes the DOL) |

Each blob directory holds `<name>_entry.S` (a header of offsets the
loader reads, the trampolines), `<name>_hook.c`, `<name>_hook.h` (the
header and context layout) and `<name>.ld`. The Wii side that installs
each one is the matching `wii/*hook.cpp`.

## `tests/`

One suite per area, `tests/<name>_tests.cpp`, registered in
`CMakeLists.txt` and run with `ctest`. `fat32_image.hpp` builds FAT32
images in memory, `zipbuild.hpp` builds small zip files, `fixtures/` holds XML and RVZ inputs (and
`plugin_selftest.rwp`, a plugin file `tools/make_plugin.py` wrote, which
`plugin_tests.cpp` reads back), `qrcode_ref.inc`
is reference data from python-qrcode. `hook_tests.cpp` and
`vsdblob_tests.cpp` run the resident runtime's and the virtual SD card
blob's C code on the host against a fake IOS (the blobs keep addresses
as 32-bit words, so their memory is placed below 4 GiB; the virtual SD
card's at the Wii's own MEM1 and MEM2 addresses).
`gcrealcard_io_tests.cpp` compiles the Wii card reader against `gcreal_mock/`
to check EXI ownership, failed mounts/reads, deferred close and removal.
`gcreal_tests.cpp` checks the real numeric DMA address limits separately.

`gcrvz_tests.cpp`, `gcdisc_tests.cpp`, `gcports_tests.cpp` and
`gccontrols_tests.cpp` exercise the GameCube RVZ reader, disc changes,
controller port allocation and control mappings. Their implementations
live in `runtime/gc/`, `src/disc/gcdiscs.cpp` and `src/data/gccontrols.cpp`.
Most other suites follow the same `<area>_tests.cpp` naming. Channel
entry/loader tests also use `.c`, and `channel_package_tests.py` checks the
packaged installer. `vcdiff_data.inc` is patch-test data. Test-only headers
and the `*_mock/` folders provide synthetic files, discs and hardware calls.

## `tools/`

| File | What |
| --- | --- |
| `lang_source.py` | The translation table: writes `wii/lang/*.po` and checks every string still appears in the code |
| `make_menu_font.py`, `make_sounds.py` | Build-time assets |
| `menu_font_chars.py` | Which characters the menu font's core holds; checks `wii/font/core.ttf` (also run by `lang_source.py`) |
| `make_sjis_table.py` | The Shift-JIS table for Japanese memory cards' text (`src/fs/sjis_table.inc`) |
| `make_channel.py`, `make_channel_art.py`, `preview_channel.py` | The Wii Menu channel's WAD and art |
| `rtreloc.py` | Relocations for the RVZ runtime blob |
| `make_plugin.py` | Plugin files: built from the two links (header, image, relocations, SHA-256), checked against the DOL's build stamp, varied for refusal tests |
| `dol_stash.py` | Adds the start-up stash (menu font, translations, GPL) to `riftwii.dol` above its BSS (`docs/MEMORY.md`) |
| `pack_dol.py` | Packs `riftwii.dol` last: each section a Zstandard frame behind the `runtime/dolpack` stub, every frame checked before it is written (`PACK=0` skips it) |
| `make_release_zip.py`, `make_update_pack.py`, `release_notes.py` | A release: the zip, the in-app update's pack, the notes and the Discord message (run by `.github/workflows/release.yml`) |
| `size_report.py` | CI's size table: the DOL's sections, its program end and the stash's end, the runtime blobs, the plugins' share of their arena |
| `menusound_list.cpp` | Lists a NAND content copy's U8 files and BRSAR names/types, optionally validates a named sound in memory; never extracts audio |
| `ipcscan.cpp`, `rvzstub.cpp` | Host tools: the IPC search on a game's DOL; an RVZ's stub disc for Dolphin |
| `theme_kit.cpp`, `make_theme_kit.py` | The theme kit zip: template pictures, a starter theme.ini, the guide |
| `rvz/make_test_disc.py` | The RVZ test fixtures |
| `dolphin/` | Dolphin test harness: `run.sh`, the USB Gecko log reader, a RiiFS test server, movie and screenshot helpers (see `docs/HARNESS.md`) |
| `attribution_audit.py`, `ci_rules.py` | Code-attribution comparison and the project rules checked by CI |
| `bannerdump.cpp` | Inspecting banner layouts, animation and textures |
| `menusound_render.cpp` | Rendering a named Wii sound/sequence for inspection |
| `make_bookshelf_theme.py`, `make_indigo_theme.py` | Generating the built-in themes' artwork |
| `make_case_colours.py`, `make_offline_games.py` | GameTDB-derived case-color and offline-game tables |
| `make_input_icons.py`, `make_korean_font.py`, `korean_title_chars.txt` | Generated button artwork, the Korean font subset and its title characters |
| `gx_kirby_table.py` | Generating the Kirby game-fix table |
| `gc_exi_scan.cpp`, `gc_trace.py` | Inspecting GameCube EXI routines and decoding a game's trace records |
| `gc_card_load_test.py`, `gc_dol_load_test.py`, `gc_fst_read_test.py` | Isolated checks of the actual GameCube loader's card, executable and file-table reads |
| `gc_io_emutest.py`, `wlog_boundary_emutest.py` | Executing built PowerPC code against simulated I/O and memory boundaries |
| `timesong/` | Artwork cutout and original audio generation scripts |

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
| `vendor-kenney/input-prompts/` | Kenney's controller and button pictures (CC0); `tools/make_input_icons.py` turns them into `src/art/inputicons_data.cpp` |
| `vendor-zstd/` | The Zstandard decoder (RVZ) |
| `vendor-gecko/` | The Gecko code handler, with its source |
| `vendor-wwfc/` | WiiLink WFC's patcher pieces |

Licenses and what RiftWii changed are in [NOTICE.md](../NOTICE.md).

## Not in the repository

Build output, all git-ignored: `build-host/` (CMake), `build_wii/` and
`riftwii.dol` / `.elf` (`Makefile.wii`), `build-runtime/` (the blobs),
`build-bearssl/`, `build-channel/`, `build-plugins/` (the plugins; `build-plugins/plugins/` is what goes to `sd:/apps/riftwii/plugins/`, and `build-plugins/<name>/<name>.elf` is the ELF to keep for a crash's offsets). `build-dolphin/` is the local Dolphin
test setup (`docs/HARNESS.md`).

## On the SD card

What RiftWii keeps in `sd:/riftwii/` is listed in
[ARCHITECTURE.md](ARCHITECTURE.md#files-on-the-sd-card).

## Conventions

- Sources compiled into the Wii app need distinct base names
  (`Makefile.wii` builds them into one flat folder).
- Logic that can run on a PC goes in `src/` with tests; `wii/` is glue
  to IOS and the screen.
- A new in-game blob follows the existing ones: its own `runtime/<name>/`,
  rules in `Makefile.runtime`, a `wii/<name>hook.cpp`, an entry in
  `ARCHITECTURE.md`'s memory map.
- New third-party code, or code written from someone else's, gets a row
  in `NOTICE.md`.
