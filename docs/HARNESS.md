# Dolphin test harness

How to boot Riftwii builds in Dolphin on this machine, and where every
piece of state lives. Intended for agents and humans taking over mid
session. Paths are Windows style unless noted.

## Pieces

- Emulator: `D:/Eclipse111/dolphin-test/Dolphin-x64/Dolphin.exe`
  (portable build, `portable.txt` present, but every run below passes
  `-u` so the portable dir is only a fallback).
-User dir: `build-dolphin/user` (always passed with `-u`). Holds
  `Config/Dolphin.ini`, the NAND, `Load/`, `Dump/`, `Logs/`.
- Device under test: `riftwii.dol` in the repo root, booted with `-e`.
- Headless helper: `C:/dolphin-trace/build/Binaries/DolphinNoGUI.exe`
  exists but the GUI binary above is the proven one.

## Boot flow

```
Dolphin.exe -u "<repo>/build-dolphin/user" -e "<repo>/riftwii.dol"
```

Quoting matters: the repo path contains a space. A broken `-e` path
shows a `Warning / Could not recognize file D:/AI` dialog and boots
nothing. Pass args as one quoted string.

`tools/dolphin/run.sh SECS` (run under msys bash with
`DOLPHIN_DIR` set to the emulator folder) boots the DOL, captures
USB Gecko output (TCP 55020) to `build-dolphin/gecko.log`, then kills
Dolphin after SECS. Dolphin log lands in
`build-dolphin/user/Logs/dolphin.log`.

## Disc images

- `D:/Games/Wii/Super Mario Galaxy 2 (USA) (En,Fr,Es).rvz` (SB4E01,
  Spectral base).
- `J:/Backup/Classic Games/gamecube/Super Mario Sunshine (USA)/New
  Super Mario Bros. Wii (USA) (En,Fr,Es) (Rev 2).iso` (SMNE01, Newer
  base).
- `D:/Games/Wii` holds more (Kirby, Galaxy 1, Sunshine mods). The
  active image is `DefaultISO` in `build-dolphin/user/Config/Dolphin.ini`.

## SD images and sync

- `build-dolphin/user/Config/Dolphin.ini` selects them:
  `WiiSDCardPath`, `WiiSDCardFilesize`, `WiiSDCardSyncFolder`
  (always `.../user/Load/WiiSDSync`), `WiiSDCardEnableFolderSync`.
- Known raws in `build-dolphin/user/Load/`:
  `WiiSD-spectral.raw` (2 GiB, Spectral + Newer packs),
  `WiiSD-newer.raw` (1 GiB, Newer era),
  `WiiSD.raw` (64 MiB, oldest).
- The 1 GiB image cannot hold Spectral (482 MB) next to Newer;
  size the raw for the packs inside it.
- Sync is sticky both ways and a folder edit does not always win
  over the raw on boot. When the emulated SD disagrees with the
  folder, point at a FRESH raw and let Dolphin build it from the
  folder. Keep the old raws; never delete, only repoint.
- A big card (REX's is 12 GB) takes many minutes to build at every
  boot and to sync back at every close. Build it once, then run
  `FELK_NOSYNC=1 felk-run.sh ...`: Dolphin uses `WiiSD-<card>.raw` as
  it is, and a forced close loses nothing. Change files in the image
  with `tools/dolphin/sdimg.py <raw> ls|get|put|rm ...` (paths without
  a leading slash) while Dolphin is closed. A REX run went from 15-30
  minutes to under 3.
- Backups live next to the tree: `build-dolphin/Dolphin-*.ini`
  (per-setup configs) and `build-dolphin/autorun-*.txt`.

## Autorun

- With `sd:/riftwii/autorun.txt` present the **test build** (`make -f
  Makefile.wii TEST=1`) runs headless commands instead of the GUI. A release
  DOL ignores the file and logs that it did: point `felk-run.sh` at the test
  DOL for every run that uses an `autorun.txt`. Current Spectral script:
  `disc, probe, layout, xml, set Core=Enabled, boot`.
- The `xml` command takes ONE word: stage spaced filenames without
  spaces (`SpectralUSA.xml`, not `Spectral USA.xml`).
- The `xml` command compiles package defaults, so a package whose
  option has no `default="1"` fails there before any `set` line
  runs. The staged Spectral copy carries that one attribute delta
  from the reviewed file; it is on record in the v0.3.8 notes.
- No `autorun.txt` means the GUI menu (used for layout shots).

## Logs and screenshots

- `build-dolphin/gecko.log`: the DOL console (autorun progress,
  folder expand notes, FST/virtual-window lines, memory notes,
  `R`/`M` DI lines). Start `tools/dolphin/gecko_log.py` before
  booting, or use `run.sh`.
- Screen scraping (`tools/dolphin/shot.ps1`) loses to focus fights
  on a busy desktop. Deterministic alternative: set
  `[Settings] DumpFrames = True` in the user `Config/GFX.ini`,
  reboot, kill Dolphin to flush, extract frames with ffmpeg
  (`ffmpeg -ss <sec> -i <avi> -frames:v 1 out.png`). Turn dumping
  back off afterwards: every run writes hundreds of megabytes.
- Menu input via keys or clicks is UNPROVEN as of v0.3.9. GC-pad
  mapping in the user config says S = GC-Y (SD), C = GC-X (USB),
  Z = GC-B (Back) and Wiimote A = left mouse with IR on the OS
  cursor, but presses never landed in-session (missed frames on a
  slow machine are suspected). Proven instead: throwaway staged
  catalog statuses in `MenuSource` (a local-only hack, always
  reverted and rebuilt before committing).
- DTM movies: `tools/dolphin/make_dtm.py` writes the header plus
  Wiimote frames (`01 00` empty, `03 01 <b> <d>` pressed). Button
  byte guess (A=0x01, B=0x02, 1=0x04, 2=0x08, -=0x10, +=0x20,
  d-pad byte separate) is UNCONFIRMED. The prebuilt
  `build-dolphin/ui_menu.dtm` predates this workflow.

## Quirks

- Two Dolphin processes at once happens (a zombie plus a live
  one); kill all before a clean run.
- A run that must sync files back has to END without booting a
  game (autorun poweroff path), else writes stay in memory.
- `taskkill //IM Dolphin.exe //F` from msys, or
  `Stop-Process -Name Dolphin -Force` from PowerShell.
- Game-list "Refreshing..." on boot can stall on slow drives;
  it is the browser, not the emulation.

## WiiWare and Virtual Console

Felk's Dolphin answers `ES_Identify` with -1017, so a channel RiftWii
starts has no title of its own: no save folder, no content (see
docs/WIIWARE.md). `tools/dolphin/es-identify.patch` adds it, and a
DolphinTool `installwad` command, to the same Felk source; the build goes
in `build-dolphin/felk-identify`. A ready Windows build is the
`riftwii-felk-identify.zip` of
https://github.com/RiftWii/dolphin-felk-riftwii/releases (source: that
repository's `es-identify` branch):

- `FELK_DIR=felk-identify felk-run.sh <card> <test dol>` runs it, with an
  `autorun.txt` of `channelboot <16-digit title>`. No `debug_off =
  identify`: ES runs the game as its own title, as on a console.
- `felk-identify/DolphinTool.exe installwad build-dolphin/user-felk
  <wad>...` installs WADs for good. A WAD booted directly with `-e` is
  only installed until emulation stops; its contents are removed then.
- `tools/dolphin/frameshot.py` is a Felk script for unattended runs: it
  saves the frame at set frame counts (`FRAMESHOT_OUT`, `FRAMESHOT_AT`).
  Give Dolphin its paths with forward slashes, and the card settings
  `felk-run.sh` passes: left out, Dolphin rebuilds whatever card it used
  last at every boot.

### Testing cIOS selection with the d2x Dolphin fork

The fork's `d2x-emulation` branch can recognise a synthetic d2x IOS slot.
Use that build for these tests; ordinary Dolphin cannot run the cIOS
paths. Stop emulation before editing its isolated user folder:

```sh
python tools/dolphin/make_d2x_slot.py build-dolphin/user-felk 249 56
python tools/dolphin/make_d2x_slot.py build-dolphin/user-felk 250 57 --major 11 --revision 21011 --release beta3
```

The arguments are the Dolphin user folder (the one passed with `-u`),
the slot (200-255, in decimal), and its base IOS (for example 56, 57 or
58). Defaults are d2x v11 beta3, revision 21011. Under the user folder's
`Wii/`, this writes `title/00000001/000000f9/content/title.tmd`,
`00000000.app` beside it, and `ticket/00000001/000000f9.tik` for slot
249. Other slots use their own eight-digit hexadecimal name. The content
is only a 64-byte version block; the TMD lists its size and SHA-1. There
is no d2x program, Nintendo data, key or real signature. These records
are for the emulator only. Repeating the same command is safe; different
existing records are refused, so choose an unused slot instead.

Build RiftWii with both test switches:

```sh
make -f Makefile.wii TEST=1 WII_DEFINES=-DRIFTWII_TEST_LIMITS -j8
```

Put `cios_in_dolphin = 1` in the test card's
`riftwii/test_limits.txt`. RiftWii then reads and checks the installed
cIOS slots and chooses them as on a Wii: image launches, channels,
d2x saves, and the menu's cIOS paths. Copied NAND channels require d2x
instead of falling back to a stock IOS. A cIOS reload needs an installed
ticket. The session log says the switch is enabled. Missing or zero
keeps the usual Dolphin behavior; a build without
`RIFTWII_TEST_LIMITS` ignores the key entirely. Dolphin's memory poisoning,
console type, and other hardware adjustments stay in place. Channel
installation remains blocked because the switch does not bypass signature
checks.

This is preparation for the fork's d2x support, not evidence that all
d2x commands work. Slot recognition alone does not provide NAND emulation,
game images on USB or SD, reload blocking, or return-to behavior. Test
those only once the fork implements them; card mounting and real-console
timing still need a Wii. The metadata layout follows
[WiiBrew's Title metadata page](https://wiibrew.org/wiki/Title_metadata)
and was checked against Dolphin's `TMDReader::IsValid`.
