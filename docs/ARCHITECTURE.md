# How RiftWii works

How the code fits together, for anyone changing it; [MAP.md](MAP.md)
lists every file. RiftWii is a Homebrew Channel
app that starts a Wii game with Riivolution-format mod packs applied,
without changing the game's files. It runs in four phases; each section
below names the files that own it.

```
 Homebrew Channel
   └─ RiftWii menu (wii/)  ── reads drives, packs, choices
        └─ plan the launch (src/, wii/modplan.cpp)
             └─ boot the game (wii/boot.cpp)  ── IOS, apploader, patches
                  └─ in the game: resident runtime + pad hook (runtime/)
```

## 1. The menu

| Piece | Files |
| --- | --- |
| Screens (Home, game page, Mods, Cheats, Settings, adapter test) | `wii/rift_menu.cpp` |
| Widgets: the tile grid and the row list | `wii/gui_gamegrid.cpp`, `wii/gui_flowlist.cpp` |
| Artwork, painted at start (no image files) | `wii/skin.cpp`, `src/canvas.cpp` |
| Menu state: the picked game, its packs, the choices | `wii/frontend.cpp`, `src/launch.cpp` (`LaunchModel`) |
| Riivolution's choices file (`riivolution/config/<ID4>.xml`), read and written | `src/riiconfig.cpp`, `wii/frontend.cpp` |
| Game images on SD and USB (WBFS, split WBFS, ISO; FAT32 and NTFS) | `wii/usbcatalog.cpp`, `src/usbgame.cpp`, `src/fat32.cpp`, `src/ntfs.cpp` |
| Cover art (GameTDB), stored as ready GX textures | `wii/covers.cpp`, `src/coverart.cpp`, `wii/gui_gamegrid.cpp` |
| Game names (GameTDB), cheats (GeckoCodes archive) | `wii/online.cpp`, `src/titles.cpp`, `src/cheats.cpp`, `src/http.cpp`, `wii/netsock.cpp` |
| Online servers (Wiimmfi, WiiLink WFC, AltWFC, custom) | `src/wfcpatch.cpp`, `wii/wfc.cpp`, `vendor-wwfc/` |
| Per-game fixes (Kirby's MetaFortress, NSMBW, Prince of Persia, RE4, Excite Truck, error #002, 480p) and the automatic cIOS choice, as USB Loader GX makes them | `src/gxpatches.cpp`, `src/gxkirby.inc`, `wii/boot.cpp`, `wii/usbcatalog.cpp` |
| Update check (GitHub over https: BearSSL, `Makefile.bearssl`) | `wii/online.cpp`, `src/update.cpp`, `wii/tls.cpp`, `wii/tlsroots.c` |
| Network packs (RiiFS) | `wii/netpacks.cpp`, `src/riifs.cpp`, `src/riifs_sync.cpp`, `docs/RIIFS.md` |
| Settings, play history, translations | `wii/loadersettings.cpp`, `src/settingsfile.cpp`, `src/playhistory.cpp`, `wii/i18n.cpp`, `tools/lang_source.py` |
| Menu IOS (IOS 58 or a d2x cIOS slot) | `wii/menuios.cpp` |
| GameCube adapter in the menu (its controllers work the menu; Settings' test page) | `wii/gcadapter.cpp` (drives `runtime/rtgcad.c` with libogc's IPC, or on IOS 58 through libogc's USB handle), `vendor-libgui/source/input.cpp` |
| Memory limits: the heap never enters memory a launch overwrites | `wii/memlimits.cpp` |
| Launches another loader asks for (`--launch` arguments, no menu; `docs/HEADLESS.md`) | `wii/headless.cpp`, `src/launchargs.cpp` |
| Screenshots: the menu's, and importing the ones games left on the NAND as PNGs | `wii/screenshot.cpp`, `src/shotfile.cpp`, `src/pngencode.cpp` (a PNG writer with its own deflate) |
| Problem reports: gathered, sent to paste.rs (dpaste.com when that fails), shown as a link and a QR code; a game's crash record imported at start | `wii/reportsend.cpp`, `src/problemreport.cpp`, `src/qrcode.cpp`, `src/gamefault.cpp`, `src/http.cpp` (the POST) |

Everything the menu decides is plain data (`LaunchModel`, the per-game
choices file) and host-tested; the screens only draw and read the pads.
Text goes through libwiigui's `GuiText`, which translates every string
it is given; `tools/lang_source.py` holds the table and checks that
every entry still appears in the sources.

## 2. Planning a launch

| Step | Files |
| --- | --- |
| Parse the XML (the whole documented format) | `src/patch.cpp` (`parse_package`) |
| Resolve choices, params and placeholders for this disc | `src/patch.cpp` (`plan_package`) |
| Turn file and folder patches into byte sources | `src/apply.cpp`, `src/overlay.cpp`, `src/expand.cpp`, `src/source.cpp` |
| Memory patches (plain, search, ocarina) | `src/mempatch.cpp` |
| The redirect table the runtime walks | `src/redirect.cpp`, `runtime/rtable.c` |
| Glue on the Wii: packs to boot options | `wii/modplan.cpp` |

A selected feature the runtime cannot carry out makes planning fail with
the pack, option and file named, so a game never starts half patched.

## 3. Booting

`wii/boot.cpp` does what the System Menu and an apploader would:

1. Pick the IOS (`wii/ios_reload.cpp`). Launches that need the SD card or
   the GameCube adapter keep the running IOS and report the game's own
   to it; the rest reload to the IOS the game asks for.
2. Open the disc (`wii/di.cpp`), or for an SD/USB image reload into a d2x
   cIOS and hand it the image's fragment list (`wii/usbcatalog.cpp`,
   `activate_image_game`).
3. Run the game's apploader. Overrides replace what it loads on the way
   in: the rewritten FST (files that grew or were created move into a
   virtual window above the disc), the data header, a pack's `main.dol`.
4. Install the resident runtime (`wii/resident.cpp`), the pad hook
   (`wii/padhook.cpp`), the screenshot hook (`wii/shothook.cpp`) and the
   game crash hook (`wii/faulthook.cpp`);
   apply memory patches, cheats (the Gecko code
   handler, `vendor-gecko/`), video patches (`src/videopatch.cpp`: width,
   deflicker, borders, and a forced TV format that converts the game's
   render mode tables) and the game language (`src/gamelang.cpp`); last,
   the online server (`wii/wfc.cpp`), which may take memory below the
   MEM1 and MEM2 arena ends.
5. Write the low-memory globals and jump to the game.

## 4. Inside the game

**Resident runtime** (`runtime/resident/`). A position-independent blob:
the build links it at two addresses and refuses it if the bytes differ.
It hooks the game's `IOS_IoctlAsync`, found by structure
(`src/symsearch.cpp`), not by per-game addresses, and answers disc reads
from the redirect table: bytes from memory, from the SD card (raw SDIO,
or d2x's SD device when the game itself is on the card), or from the
disc at another offset. Failed SD and disc reads are retried three times.
With `<savegame>` it also answers the game's NAND file calls from a
folder on the card (`runtime/rtfs.c` on the FAT32 engine
`runtime/rtfat.c`), and it serves Riivolution's `file` device for
Pulsar packs. On the raw `/dev/sdio/slot0` path the card gets one
command at a time: savegame transfers and mod file reads wait for each
other, and a failed transfer is tried again after a pause (2, 8, 32 ms).
What still fails is written to a one-sector card log
(`sd:/riftwii/cardlog.bin`) that the menu turns into
`sd:/riftwii/cardlog.txt` at the next start (`src/cardlog.cpp`).

**Pad hook** (`runtime/pad/`, `runtime/rtgcad.c`). With the GameCube
adapter on, a second blob hooks the game's `PADRead` and
`PADControlMotor` (found by structure too) and runs the WUP-028 driver
through the game's own asynchronous IPC. The adapter's controllers fill
ports with nothing plugged in; rumble goes back to them.

**Screenshot hook** (`runtime/shot/`, `runtime/rtshot.c`). With In-game
screenshots on, a third blob hooks the game's `IOS_IoctlvAsync` and, when
the game has one, `PADRead`. Each Bluetooth ACL read from the dongle's
bulk-in endpoint gets the blob's completion in place of the game's: the
Wii Remote's input report is read (and HOME hidden while the combo holds
it) before the game's callback runs. On the combo (1 held, then HOME; or
L and R held, then Down) it copies the frame the video interface shows
into MEM2 and writes it to `/shared2/riftwii/shotNNNN.raw` on the NAND
through the game's own asynchronous IOS calls, one request from each
completion, so the game never waits. Where another blob already hooked
the function (the resident runtime's `IOS_IoctlvAsync`, the adapter's
`PADRead`), the replay slot takes that blob's branch and both run. The
menu turns the files into PNGs at its next start. `docs/BLUETOOTH.md`
has what the same tap would take to support other Bluetooth
controllers.

**Game crash hook** (`runtime/fault/`, `runtime/rtfault.c`). On every
launch a fourth blob (about 2.4 KB) hooks the game's
`__OSUnhandledException`, found by the code that builds the address of
its "Unhandled Exception %d" string (`find_unhandled_exception`, checked
on nine games with `tools/ipcscan.cpp`). Every exception the game has no
handler for ends there, and so do crash screens a game or mod installs
with `OSSetErrorHandler`. The blob records the registers, the stack's
return addresses and the code around the fault, then carries on into
the function. The game runs its exception handler with interrupts off,
so its own IOS calls would never complete: the blob drives the IPC
registers itself, polling for IOS's acknowledgement and reply (and
passing over a reply to a request the game made before it crashed), on
a 4 KB stack of its own. It writes `/shared2/riftwii/crash.bin`. At its
next start the menu turns that into `sd:/riftwii/gamecrash.txt`, deletes
it, and offers to send a problem report. A game that freezes without an
exception leaves nothing.

**Virtual SD card** (`runtime/vsd/`, `runtime/rtvsd.c`, `wii/vsdhook.cpp`).
When the code builds launched are inside `riftwii/sd.raw` (a FAT32 card
image on the SD card or the USB drive; `wii/vsdimage.cpp` lists them
through a read-only `vsd:` mount of the image), a blob of about 9 KB
hooks the game's IOS_Open, IOS_Close, IOS_Ioctl and IOS_Ioctlv, sync and
async (`find_ipc_api`). An open of `/dev/sdio/slot0` gets a handle of
the blob's own, and everything on it is answered by a card model
(`rtvsd.c`, host-tested): the host controller registers, the card
commands and registers (an SDHC card above 2 GiB, a standard one below),
the card event left pending. Its block reads and writes become requests
on the device holding the image, through the game's own IPC functions:
the SD slot the loader selected (with CMD13 polls after writes), d2x's
`/dev/sdio/sdhc` for a game on the card, or d2x's `/dev/usb2` for an
image on the USB drive (opened before the game partition, so for a game
on the USB drive only). The code and its state (the image's extents, a
32 KB bounce buffer for USB) go to the bottom of the MEM2 arena, staged
at its top and copied down at the jump; the game reaches them through
veneers at `0x80002300`, the code handler's list room a code build
leaves free (below the MEM1 arena's top without it). Not with the
resident runtime yet.

## Memory

| Where | What |
| --- | --- |
| `0x80000000`–`0x80003400` | Low-memory globals; the Gecko code handler at `0x80001800` |
| `0x80A00000`–`0x81200000` | The RiftWii loader (link address in `Makefile.wii`) and its heap |
| `0x81200000` | The game's apploader, while it runs |
| `0x80002300`–`0x80003000` | With a code build whose code list is elsewhere: veneers into code in MEM2 (the resident runtime's or the virtual SD card's) |
| Top of the game's MEM1 arena | Resident runtime code, then the pad blob, the screenshot blob and the crash blob below it |
| `0x90000000`–`0x90800000` | Left alone by the loader: an IOS reload stages its kernel here |
| `0x90800000`–`0x90809000` | The restart snapshot and handoff (`wii/restart.hpp`) |
| Bottom of the game's MEM2 arena | Resident runtime data (or the virtual SD card's code and state, about 49 KB), then the pad state, the screenshot state (a few KB: its picture is written straight from the frame buffer), then the crash blob's state (about 5 KB) |
| `0x933E0000` and up | IOS |

`wii/memlimits.cpp` keeps the loader's heap between the end of its own
image and `0x81200000`, and in MEM2 above `0x90800000`. In Dolphin it
fills the reload area with `0xDEADBEEF` at each reload, so a heap that
strays there fails in the emulator as it would on a Wii.

### The loader's size and the game's room

The loader's own program (`boot.dol`) is linked at `0x80A00000` and
grows upward from there, toward the apploader at `0x81200000`. The game
loads from `0x80004000` up to just below `0x80A00000`, so the loader
getting bigger never takes room from the game: the line between them is
fixed. What growth does take is the loader's own MEM1 heap, the space
between the end of its image and `0x81200000` (about 2.4 MiB at 2.2.3,
image end `0x80F8C560`, 4.7 MB file of which most is the menu font and
the packed runtime blobs). That heap holds a big pack's file table while
it is planned, so it is the thing to watch: `session.log` prints the
MEM1 figures at start and at each "Heap check".

A game whose DOL (or bss) reaches `0x80A00000` would overwrite the
loader while the apploader still runs. The loader checks every
apploader load against its own range and stops with "overlaps the
loader" instead of crashing; no game seen so far comes close (Brawl,
among the largest, ends at `0x805A5154`).

The in-game part (the resident runtime) is separate and small: 43 KB of
code at the top of the game's MEM1 arena (or in MEM2 for code builds
such as Project+), plus its data in MEM2. It is built with `-Os` for that
reason (`Makefile.runtime`).

## Restarts and crashes

RiftWii can start itself again without the Homebrew Channel
(`wii/restart.cpp`). `Makefile.wii` wraps crt0's `__CheckARGV`, which
runs before `.bss` is cleared, so every start copies the image's
writable data (`.ctors` to `.sdata`, about 20 KB) to `0x90800000`. A
restart shuts libogc down, copies that back and jumps to `__app_start`:
the program starts exactly as it did from the Homebrew Channel. A
handoff record next to the copy says why; the new start reloads IOS (so
no handle of the old run survives) and shows the reason on Home.

Two things restart:
- a launch that fails (A on the error screen, or two minutes untouched);
- a crash. `wii/crash.cpp` replaces libogc's panic function: it notes
  the registers, then resumes the crashed thread in a recovery function
  on its own stack with interrupts on. That function shows the report,
  writes it to the log and to `sd:/riftwii/crash.txt`, and restarts. A
  second crash within 30 seconds of a crash restart leaves to the
  Homebrew Channel instead of looping.

`addr2line -e riftwii.elf <address>` turns the report's PC, LR and stack
addresses into source lines (keep the `riftwii.elf` of each release).

## Files on the SD card

| Path | What |
| --- | --- |
| `sd:/apps/riftwii/` | The app (`boot.dol`, `meta.xml`, `icon.png`) |
| `sd:/riivolution/` | Mod packs: XML files and their folders |
| `usb:/riivolution/` | Mod packs on a FAT32 or NTFS USB drive: listed through the menu's mount (`usb_xml_names`), read at launch through d2x's `/dev/usb2` (`wii/umsdev.cpp`, an `ImageVolume`); table runs of kind `RT_KIND_USB`, and small NTFS files kept in their MFT record become MEM replacements (`CompiledMod::mem`). Code builds on USB are refused at Start (`usb_mod_folders`, `ModPlaceProblem`) |
| `sd:/riftwii/settings.txt` | Settings |
| `sd:/riftwii/menu_ios.txt` | The menu IOS slot |
| `sd:/riftwii/choices/<ID>.txt` | Per-game choices (packs, options, saves, cheats, picture) |
| `sd:/riftwii/choices/<ID>.video` | Which borders the game drew last time |
| `sd:/riftwii/cheats/<ID>.txt` | The game's cheat list |
| `sd:/riftwii/saves/<ID>/` | Saves kept on the card |
| `sd:/riftwii/history.txt` | Recently played |
| `sd:/riftwii/titles-<lang>.txt` | GameTDB's game names |
| `sd:/riftwii/lang/<lang>.po` | A translation that overrides the built-in one |
| `sd:/riftwii/riifs/` | Files copied from network packs |
| `sd:/riftwii/session.log`, `session-previous.log`, `boot.log` | The menu's log, the one of the start before, and the last launch's log |
| `sd:/riftwii/crash.txt` | RiftWii's last crash |
| `sd:/riftwii/gamecrash.txt` | The last game crash, from the crash blob's record |
| `sd:/riftwii/report.txt`, `report-state.txt` | The last problem report as sent, and which crash was already offered |
| `sd:/riftwii/sd.raw` (or `usb:/riftwii/sd.raw`) | The virtual SD card's image, for code builds inside it |
| `sd:/riftwii/codebuilds.txt` | Code builds picked by hand on the Mods page |
| `sd:/riftwii/covers/`, `screenshots/` | Cover art as textures; screenshots (menu and games) |
| `sd:/riftwii/rvz/` | RVZ group tables the runtime reads |
| `sd:/riftwii/update.txt` | The last update check |
| `sd:/riftwii/cardlog.bin`, `cardlog.txt` | The SD card's failures during the last game, and their text |
| `sd:/riftwii/autorun.txt`, `guiscript.txt` | Test scripts (see `DEVELOPING.md`) |

## Rules the code keeps

- Clean room: no Riivolution code, ever. What was used instead is listed
  in `NOTICE.md`.
- Nothing is hardcoded for a particular game or mod. Hooks are found by
  the structure of the SDK's code.
- The runtime has no globals, no string literals and no library calls;
  everything it needs is in the context the loader fills.
- Failures say what failed and why, on screen and in the log.
