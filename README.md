# Riftwii

Riftwii loads Wii game mods from your SD card while the original disc
stays untouched in the drive. You pick one or more mod packs from a menu,
and the game starts with the mods applied. It reads the same XML mod
packs that Riivolution uses, and it was written from scratch without
copying Riivolution code (see `NOTICE.md`). Licence: GPL-3.0-or-later
(`LICENSE`); third-party notices in `NOTICE.md`.

> Prerelease warning: this is an early test build. It has been checked
> with automated tests and in the Dolphin emulator, but it has barely
> run on real Wii hardware yet. Please report anything odd as a GitHub
> issue, with your Wii model, system menu version, and what was on the
> screen.

## For Wii owners: install and use

You need: a Wii with the Homebrew Channel installed and an SD card. An original
game disc remains supported. SD and USB image boot need a user-installed d2x
cIOS and FAT32 media with 512-byte sectors. Both support `.wbfs` (including
split `.wbf1`, `.wbf2`, …) in `<source>:/wbfs` and raw `.iso` in
`<source>:/games`.

1. Copy `riftwii.dol` from the release into `sd:/apps/riftwii/boot.dol`.
   If you also want the icon and description, copy `meta.xml` and
   `icon.png` next to it (any Homebrew Channel `meta.xml`/`icon.png`
   will do; they are not in this repo).
2. Put mod packs (folders with an XML file plus their files) into
   `sd:/riivolution/`, the same layout Riivolution uses. The release
   notes say which packs were tested with this build.
3. Start Riftwii from the Homebrew Channel. Select **SD**, **USB**, or
   **DISC** on the source screen. SD and USB open an image picker; DISC opens
   the matching mods screen directly.
4. The list shows matching packs as On (will be used) or Off, and keeps
   Invalid XML visible with its reason. Packs made for other games are hidden.
   Press A to switch a pack on or off. Press **Mod Options** for a pack's options,
   A and Minus to change them, B to go back.
5. Press Launch (or just start with nothing enabled to boot the plain
   disc). Your choices are saved per game, so next time they are back.
6. Home exits to the Homebrew Channel.

Controls work with a Wii Remote (pointer or D-pad), Classic Controller,
GameCube pad, or Wii U GamePad (same button names; X stands in for 1).
With no pointer on screen, only the highlighted row answers to A.

Save games: **Save Mode** cycles NAND, Separate (clones NAND progress once to
`sd:/riftwii/saves/<ID>/clone`), and Fresh (an independent initially empty
slot at `sd:/riftwii/saves/<ID>/fresh`). A package's `<savegame>` setting takes
precedence. A first boot may take a while (shaders and caches); give it time.

If the screen reports an error, it names the pack, option and file it
comes from. Nothing is launched half patched: if a pack cannot be
applied completely, the game does not start and the message tells you
why. If a mod compiles to nothing, the message names the folder and how
many files were skipped, so a misplaced external folder is fixable.

## For developers

Direction, review findings and the roadmap live in `docs/`.

## Status

- `riftwii` static library: XML patch package parsing (the full documented
  format: file/folder/memory/savegame patches, options, macros, params,
  `{$name}` placeholders), disc filtering, patch planning, read overlay
  composition, Wii FST and disc structure parsers, the redirect table
  builder and a read-only FAT32 fragment resolver (all host-tested).
- `riftwii_runtime` static library (`runtime/`): freestanding C99 redirect
  table walker shared by the host tests and the future resident runtime.
- Wii frontend (`wii/`): own `/dev/di` client, IOS reload, disc probe,
  apploader run and handoff (`boot.cpp`), file dump through the FST, and a
  headless autorun mode for Dolphin. Mario Kart Wii boots and plays from
  `riftwii.dol` in Dolphin; not yet run on hardware.
- Resident runtime (`runtime/resident/`): a position-independent blob the
  loader installs at the top of the MEM1 arena (its tables and buffers at
  the top of the MEM2 arena); it hooks the game's
  `IOS_IoctlAsync` (found by structure, not by SDK patterns), sees every
  disc read and serves same-size replacements from memory through the
  redirect table, files of a new size through the virtual window (FST
  rewritten in the loader, reads above the disc answered from memory), and
  files stored on the SD card (fetched sector by sector through the game's
  own IPC, no file system in the runtime), and the disc's own bytes for
  relocated or partially patched files. Verified in Dolphin against
  Dolphin's own DI and SD logs and by checksums of the rewritten game
  buffers, on Mario Kart Wii, Super Smash Bros. Brawl, Wario Land: Shake
  It! and Kirby's Epic Yarn (SDKs from 2007 to 2009; the runtime's code
  lives in MEM1 because a 2009 SDK keeps no instruction BAT for MEM2,
  see `docs/CONDUCTOR_REVIEW_2.md` section 23). A Riivolution-format
  package runs end to end this way
  (`wii/modplan.cpp`): `<file>` and `<folder>` patches, including created
  files, and `<memory>` patches (plain, search and ocarina) applied
  before the game starts, with several packages composing in order and
  option choices applied over the defaults; the GUI enables packages,
  sets their options and launches, keeping the choices per game.
  `<savegame external>` serves the title's NAND data directory from a
  folder on the card: the resident runtime hooks the SDK's IOS calls
  and answers the ISFS requests from its own FAT32 engine over SDIO,
  importing the temporaries the SDK's safe write renames in (Mario
  Kart Wii's `rksys.dat` lands on the card in Dolphin); `clone` copies
  the NAND save into a folder created at launch, as the game (a hidden
  `riftwii.cln` in the folder marks a clone still due; the game never
  sees hidden entries).
- `vendor-pugixml`: MIT-licensed XML parser, pinned at v1.15.
- `vendor-libgui`: pinned GPL libwiigui 1.07 snapshot, used only by the Wii
  frontend (not built by the host build).

## Host build

Requires CMake 3.20+, Ninja, and a C++17 compiler.

```
cmake -S . -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## Wii frontend

Requires devkitPPC, libogc, libfat, and the Wii/PPC portlibs used by
`Makefile.wii`. Run from the project root in the devkitPro shell:

```
make -f Makefile.wii
```

This produces `riftwii.dol` and `riftwii.elf`. The build uses relative paths
and supports a project directory containing spaces. The frontend links the
XML/overlay core and libwiigui; it is not part of the host test build.

The frontend identifies the selected source, scans `sd:/riivolution` for
XML files (up to 150), and lists matching files as On or Off plus Invalid
files (with the parse error in the details line). Files whose `<id>` does not
match the selected game's ID, revision, or disc number are hidden. A enables
or disables the highlighted package; "Mod Options" (Wii Remote
Plus / GameCube X) opens its options, where A moves an option to its
next choice, Minus to the previous one and B returns; "Launch" (Wii
Remote 1 / Classic Y / GameCube Y) compiles the enabled packages with
their choices, saves the choices to `sd:/riftwii/choices/<game id>.txt`
(restored on the next start) and boots, or boots the disc unmodified
when nothing is enabled, logging to `sd:/riftwii/boot.log`. Home exits.
Wii Remote 2 / GameCube B ("Dump", a development aid without a button)
writes the disc header, partition table, TMD, FST, apploader and
`/opening.bnr` to `sd:/riftwii/dump/`. Files larger than 1 MiB are
rejected rather than silently truncated. Unknown attributes and elements
are ignored and counted as warnings in the details line.

### Manual testing

Copy `riftwii.dol` to `sd:/apps/riftwii/boot.dol` and launch it from the
Homebrew Channel, or load the DOL in Dolphin with an SD image configured.

### Headless runs in Dolphin

If `sd:/riftwii/autorun.txt` exists the frontend skips the GUI and runs
the commands in it (`sd <path-or-id> [cios-slot]`, `usb <path-or-id> [cios-slot]`,
`disc`, `probe`, `layout`, `meta [dir]`, `dump <disc path>
[sd path]`, `dol [sd path]`, `nofallback`, `hook`, `replace <disc path>
<sd path>`, `grow <disc path> <sd path>`, `sdreplace <disc path> <sd
path>`, `sdgrow <disc path> <sd path>`, `keep <disc path>`, `xml <sd
path>`, `set <option>=<choice>`, `boot`, `launch`), logging to
`sd:/riftwii/autorun.log`;
under Dolphin it powers off afterwards so the SD folder syncs back.
`tools/dolphin/run.sh` drives this: it expects `build-dolphin/user/` (an
isolated Dolphin user directory with `WiiSDCard`, folder sync and a
`DefaultISO` pointing at your own dump, plus the USB Gecko on slot B) and
prints the Gecko and Dolphin logs after the run:

```
DOLPHIN_DIR=/c/path/to/Dolphin-x64 tools/dolphin/run.sh 45
```
Verify startup, pointer/controller navigation, scrolling, package details,
rescan, and exit. Test missing/empty package directories, malformed XML,
uppercase `.XML` extensions, files larger than 16 KiB, and the 1 MiB limit.
Test more than eight packages to exercise scrolling. Hardware/emulator
behavior has not been verified by the automated host tests.

## Scope

The loader validates and plans Riivolution-format XML patches and composes
read overlays.

Parsing (`parse_package`) accepts the whole documented format, including the
XML declaration, `shiftfiles`, `<folder>`, `<memory>` (plain/ocarina/search),
`<savegame>`, `<macro>`/`<param>`, bare file-name `disc` targets and
`{$__gameid}`/`{$__region}`/`{$__maker}`/param placeholders. Unknown
attributes and elements are tolerated and reported in `Package::warnings`;
malformed values, DOCTYPE/entities and non-declaration processing
instructions are rejected. The fixtures under `tests/fixtures/` exercise
every construct and are authored for this project.

Planning (`plan_package`) resolves the selected choices for a disc identity:
placeholders are substituted, paths resolved, and any selected feature the
runtime cannot execute yet (controlled by `PlanOptions`) makes planning fail
with a message naming the option, choice, patch and feature, so a mod is
never launched partially applied.

Replacement (`build_replacement`, `apply_patches`): `DirectoryProvider`
(game dir + SD dir) feeds the engine, which turns planned `FilePatch`es into
an owned `AppliedFile` view backed by `ReadOverlay`; several patches on one
disc file compose in order. Covered and tested: `offset` (low 2 bits
cleared, since DI reads address the disc in 4-byte words), `fileoffset`,
`length` (0 = rest of external), `resize` (truncate/extend vs keep tail),
`create` (a disc file reported *not found* becomes an empty original; I/O
errors never do), short-external zero padding, past-EOF zero gaps, overflow
checks, and a 256 MiB per-file cap that holds for files above 4 GiB. The
end-to-end test runs XML text -> `parse_package` -> `plan_package` ->
provider -> split `read()` consumption and compares every byte against an
oracle.

Still future: hardware runs of everything the Dolphin sections of
`docs/CONDUCTOR_REVIEW_2.md` cover (the runtime architecture and the
gate sequence are described there).
