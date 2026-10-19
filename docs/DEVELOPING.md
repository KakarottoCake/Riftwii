# Developing RiftWii

Start with `ARCHITECTURE.md` for the map of the code. This page covers
building, testing and releasing.

## Building

Everything builds on Windows inside devkitPro's MSYS2 shell (plain Git
Bash cannot find the C++ headers). From the project root:

```
# the host library and its tests
cmake -S . -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure

# the Wii app: riftwii.dol and riftwii.elf
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
make -f Makefile.wii
```

The Wii build needs devkitPPC, libogc, libfat and the portlibs `Makefile.wii`
lists (FreeType, zlib, libogg/tremor), and the `zstd` command-line
tool (devkitPro's MSYS2 has it), which packs the runtime blobs, the menu
font, the translations and the GPL text. Python 3 then adds the font,
the translations and the GPL to `riftwii.dol` as the start-up stash
(`tools/dol_stash.py`, [MEMORY.md](MEMORY.md)); `riftwii.elf` has no
stash and stops at the menu font if started directly. It first builds the
two in-game blobs with `Makefile.runtime` (`build-runtime/riftwii_rt.bin`,
the resident runtime, and `riftwii_pad.bin`, the pad hook), checking that
each is position-independent, then embeds them with the font, the
translations and the Gecko code handler.

The plugins ([PLUGINS.md](PLUGINS.md)) are built in the same `make`,
before the DOL, by `Makefile.plugin` (`make -f Makefile.wii plugins`
builds only them). Each is linked at two bases and
`tools/make_plugin.py` writes the `.rwp` with the DOL's version and build
stamp (the version and the commit, `2610-469-ga5ef290f`):
`build-plugins/plugins/<name>.<version>.rwp` goes to
`sd:/apps/riftwii/plugins/` next to `boot.dol`, and
`build-plugins/<name>/<name>.elf` (linked at 0) is the ELF a crash
report's `plugin <name> +0x...` offset is looked up in. The DOL's rule
ends with `make_plugin.py check`, so a DOL is never built next to
plugins it would refuse. Copy the DOL and its plugins together: a DOL
refuses plugins from any other build.

Things that bite:

- `Makefile.wii` compiles every source into one flat folder, so two
  sources may not share a base name (`redirect.cpp` and `rtable.c`, not
  two `redirect`s).
- Versions follow Dolphin's numbering: a release is its year and month
  (`2610`), and every build after it is that plus the commits since the
  release's tag (`2610-123`). `CMakeLists.txt` holds the YYMM
  (`project(Riftwii VERSION ...)`) and the tag (`RIFTWII_BASE_TAG`);
  `Makefile.wii` counts the commits with git at every build and rebuilds
  only the sources that print the version. A release is tagged with
  `v` and the DOL's version (`v2610-123`), built from that commit.
- The pugixml `-Wmaybe-uninitialized` warning is expected.

## Tests

`ctest` runs 49 suites, one per area (`tests/<area>_tests.cpp`): the XML
parser and planner, the patch engine, FST and disc parsing, FAT32 and
NTFS walkers, the WBFS mapper and WBFS drives, the resident runtime's C code (compiled
for the host with fake IOS calls), the FAT32 and NTFS engines, the
GameCube adapter driver and the PAD search, the titles, cheats and
settings files. The in-game code is built for both the host (tests) and
the Wii from the same sources.

Translations: `python tools/lang_source.py` checks that every table entry
is still in the menu's sources and writes `wii/lang/*.po`. New Japanese
text must be in the menu font; `tools/make_menu_font.py` explains how the
subset is cut.

## Running in Dolphin

`docs/HARNESS.md` describes the isolated Dolphin setup
(`build-dolphin/user`). In short:

```
DOLPHIN_DIR=/c/path/to/Dolphin-x64 tools/dolphin/run.sh 240
```

runs `riftwii.dol` (or `DOL=...`) for 240 seconds and prints the USB Gecko
and Dolphin logs. Dolphin rebuilds its SD image from
`build-dolphin/user/Load/WiiSDSync/` at every boot, which takes more than
a minute on a large card, so short runs never reach the menu.

Two scripts on the card drive a run without hands on a controller:

- `sd:/riftwii/autorun.txt` skips the menu and runs commands, one per
  line, logging to `sd:/riftwii/autorun.log`: `disc`, `sd <path-or-id>
  [cios]`, `usb <path-or-id> [cios]`, `probe`, `layout`, `meta [dir]`,
  `dump <disc path> [sd path]`, `dol [sd path]`, `xml <sd path>`,
  `set <option>=<choice>`, `replace`/`grow`/`sdreplace`/`sdgrow <disc
  path> <sd path>`, `keep <disc path>`, `hook`, `nofallback`, `boot`,
  `launch`.
- `sd:/riftwii/guiscript.txt` plays input into the menu and saves
  screenshots (`wait`, `point`, `nopoint`, `press`, `hold`, `release`,
  `glide`, `shot`, `finalshot`, `slowmo <factor>` to stretch every
  transition for frame-by-frame screenshots, and `failnext` and `crash`
  to test the way back from a failed launch and the crash screen; see
  `wii/menu/guiscript.hpp`). After a restart the menu reads
  `guiscript-restart.txt` instead. Pull the screenshots (and
  `crashscreen.bmp`) out of the SD image afterwards.

- GameCube games have test switches too: empty files in
  `sd:/riftwii/gc/test/`, read only from there so a stray file among a
  player's cards, saves and controls cannot set one. `press` (presses on
  port 1), `leave` (holds the way back), `crash` and `crashblack` (a
  crash on purpose, also on a blanked screen), `streamonly` (disc music
  alone), `nowiden` (16:9's hook without the widening), `keeparam` (ARAM
  addresses as the SDK gives them), `fakeremote` (a made-up Wii Remote
  through the controls map instead of the Bluetooth, for Dolphin, whose
  emulated remote a script cannot press), `fakepro` (the same with a
  made-up Wii U Pro Controller, which Dolphin cannot emulate, in its own
  report layout), `rumbletest` (the game's PADControlMotor called for the
  test remote's port) and `reset` (the blob presses Reset twice, the
  restart, `GC_FLAG_RESET_TEST`). The first GameCube
  line of `sd:/riftwii/boot.log` lists the ones that are on. The test
  remote's results take the `projection_callers` words of the trace
  (`tools/gc_trace.py`), which that run does not need.

Dolphin cannot run a d2x cIOS, so SD and USB image boot can only be
checked on a Wii (`docs/USB_HARDWARE_TEST.md`). What Dolphin does check
well: the menu, disc launches with packs, the resident runtime (its
Gecko lines and Dolphin's DI and SD logs), savegames, and heap placement
(RiftWii poisons the IOS reload area there, see `ARCHITECTURE.md`).

### The plugin test switch

`sd:/riftwii/plugin_test.txt` loads plugins at the menu's start, which
nothing else does, and logs every step to `session.log`:

```
load selftest
```

`selftest` (`plugins/selftest/`) logs its version, the address its code
runs from, a value computed through relocated function pointers (it must
come out `0x8af4bb0e`), a block from the plugin arena, and a read of the
test file through the host table; then its probe and, when the menu
closes, its shutdown. A line `crash` in the same file makes it crash on
purpose after that, to check that the crash screen and `crash.txt` name
`plugin selftest +0x...`. The plugins are read from the running DOL's
folder (`argv[0]`, else `sd:/apps/riftwii/plugins/`; Dolphin passes no
path), or from `plugins = <folder>` in `settings.txt`.

The refusals: a `load <name>` line for a plugin that isn't there, or a
file changed with `tools/make_plugin.py variant` (`--damage` for a broken
hash, `--api 2` or `--stamp 2610-1-g00000000` for another RiftWii's
plugin, `--name` to load it under another name), each give one log line
saying which check failed and Home's notice with the sentence a player
would see. Nothing of a refused plugin runs. Leave no `plugin_test.txt`
on a tester's card.

## Logs to ask testers for

- `sd:/riftwii/session.log`: the menu, from start to the launch.
- `sd:/riftwii/boot.log`: the last launch, up to the jump into the game.
- The Wii model, the System Menu version and which cIOS is installed.

## The Wii Remote log

A Wii U Pro Controller works in RiftWii's menu (libogc reads it) but in no
game. To learn why, a launch keeps what the game's Bluetooth and Wii Remote
libraries would print in a debug build (`runtime/wlog`): retail games
compile those prints into empty functions, and the blob takes their place.
Leaving the game through HOME > Wii Menu saves it to
`/shared2/riftwii/wpadlog.bin`; RiftWii's next start turns it into
`sd:/riftwii/wpadlog.txt`, which goes into problem reports. Link keys are
never written out and Bluetooth addresses keep only their last byte.

It is on when the menu saw a Wii U Pro Controller: a pairing alone is not
enough, because the log takes 43 KB of the game's MEM2 and the blob 10 KB of
MEM1 (Newer Super Mario Bros. Wii from an RVZ ran out of MEM2 with them).
`wpad_log = on` in `settings.txt` keeps it for every launch (Dolphin tests),
`wpad_log = off` never. Not in code builds or games that clear MEM1's top.

Test builds (`make -f Makefile.wii WII_DEFINES=-DRIFTWII_TEST_LIMITS`) can
also run a game short of memory, to find out whether a crash is one:
`sd:/riftwii/test_limits.txt` with `game_mem1_kib = N`, `game_mem2_kib = M`
or `game_mem2_top_kib = T` takes that many KiB off the end of the game's
MEM1 arena, the start of its MEM2 arena or its end. In Dolphin, Newer Super
Mario Bros. Wii v1.30 runs with 336 KiB less MEM2 and dies at the start with
352 KiB less.

The same blob runs the Wii U Pro Controller translator and keeps a trace of
the game's Bluetooth traffic in the log (docs/BLUETOOTH.md).
`pro_controller = off` keeps the trace and leaves the traffic alone;
`pro_controller = every` takes every link as a Pro Controller, so the
translator runs in Dolphin (whose Wii Remote then has no buttons of its
own). `build-dolphin/wlog_memdump.py` saves the log from the running game's
memory when HOME > Wii Menu cannot be reached.

What the logs showed: right after a controller connects, the game's Wii
Remote library reads one byte at address 0x1770, past the end of a Wii
Remote's memory, and takes the error a Wii Remote answers as "firmware 2.1
or later". The Pro Controller hangs up on that read instead. `wpad_probe =
valid` in `settings.txt` (a test, off by default) makes the probe read
0x0016 and the answer's handler take the error path anyway, so Wii Remotes
behave as before (`find_wpad_probe` in `src/patching/wpadlog.cpp`; found in
Mario Kart Wii, City Folk and Brawl).

## A game that only fails on a console

`debug_off` in `sd:/riftwii/settings.txt` turns RiftWii's launch extras
off one at a time (`wii/launch/boot.cpp`, `debug_off`), so a tester can bisect
what Dolphin cannot show:

```
debug_off = bca, fault, dolswitch, 480p, returnto, consoletype, prerun
```

`bca` the retail BCA answer, `fault` the crash recorder, `dolswitch` the
cheats' hook on jumps to another executable, `480p` the 480p fix,
`returnto` the return-to patch, `consoletype` the console type RiftWii
writes at 0x8000002C on a Wii, `prerun` the code handler's run before the
game's entry, `closepart` the partition close before a Homebrew Channel
app (CTGP) starts, `wpadlog` the Wii Remote log, `musicresume` the menu's
music coming back after an SD, USB or burned-disc launch's cIOS reload (it
fades out before the reload either way). `identify` lets a
WiiWare or Virtual Console game start with RiftWii's loader even when ES
won't run it as its own title (only for testing in Dolphin, whose ES
refuses that to anything but the Wii Menu; the game's saves may not open).
`boot.log` names each one left out. Start with all of them,
then halve the list. The PMEX Remix crash on a Wii (2.7.0 RC9.2) was found
this way: it was the crash recorder.

A code build (REX, Project+, a `gameconfig.txt` build) can also start
without these extras at all, picked at Start (or `code_build_extras =
off` for autorun): only its codes, `gameconfig.txt`'s words, the code
handler's run before the game and the GameCube adapter when one is
plugged in. REX went black on a tester's Wii U (2610-412).

`build-dolphin/sim.sh` runs one code-build launch on a card image
without folder sync and scores it (PASS or BLACK); `sim_matrix.sh` runs
every code-build card with the extras on and off. Test builds' keys in
`test_limits.txt`: `console_type_as_wii = 1` writes the console type in
Dolphin as on a console. Dolphin's IOS leaves MEM2's top where the same
IOS does on a Wii (0x935e0000 under Brawl's IOS36, 0x933e0000 under
IOS58), so a game needs no squeeze to have a Wii's memory;
`wii_mem2_top = 1` forces IOS58's layout, and `game_mem2_top_kib`
alone builds a layout no Wii has (Project+ goes black in it).

`debug_crash` in the same file breaks the game on purpose, to try the
crash recorder: `debug_crash = pad` puts a read from address 0x28 at the
start of the game's PADRead (its main thread, every frame), or give a
hex address. In Dolphin, turn on MMU emulation (`FELK_MMU=1` for
`build-dolphin/felk-run.sh`), or the bad read is only a warning and the
game crashes a few steps later instead. The next RiftWii start writes
`sd:/riftwii/gamecrash.txt`. Never leave it in a tester's settings.

## A menu that freezes only on a console

`debug_gx` in `settings.txt` watches the menu's drawing
(`wii/menu/drawtrace.hpp`). `debug_gx = watch`: for a minute after a
game's page opens, and whenever the frames stop with the GUI not halted,
the menu's watchdog thread logs a `GUI watch` line once a second: the
frame count, where the GUI thread is (`render step "GX_DrawDone"` is a
GPU that never finished its frame), whether the menu's own thread is
suspended, and how often GX's command buffer filled up. `debug_gx =
drawdone` adds the GX trace: the GUI waits for the GPU after every
element it draws and keeps the last 16 in memory (class, name, last
picture's address, size and format); when the frames stop, `GX trace`
lines name them, the one the GPU stopped in first. Drawing is slower with
it. Off by default; a GX buffer that filled up is logged either way
(`Graphics: the menu's drawing ran ahead of the GPU`).

## Releasing

1. Bump the version in `CMakeLists.txt` and `hbc/meta.xml` (`version`,
   `release_date`), then `make -f Makefile.wii clean` and build.
2. Run the host tests and the Dolphin checks above.
3. Pack the zip: `sd-card/apps/riftwii/` with `boot.dol` (the new
   `riftwii.dol`), `meta.xml`, `icon.png` and `music.ogg` (all from `hbc/`),
   and `plugins/` with every file of `build-plugins/plugins/` from the
   same build (`make_update_pack.py` refuses plugins that don't match the
   zip's `boot.dol`);
   `sd-card/apps/riftwii_channel/`
   with `boot.dol` (`build-channel/installer.dol`, from
   `make -f Makefile.channel`) and `channel/installer/hbc/meta.xml` and
   `icon.png`; `sd-card/riivolution/`,
   `sd-card/wbfs/`, `sd-card/games/`, `usb-drive/wbfs/` and
   `usb-drive/games/`, each with a short text file saying what goes
   there; a `README.txt` for players at the top.
   The GPL goes with every copy: also at the top, `LICENSE.txt` (the
   repository's `LICENSE`), `NOTICE.md`, and `SOURCE.txt` naming the
   tag and commit the zip was built from and the repository URL. The
   README's last section says the same in a few lines and lists the
   credits. Settings > Credits and license shows `LICENSE` itself (the
   build packs it).
   The in-app update's pack: `py -3.13 tools/make_update_pack.py
   riftwii-vYYMM-N.zip riftwii-update.pack` (the themes, then the
   zip's plugins, channel installer and icon). It fails a zip without
   `plugins/gamecube.<version>.rwp`: the DOL has no GameCube loader of its
   own any more. Attach the pack to every release: the in-app update
   downloads it with the DOL and writes its plugins first.
   Before going on, check the zip has the plugin folder: `unzip -l
   riftwii-vYYMM-N.zip | grep apps/riftwii/plugins/` lists one
   `<name>.<version>.rwp` per plugin, with this release's version. A zip
   without it starts, but every feature that lives in a plugin says the
   add-on is missing. `tools/make_release_zip.py` (when it is in the tree)
   copies `build-plugins/plugins/` itself; a zip packed by hand or by an
   older script needs the folder copied, so check either way.
   Keep `build-plugins/*/*.elf` with `riftwii.elf`: a crash in a plugin
   is named as `plugin <name> +0x...`, an offset into that ELF.
   The theme kit when a picture or a setting changes (a new one in
   `src/art/theme.cpp`'s list, or a new look): `cmake --build build-host
   --target theme_kit`, then `py -3.13 tools/make_theme_kit.py
   build-host/theme_kit.exe --repo` (templates painted by the menu's own
   code) refreshes `themes/kit/MyTheme` and `themes/kit/templates`;
   commit them with the release.
4. Screenshots for the notes go in `docs/screenshots/` and are committed
   before the tag; the notes load them from
   `raw.githubusercontent.com/KakarottoCake/Riftwii/<tag>/docs/screenshots/`.
   `gh release create vYYMM-N` (not `--prerelease`) with three files: the zip,
   `riftwii.dol` (what the in-app update downloads) and
   `riftwii-update.pack`. The notes give what changed and the DOL's
   SHA-256, and end with a "License and source" line: GPL-3.0-or-later,
   the source at the tag, the credits in `NOTICE.md`.
5. Every release is for everyone. It is published as GitHub's latest
   release (no `--prerelease`), titled "RiftWii YYMM-N", and every
   RiftWii updates to it: the update check reads `/releases/latest`.
   There is no Stable or Beta any more. Older builds still have that
   setting: on Stable they read the latest release too, but on Beta they
   take the newest release of all, pre-releases included, so a test
   build for a few testers goes out as a DOL in a message, never as a
   GitHub pre-release.
6. When a third-party piece is added: a row in `NOTICE.md` (what, where
   from with the commit, license), `SPDX-FileCopyrightText` lines (one per
   holder, above the license line) in each file that follows it, and a
   line in the README's Credits and in
   `wii/menu/credits.cpp`. Binaries built from someone else's source ship
   with that source, as `vendor-gecko/` does.

## Ground rules

- No Riivolution code. Behaviour comes from its public patch-format
  documentation and from Dolphin's independent implementation; see the
  provenance list in `NOTICE.md`. MIT and compatible sources may be
  adapted with credit in `NOTICE.md`.
- No Nintendo keys or assets in the repository.
- Nothing hardcoded for one game or one mod.
