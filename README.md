# RiftWii

RiftWii loads Wii game mods from your SD card into games played from
the disc, a USB drive or the SD card; the game files themselves are never
changed. You pick one or more mod packs from a menu,
and the game starts with the mods applied. It reads the same XML mod
packs that Riivolution uses, and it was written from scratch without
copying Riivolution code (see `NOTICE.md`). Licence: GPL-3.0-or-later
(`LICENSE`); third-party notices in `NOTICE.md`.

> 1.0 Beta: the menu and the mod engine are checked by host tests and
> in the Dolphin emulator, and players are running it on real Wiis. USB and SD game boot
> depend on d2x and are the least proven. Please report anything odd as
> a GitHub issue, with your Wii model, system menu version, and
> `sd:/riftwii/boot.log`.

## For Wii owners: install and use

You need: a Wii with the Homebrew Channel installed and an SD card. An original
game disc remains supported. SD and USB image boot need a user-installed d2x
cIOS (slot 249, 250 or 251). The SD card must be FAT32; a USB drive may be
FAT32 or NTFS; both need 512-byte sectors. Both support `.wbfs` (including
split `.wbf1`, `.wbf2`, …) in `<source>:/wbfs` and raw `.iso` in
`<source>:/games`.

1. Copy `riftwii.dol` from the release into `sd:/apps/riftwii/boot.dol`,
   with `hbc/meta.xml` and `hbc/icon.png` next to it for the Homebrew
   Channel's name, description and banner. (The release zip has all
   three in place.)
2. Put mod packs (folders with an XML file plus their files) into
   `sd:/riivolution/`, the same layout Riivolution uses.
3. Start RiftWii from the Homebrew Channel. It reads the SD card and the
   USB drive and shows the games that have mod packs as tiles, with the
   disc drive first. The round button at the bottom left (or 1) switches
   between games with mods and all games. Games with packs carry a
   **MODS** tag.
4. Pick a game. Its page lists only the packs made for it; broken XML
   shows its error under the pack. A on a pack turns it on and shows its
   settings under it; A on a setting steps it, Minus steps it back.
   Options that share an id and a section across packs show once, as
   Riivolution does.
5. **Saves**, the first row, keeps the Wii saving as usual, or keeps this
   game's saves on the SD card: cloned once from the Wii's save
   (`sd:/riftwii/saves/<ID>/clone`) or started fresh
   (`sd:/riftwii/saves/<ID>/fresh`). While a pack that brings its own
   saves (`<savegame>` in its XML) is on, the row reads "Kept by the
   pack" and cannot be changed; turning the pack off restores your
   choice.
6. **Start** (Plus) boots, with nothing on for the plain game. While it
   starts, the log prints on screen. Your choices are saved per game.
7. Settings (the gear, or 2) holds the menu IOS (pick the cIOS slot with
   fakemote for USB DS3/DS4 pads), network packs, Rescan and Exit. HOME
   exits too.
8. **Network packs (RiiFS).** Packs can come from a PC running a RiiFS
   server, as with Riivolution: put an XML in `sd:/riivolution` with
   `<network protocol="riifs" address="192.168.1.20" port="1137"/>`, or
   turn on *Find network packs* in Settings to look for servers on your
   network. The server's packs show with `@ address` after their name.
   RiftWii copies what a launch needs into `sd:/riftwii/riifs/` first
   (only files whose size changed; *Copy network packs again* forces a
   full copy), then boots from the card. Saves stay on the card. See
   `docs/RIIFS.md`.

Pulsar packs (Retro Rewind and others) save their settings, ghosts and
leaderboards to the SD card, as they do under Riivolution. CT-CODE
packs that replace the game's `main.dol` (CTGP Revolution 1.02) work
too.

Controls work with a Wii Remote (pointer or D-pad), Classic Controller,
GameCube pad, or Wii U GamePad (same button names; X stands in for 1).
The GameCube control stick and the Classic Controller's left stick move
a pointer like a Wii Remote's; the D-pad moves the highlight. With no
pointer on screen, only the highlighted tile or row answers to A.

If a mod cannot start, the screen names the pack, option and file it
comes from. Nothing is launched half patched. A memory patch whose file
is missing from the card is skipped with a warning (as Dolphin does),
and a missing folder is logged with what the nearest existing folder
holds.

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

The frontend (`wii/rift_menu.cpp`) reads both drives into a grid of
games, filters it by the packs in `sd:/riivolution`, and for the picked
game lists its packs (those whose `<id>` matches the game's ID, revision
and disc number) with their options. Choices are saved to
`sd:/riftwii/choices/<game id>.txt` and restored on the next start; a
launch logs to `sd:/riftwii/boot.log`, the menu to `session.log`. The
menu's artwork is painted at start on a small software canvas
(`include/riftwii/canvas.hpp`, host-tested) into GX textures, so it
ships no image files. On the game page, the Wii Remote's 2 or a
GameCube pad's Z ("Dump", a development aid) writes the disc header,
partition table, TMD, FST, apploader and `/opening.bnr` to
`sd:/riftwii/dump/`. `sd:/riftwii/guiscript.txt` (see
`wii/guiscript.hpp`) plays scripted input and saves screenshots, for
checking the menu in Dolphin.

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
