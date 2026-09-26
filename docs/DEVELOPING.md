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
lists (FreeType, zlib, brotli, libogg/tremor), and the `zstd` command-line
tool (devkitPro's MSYS2 has it), which packs the runtime blobs. It first builds the
two in-game blobs with `Makefile.runtime` (`build-runtime/riftwii_rt.bin`,
the resident runtime, and `riftwii_pad.bin`, the pad hook), checking that
each is position-independent, then embeds them with the font, the
translations and the Gecko code handler.

Things that bite:

- `Makefile.wii` compiles every source into one flat folder, so two
  sources may not share a base name (`redirect.cpp` and `rtable.c`, not
  two `redirect`s).
- The version lives in `CMakeLists.txt` (`project(Riftwii VERSION ...)`
  and `RIFTWII_VERSION_SUFFIX`). It reaches the code as a `-D` flag with
  no dependency tracking: after changing it, `make -f Makefile.wii clean`.
- The pugixml `-Wmaybe-uninitialized` warning is expected.

## Tests

`ctest` runs 22 suites, one per area (`tests/<area>_tests.cpp`): the XML
parser and planner, the patch engine, FST and disc parsing, FAT32 and
NTFS walkers, the WBFS mapper, the resident runtime's C code (compiled
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
  `glide`, `shot`, `finalshot`, and `failnext` and `crash` to test the
  way back from a failed launch and the crash screen; see
  `wii/guiscript.hpp`). After a restart the menu reads
  `guiscript-restart.txt` instead. Pull the screenshots (and
  `crashscreen.bmp`) out of the SD image afterwards.

Dolphin cannot run a d2x cIOS, so SD and USB image boot can only be
checked on a Wii (`docs/USB_HARDWARE_TEST.md`). What Dolphin does check
well: the menu, disc launches with packs, the resident runtime (its
Gecko lines and Dolphin's DI and SD logs), savegames, and heap placement
(RiftWii poisons the IOS reload area there, see `ARCHITECTURE.md`).

## Logs to ask testers for

- `sd:/riftwii/session.log`: the menu, from start to the launch.
- `sd:/riftwii/boot.log`: the last launch, up to the jump into the game.
- The Wii model, the System Menu version and which cIOS is installed.

## Releasing

1. Bump the version in `CMakeLists.txt` and `hbc/meta.xml` (`version`,
   `release_date`), then `make -f Makefile.wii clean` and build.
2. Run the host tests and the Dolphin checks above.
3. Pack the zip: `sd-card/apps/riftwii/` with `boot.dol` (the new
   `riftwii.dol`), `meta.xml` and `icon.png`; `sd-card/apps/riftwii_channel/`
   with `boot.dol` (`build-channel/installer.dol`, from
   `make -f Makefile.channel`) and `channel/installer/hbc/meta.xml` and
   `icon.png`; `sd-card/riivolution/`,
   `sd-card/wbfs/`, `sd-card/games/`, `usb-drive/wbfs/` and
   `usb-drive/games/`, each with a short text file saying what goes
   there; a `README.txt` for players at the top.
4. `gh release create vX.Y.Z-beta --prerelease` with the zip and
   `riftwii.dol`, the notes giving what changed and the DOL's SHA-256.

## Ground rules

- No Riivolution code. Behaviour comes from its public patch-format
  documentation and from Dolphin's independent implementation; see the
  provenance list in `NOTICE.md`. MIT and compatible sources may be
  adapted with credit in `NOTICE.md`.
- No Nintendo keys or assets in the repository.
- Nothing hardcoded for one game or one mod.
