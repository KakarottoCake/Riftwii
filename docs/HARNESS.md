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
- Backups live next to the tree: `build-dolphin/Dolphin-*.ini`
  (per-setup configs) and `build-dolphin/autorun-*.txt`.

## Autorun

- With `sd:/riftwii/autorun.txt` present the DOL runs headless
  commands instead of the GUI. Current Spectral script:
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
