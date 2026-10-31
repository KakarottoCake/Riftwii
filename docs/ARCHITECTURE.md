# How RiftWii works

How the code fits together, for anyone changing it; [MAP.md](MAP.md)
lists every file. RiftWii is a Homebrew Channel
app that starts a Wii game with Riivolution-format mod packs applied,
without changing the game's files. It runs in four phases; each section
below names the files that own it.

```
 Homebrew Channel
   └─ RiftWii menu (wii/)  ── reads drives, packs, choices
        └─ plan the launch (src/, wii/launch/modplan.cpp)
             └─ boot the game (wii/launch/boot.cpp)  ── IOS, apploader, patches
                  └─ in the game: resident runtime + pad hook (runtime/)
```

## 1. The menu

| Piece | Files |
| --- | --- |
| Screens (Home, game page, Mods, Cheats, Settings, adapter test) | `wii/menu/rift_menu.cpp` |
| Widgets: the tile grid and the row list | `wii/menu/gui_gamegrid.cpp`, `wii/menu/gui_flowlist.cpp` |
| Artwork, painted at start (no image files) | `wii/menu/skin.cpp`, `src/art/canvas.cpp` |
| Menu state: the picked game, its packs, the choices | `wii/menu/frontend.cpp`, `src/patching/launch.cpp` (`LaunchModel`) |
| Riivolution's choices file (`riivolution/config/<ID4>.xml`), read and written | `src/patching/riiconfig.cpp`, `wii/menu/frontend.cpp` |
| Game images on SD and USB (WBFS, split WBFS, ISO; FAT32 and NTFS), and USB drives formatted as WBFS | `wii/storage/usbcatalog.cpp`, `src/fs/usbgame.cpp`, `src/fs/fat32.cpp`, `src/fs/ntfs.cpp`, `src/fs/wbfspart.cpp` |
| Dragon Quest X on its WFS USB drive: the drive read with the Wii's WFS key, the game started by the system with IOS59 ([WFS.md](WFS.md)) | `src/fs/wfs.cpp`, `src/fs/wfstitle.cpp`, `wii/storage/wfsdrive.cpp`, `wii/main.cpp` |
| Cover art (GameTDB), stored as ready GX textures | `wii/menu/covers.cpp`, `src/art/coverart.cpp`, `wii/menu/gui_gamegrid.cpp` |
| Game names (GameTDB), cheats (GeckoCodes archive) | `wii/online/online.cpp`, `src/data/titles.cpp`, `src/data/cheats.cpp`, `src/net/http.cpp`, `wii/online/netsock.cpp` |
| Online servers (Wiimmfi, WiiLink WFC, AltWFC, custom) | `src/patching/wfcpatch.cpp`, `wii/launch/wfc.cpp`, `vendor-wwfc/` |
| Per-game fixes (Kirby's MetaFortress, NSMBW, Prince of Persia, RE4, Excite Truck, error #002, 480p) and the automatic cIOS choice, as USB Loader GX makes them | `src/patching/gxpatches.cpp`, `src/patching/gxkirby.inc`, `wii/launch/boot.cpp`, `wii/storage/usbcatalog.cpp` |
| Update check (GitHub over https: BearSSL, `Makefile.bearssl`) | `wii/online/online.cpp`, `src/net/update.cpp`, `wii/online/tls.cpp`, `wii/online/tlsroots.c` |
| Network packs (RiiFS) | `wii/online/netpacks.cpp`, `src/net/riifs.cpp`, `src/net/riifs_sync.cpp`, `docs/RIIFS.md` |
| Settings, play history, translations | `wii/core/loadersettings.cpp`, `src/data/settingsfile.cpp`, `src/data/playhistory.cpp`, `wii/core/i18n.cpp`, `tools/lang_source.py` |
| Menu IOS (IOS 58 or a d2x cIOS slot) | `wii/launch/menuios.cpp` |
| GameCube adapter in the menu (its controllers work the menu; Settings' test page) | `wii/controllers/gcadapter.cpp` (drives `runtime/rtgcad.c` with libogc's IPC, or on IOS 58 through libogc's USB handle), `vendor-libgui/source/input.cpp` |
| Memory limits: the heap never enters memory a launch overwrites | `wii/core/memlimits.cpp` |
| Launches another loader asks for (`--launch` arguments, no menu; `docs/HEADLESS.md`) | `wii/launch/headless.cpp`, `src/patching/launchargs.cpp` |
| Screenshots: the menu's, and importing the ones games left on the NAND as PNGs | `wii/menu/screenshot.cpp`, `src/data/shotfile.cpp`, `src/art/pngencode.cpp` (a PNG writer with its own deflate) |
| Problem reports: gathered, sent to paste.rs (dpaste.com when that fails), shown as a link and a QR code; a game's crash record imported at start | `wii/online/reportsend.cpp`, `src/data/problemreport.cpp`, `src/art/qrcode.cpp`, `src/data/gamefault.cpp`, `src/net/http.cpp` (the POST) |

Everything the menu decides is plain data (`LaunchModel`, the per-game
choices file) and host-tested; the screens only draw and read the pads.
Text goes through libwiigui's `GuiText`, which translates every string
it is given; `tools/lang_source.py` holds the table and checks that
every entry still appears in the sources.

## 2. Planning a launch

| Step | Files |
| --- | --- |
| Parse the XML (the whole documented format) | `src/patching/patch.cpp` (`parse_package`) |
| Resolve choices, params and placeholders for this disc | `src/patching/patch.cpp` (`plan_package`) |
| Turn file and folder patches into byte sources | `src/patching/apply.cpp`, `src/patching/overlay.cpp`, `src/patching/expand.cpp`, `src/patching/source.cpp` |
| Memory patches (plain, search, ocarina) | `src/patching/mempatch.cpp` |
| The redirect table the runtime walks | `src/patching/redirect.cpp`, `runtime/rtable.c` |
| Glue on the Wii: packs to boot options | `wii/launch/modplan.cpp` |

A selected feature the runtime cannot carry out makes planning fail with
the pack, option and file named, so a game never starts half patched.

## 3. Booting

`wii/launch/boot.cpp` does what the System Menu and an apploader would:

1. Pick the IOS (`wii/launch/ios_reload.cpp`). Launches that need the SD card or
   the GameCube adapter keep the running IOS and report the game's own
   to it; the rest reload to the IOS the game asks for.
2. Open the disc (`wii/launch/di.cpp`), or for an SD/USB image reload into a d2x
   cIOS and hand it the image's fragment list (`wii/storage/usbcatalog.cpp`,
   `activate_image_game`).
3. Run the game's apploader. Overrides replace what it loads on the way
   in: the rewritten FST (files that grew or were created move into a
   virtual window above the disc), the data header, a pack's `main.dol`.
4. Install the resident runtime (`wii/launch/resident.cpp`), the pad hook
   (`wii/hooks/padhook.cpp`), the screenshot hook (`wii/hooks/shothook.cpp`) and the
   game crash hook (`wii/hooks/faulthook.cpp`);
   apply memory patches, cheats (the Gecko code
   handler, `vendor-gecko/`), video patches (`src/patching/videopatch.cpp`: width,
   deflicker, borders, and a forced TV format that converts the game's
   render mode tables) and the game language (`src/patching/gamelang.cpp`); last,
   the online server (`wii/launch/wfc.cpp`), which may take memory below the
   MEM1 and MEM2 arena ends.
5. Write the low-memory globals and jump to the game.

## 4. Inside the game

**Resident runtime** (`runtime/resident/`). A position-independent blob:
the build links it at two addresses and refuses it if the bytes differ.
It hooks the game's `IOS_IoctlAsync`, found by structure
(`src/patching/symsearch.cpp`), not by per-game addresses, and answers disc reads
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
`sd:/riftwii/cardlog.txt` at the next start (`src/data/cardlog.cpp`).

**Pad hook** (`runtime/pad/`, `runtime/rtgcad.c`). With the GameCube
adapter on, a second blob hooks the game's `PADRead` and
`PADControlMotor` (found by structure too) and runs the WUP-028 driver
through the game's own asynchronous IPC. The adapter's controllers fill
ports with nothing plugged in; rumble goes back to them. PADRead tells
the video code which of its own ports are in use, so the Wii's screen
burn-in reduction leaves the screen lit; for the adapter's pads the
blob calls the same reset (found by the call near PADRead) on each
button or stick change.

The same blob gives Wii games the GameCube games' way back: L, R, Z and
Start held two seconds on a GameCube controller (the adapter's or the
console's own ports) leave through the game's own `OSReturnToMenu`, the
HOME Menu's Wii Menu button, which Return to RiftWii points at the
channel. The combo is checked in `PADRead`; the leave itself waits for
the main thread's next `OSSleepThread` (hooked: `PADRead` can run in the
SI sampling callback), and fires once, since `OSReturnToMenu`'s own NAND
calls sleep through there again. The main thread is the oldest live one
(`0x800000DC`, the head of the OS's thread list), not `0x800000D8`, which
names whichever thread last had its floating-point registers saved. N64
Virtual Console games never sleep their main thread, and the thread that
reads their pads waits each frame by suspending itself: half a second
after the ask, that thread's own `OSSuspendThread` (hooked too, found by
its one add-1 to a thread's suspend count) leaves under the same rules,
as would its `OSSleepThread`. Without `OSSuspendThread` only sleeps
leave. A hook on the SDK's IPC submit routine,
which every IOS request passes, notes save activity (a write, a NAND
file opened for writing, a `/dev/fs` change), and with saves on the SD
card the blob reads the resident runtime's last save write: the combo
waits until no save was written for a second (gaps inside a save
measured in Dolphin: up to 0.418 s in Super Smash Bros. Brawl). The
decisions are `runtime/rtleave.c`, host-tested. With any of the three
functions missing, or one of the two hooks impossible, the way back stays
off and boot.log says why; the adapter works as before.

**Packs that add GameCube controllers** (#352). A game that reads only
Wii Remotes has no `PADRead`, so the adapter has nothing to hook. A mod
pack can give it one, and this is the supported way to do it: the pack
adds a text (code) section to the game's main.dol with its own `PADRead`
(only text sections are searched), and turns the GameCube controller
into what the game reads (Super Sentai Battle:
Ranger Cross's pad pack hooks the game's `KPADReadEx` call and reports a
Wii Remote with a Nunchuk). RiftWii searches the main.dol that actually
starts, so the pack's own copy is searched like a game's. For the search
(`find_pad_symbols`, `src/patching/symsearch.cpp`) to take a function for
`PADRead`, it must be a framed function (`stwu r1`, then `mflr r0`) with
at least three of the SDK's "no controller in this port" paths, each
`stb rE,10(rS); mr r3,rS; li r4,0; li r5,10; bl memset`, all calling the
same function; the Ranger Cross pack writes one per port, four in all,
and calls a small memset of its own. RiftWii then hooks it and fills the
ports it marks missing from the adapter, as for any game. Two things a
pack does itself: there is no screen-dimming reset next to its
`PADRead` for RiftWii to call, so the pack clears the game's dimming
on real input itself (`VIResetRFIdle`); and without a `PADControlMotor`
there's no rumble. A pack that wants rumble adds its own
`PADControlMotor` in the same shape as the SDK's, which the search finds
by reading the rumble-off flag and then building the motor command with
`oris rA, rB, 0x40` (described in `include/riftwii/symsearch.hpp`). A
pack done this way also gets the GameCube
controller's way back (L, R, Z and Start held two seconds), since the
game's `OSReturnToMenu`, `OSSleepThread` and IPC submit routine are
found as in any game. `TestPackPadRead` (`tests/gcad_tests.cpp`) keeps a
routine of this shape found, so a later change to the search fails the
tests instead of quietly breaking these packs.

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

**Virtual SD card** (`runtime/vsd/`, `runtime/rtvsd.c`, `wii/hooks/vsdhook.cpp`).
When the code builds launched are inside `riftwii/sd.raw` (a FAT32 card
image on the SD card or the USB drive; `wii/hooks/vsdimage.cpp` lists them
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
on the USB drive only). The code and its state (a 32 KB bounce buffer for
USB, then the image's extent table, 12 bytes a piece of the image file on
its device, up to 16384 pieces: sized to the image, so three pieces cost 36
bytes and an image scattered by a well-used card up to 192 KB, found by a
binary search) go to the bottom of the MEM2 arena, staged at its top and
copied down at the jump; the game reaches them through veneers at
`0x80002300`, the code handler's list room a code build
leaves free (below the MEM1 arena's top without it). Not with the
resident runtime yet.

## 5. GameCube games

GameCube games run in **Wii mode**, with RiftWii's own code: no
Nintendont and nothing on the Starlet. IOS keeps running, and the game
reaches the SD card, the Wii Remotes and the way back through it.

**Listing.** `wii/launch/gclaunch.cpp` adds the `sd:/games/<folder>/game.iso`
images (or `game.rvz`, see [RVZ.md](RVZ.md#gamecube-games)) whose header says
GameCube to the SD card's games (`ImageGame::gamecube`), and the same in
`usb:/games/<folder>/` to the USB drive's (`ScanUsbGcImages`, through the
menu's view of the drive; the Wii games' images lie in `usb:/games`
itself).

**From the USB drive.** libogc's USB driver only works under IOS 58, and
a GameCube game keeps the IOS it starts on. So a game on the drive has
its file's pieces found through the menu's view first (`usb_file_pieces`),
then the launch reloads into a d2x cIOS (`activate_gamecube_usb` in
`wii/storage/usbcatalog.cpp`, in `image_cios_order`'s order) and opens
d2x's `/dev/usb2`, checking that the image's first sector reads back the
same through it. The plugin reads the image through the host's
`usb_read`; the blob reads `GC_RUN_USB` runs (and an RVZ's pieces, with
`extents_on_usb`) through the same fd, 32 KB at a time into the MEM2
bounce buffer, since d2x's USB driver writes nowhere else. A pack on the
drive is planned from the menu's view of it (`GcProvider`'s `usb:`
files become `UsbDevice` pieces, `RWP_GC_PIECE_USB` in the request, Usb
runs in the blob); with one, a game on the SD card is launched under d2x
too (`ready_usb`, checked against the pack's first file). The memory
cards, the RVZ's group table and the trace stay on the SD card.
The heartbeat reads the image's first sector when the drive has been
quiet for 30 s (`gc_usb_keep_alive`), so a hard drive does not spin down
under a game that has loaded what it needs. A patch-only
mod, `sd:/riftwii/patches/*.xdelta`, is listed as the game it makes when
one of those images is its original (`wii/launch/gcpatch.cpp`, the VCDIFF
reader `src/patching/vcdiff.cpp`). Starting it builds
`sd:/games/<ID>/game.iso` once, in the menu's Build box and progress
window (`PrepareGcGameInMenu` in `wii/menu/rift_menu.cpp`).

**Loading** (`wii/launch/gclaunch.cpp`, `wii/launch/gcplugin.cpp`, and the
`gamecube` plugin's `plugins/gamecube/gcnative.cpp`, loaded on first use;
[PLUGINS.md](PLUGINS.md)). The
loader reads the game's choices file into a `GcImage` (disc reads, memory
card, 480p, 16:9, language). The plain-C `rwp_gc_launch` carries
`real_card` separately from `shared_card`, with the twelve-byte physical
header ID verified by the DOL; `gc_request_check` rejects conflicting
choices. The loader plans its Riivolution packs as a window past
the image's end with a rewritten FST (`src/patching/gcoverlay.cpp`), and
reads the DOL. It finds the SDK functions it needs by their
instructions, not by per-game addresses: `DVDLow*`, `PADRead`,
`ARStartDMA`/`ARInit`, the EXI calls for memory card slot A,
`__OSUnhandledException`, and for the options `VIGetDTVStatus`,
`OSGetProgressiveMode`, `OSGetLanguage`, `VIConfigure` and
`GXSetProjection`. Each found function is branched to the blob; one not
found leaves its feature off, with a `boot.log` line. Then it places the
blob under the FST and its data buffers in MEM2 (see Memory), checks
that the game's own stack top and ArenaLo (found in `__init_registers`
and `OSInit`, `src/patching/gclayout.cpp`) stay under the arena's top,
opens `/dev/sdio/slot0` and `/dev/es`
(IOS keeps both open across the jump), writes low memory as a GameCube's
start-up leaves it, and jumps to the game.

**Games built with GCC and libogc** (Sonic Riders Tournament Edition, a
rebuild of the game; `runtime/gc/gc_rt.h`, "libogc"). Nintendo's
libraries are there, recompiled with GCC, next to libogc's OS, alarms and
disc code, so the searches above learn GCC's ways. A `lis`/`ori` pair
becomes the `lis`/`addi` that gives the same value before they run
(`ori_to_addi`), but only in a game built so. The register walkers also
follow `ori rA,rS` and `addis rX,rY,0xCC00`, and ignore a D-form whose
`rA` is 0. A function's start is the `stwu` after the previous `blr`,
as GCC puts early returns' epilogues mid-function.

libogc's disc code writes the DI's registers itself, many commands of
them. So the DI is answered at its registers: each store that starts a
command (DICR) jumps to a slot that calls `gc_on_di_start`. That carries
the command out from the card. At its end, it sets the stand-in's DISR
and runs the game's DI handler (its `IRQ_Request(21, …)`). The blob's
alarms go through `SYS_CreateAlarm`/`SYS_SetAlarm`. The FST goes below
the blob, as libogc may take its arena's top as the FST less `0x800`.
The memory card is the console's own through the Wii's EXI: the card on
the SD card needs libogc's EXI functions found, which is not done yet.

**Cheats.** The game page's Cheats work as for a Wii game (the same
file, download and code list). After the low-memory clear the loader puts
the Gecko code handler at `0x80001800`, the GameCube's debugger area, and
the code list after it up to `0x80003000`; the end of the game's
`__VIRetraceHandler` branches to it, found by its check of the second
display interrupt (`CodeHook::GcRetrace` in `riftwii/codehook.hpp`, the
Wii's pattern as a fallback). The hook is looked for before any other
patch, and put in only after all of them, if its `blr` is still there.
No hook or too many codes: the game starts without them, with a
`boot.log` line.

Japanese GameCube IDs select the Shift-JIS IPL font on NTSC through the
recognized `OSGetFontEncode` decisions and their inlined copies
(`src/patching/gcfont.cpp`), replacing only their VI DTV loads so a Western
console's missing Japanese strap does not select ANSI; cached encodings,
non-NTSC paths and other game regions keep their own behavior.

**The blob** (`runtime/gc/`, position independent, no heap or libc):
- `gc_rt.c`: disc reads from the image's SD sectors (and the packs'
  window), with polled IPC and the game's own `OSSetAlarm` for the
  callbacks, the SD card read from the alarm once `DVDLowRead` has
  returned, as a drive works while the game runs on; a GameCube drive's
  timing unless Disc reads is Fast; Luigi's Mansion's read buffers steered
  to the heap each load was queued from, and its callbacks steered only
  when they interrupt a temporary heap (see below); disc
  audio streaming mixed into the game's AI DMA; ARAM kept in MEM2 at
  `0x90000000` (a Wii's ARAM DMA moves nothing in Wii mode), with the
  game's ARAM addresses raised so its own sound program reads MEM2
  (except in games whose own checks need them as they are,
  `gc_keeps_aram_addresses`: Mario Party 4), and
  each `ARStartDMA` called with interrupts off returning only once its
  interrupt is pending (a Wii's
  instant transfers otherwise let ARQ's chained chunks finish in the
  middle of the game's own bookkeeping); the options; the trace; the way
  back; the crash screen.
- `gc_bt.c`: a Bluetooth driver for Wii Remotes, Nunchuks, Classic
  Controllers and Wii U Pro Controllers, filled into the pad ports. A Pro
  is told apart by its extension ID (`01 20`), asked for report mode 0x34
  and read in its own layout (`gc_pro.h`), through the Classic
  Controller's controls. `gc_ports.h` has the rule: a
  GameCube controller keeps its own port; each remote plays on its slot's
  port (its LED), or the next free one when a GameCube controller holds
  that, and the LED follows; a remote that reconnects gets its slot back
  by its Bluetooth address.
  A port where a GameCube controller is plugged in stays that
  controller's even while the game has switched it off: F-Zero GX's
  `PADSetAnalogMode` disables the port and its next `PADRead` says "no
  controller", which the game answers with a reset that brings the
  controller back; a Wii Remote filled in there kept the game from ever
  resetting it. The SDK's SI type table (found through `PADRead`'s
  `SIGetStatus`) identifies controllers, but real SI can replace a disabled
  port's type with NOREP too. The runtime remembers identified controllers
  through that interval. It releases a remembered port only when the
  original `PADRead` returns err -1 and type 8 (NOREP) for a port that was
  enabled before the read. `PADRead` disables an unplugged port itself,
  so a hook before it samples EnabledBits (found structurally relative to
  the game's r13). Ports never identified remain free for Wii Remotes;
  late identification reserves the port on the next read. An unplug while
  disabled remains reserved until an enabled read confirms absence. If the
  enabled-state layout is unsupported, the current-type rule remains.
  Some games ask `SIProbe` what is in a port and read only the ports it
  calls a controller: Mario Kart: Double Dash!!'s `JUTGamePad::read`
  skips a port unless `SIProbe` says a GameCube controller or a WaveBird.
  A Wii U has no GameCube ports and a Wii's empty port has nothing in it,
  so `SIProbe` says "no response" and the Wii Remote's port was ignored
  although its `PADStatus` had err 0. The loader hooks `SIProbe` (found as
  the small `SIDecodeType(SIGetType(chan))` wrapper whose first call is
  one of `PADRead`'s), and a port a remote or the adapter filled in the
  last read (the game's own err -1, err 0 after the fill) answers as a
  controller while nothing of its own answers there (`gc_ports_probe`).
  Pad trace records (`where=pad_si`, value 12) reuse ten crash-only words
  for the four full SI types, SISR, enabled bits before/after the read,
  original errors, the reserved and played port masks, and the `SIProbe`
  calls and the ones answered as a controller. Existing `pad_errs` records
  the errors after filling, even without Bluetooth. SISR is read without
  writing it or consuming SI input buffers; its latched NOREP bits describe
  past transfers and are diagnostic, not proof of current presence.
- `gc_gcad.c`: the GameCube controller adapter for Wii U, through the
  same driver as Wii games (`runtime/rtgcad.c`) on the `/dev/usb/hid`
  handle the menu opened before the hand-over (`TakeGcAdapterForGameCube`
  in `boot.cpp`, by Wii games' rules on the menu's IOS). Its requests go
  out polled from `PADRead`, and replies it did not wait for are routed
  between it and `gc_bt.c` (`gc_ipc_route`). Its state is in MEM2
  (placed only when the menu passed a handle). Its controllers take
  ports by `gc_ports.h`'s rule before the Wii Remotes, which then skip
  the ports they filled.
- `gc_card.c`: emulated memory card slot A, from an image in MEM2 at `0x91000000`
  written back to its file's sectors as the game saves. The SDK's EXI
  functions are found only for SD-image launches; **Real card in slot A**
  leaves every card EXI function unhooked, so channel 0/device 0 reaches
  the hardware. Slot B, SRAM/clock and the serial port pass through in
  either mode. The functions are found by `src/patching/gcexi.cpp`, through their shared
  control blocks and call relationships. `EXIGetID` identifies the bus
  selector when two routines set the same selected-state bit (Pokemon
  Channel). The clock/SRAM on channel 0 device 1, slot B and the serial
  port pass through to the game's original functions and the hardware;
  only channel 0 device 0 is answered by the card image.
- `gc_rvz.c` (the RVZ build, `riftwii_gc_rvz.bin`): the disc read from an
  RVZ, a group at a time ([RVZ.md](RVZ.md#gamecube-games)).
- `gc_entry.S`: the hooks' entry points.

Disc reads, stream top-ups and heartbeat storage work share a cooperative
worker with a 64 KiB MEM2 stack and a 64 KiB staging buffer. An IPC wait
switches back to the alarm's stack and returns normally; an alarm 100 us
later checks the same request again. Interrupts stay disabled inside each
turn, but the game's normal alarm epilogue services AI/DSP between turns.
No nested interrupt handlers or game thread APIs are needed. Replies for
Bluetooth and the USB adapter still go to their own request buffers.
Replacement commands invalidate the old read's generation; its DMA drains
into staging without publishing bytes or calling the old callback.

RVZ decoding uses the same stack. Optional checkpoints in Zstandard's
literal/sequence loops and its copies yield after about 400 us of CPU
work, plus the work to the next checkpoint. Only its GameCube compilation
reserves r31 for the worker pointer; wrappers save and restore that register,
and other decoder builds have no checkpoints. Card flushes snapshot at
most 8 KiB per job and retain dirty bits if the game accessed the card
while IOS wrote it. Further batches wait behind disc reads. The final
ES title launch remains a nonreturning polled wait, since it reloads IOS.
Reset and program switches first drain any outstanding DMA before dropping
the worker and the old OS's alarms.

**Luigi's Mansion's loader.** The game has one current heap for all its
threads. At the SD card's speed its loader thread can wake while another
thread has made a small, full heap current for a moment (a 512 KB solid
heap for a room's models), and its allocation then gets nothing. So each
load's read buffer comes from the heap that was current when the load was
queued. A load's callbacks are steered the same way only when they find
such a temporary heap current; when it is one of the game's two main
heaps or JKRHeap's system or root heap, they keep it, as the game would.
That matters because a callback can make something that outlives its
buffer: `/Game/game_usa.szp` is queued with the scene heap current, but
the game switches to the persistent heap straight after, and that is
where its callback should build the game's global archive. Built on the
scene heap instead, the archive was freed at the next scene change while
the game still used it, and Luigi's Mansion crashed to black after
choosing progressive scan.

`src/patching/gcload.cpp` finds the main-heap globals from the loader's group
function and the system/root globals from JKRHeap's constructor comparisons
and exchange setters, checked against the loader's current-heap global.
The blob reads their values at each callback, including after a restart.
Only the `gc_loads_use_queued_heap` game list enables this policy. If all
four globals cannot be identified unambiguously, callback hooks are left
out and the log says that only read buffers are protected. Neither game
addresses nor archive paths are used to select behavior.

**Clock speed** (`match_game_to_clocks` in `plugins/gamecube/gcnative.cpp`,
`src/patching/gcclock.cpp`). A GameCube runs its CPU at 486 MHz and its
bus (Flipper: memory, graphics) at 162 MHz. A Wii in Wii mode runs
Broadway at 729 MHz and Hollywood at 243 MHz, half as fast again. The
time base and decrementer count at a quarter of the bus clock: 40.5 MHz
on a GameCube, 60.75 MHz in Wii mode.

*Can RiftWii switch to a GameCube's speed?* No. Hollywood's HW_CLOCKS
register (`0x0d800190`) has a SPEED bit for 162/486 MHz, but WiiBrew's
[clock speed control](https://wiibrew.org/wiki/Broadway/Clock_Speed_Control)
page has the switch made from the Starlet with Broadway held stopped
([Hollywood registers](https://wiibrew.org/wiki/Hardware/Hollywood_Registers)).
RiftWii runs nothing on the Starlet, and AHBPROT only lets the PPC reach
the registers. Nintendo's own way, BC handing the console to MIOS,
leaves Wii mode: no IOS, so no SD card, Wii Remotes or way back. So
there is no Speed option; one would need RiftWii's own code on the
Starlet (an IOS exploit or a custom IOS module), which RiftWii does not
do.

*What RiftWii does instead: keeps the game's timers true.*
- The SDK reads the bus clock from `0x800000F8` (OSBusClock) and the CPU
  clock from `0x800000FC` (OSCoreClock). A GameCube's IPL writes 162 and
  486 MHz there, a Wii's 243 and 729 MHz (Dolphin's HLE boot writes the
  same pairs). The SDK turns time base ticks into time with them:
  `OSTicksToMilliseconds` and the like, `OSSetAlarm` periods, any wait on
  `OSGetTime`.
- When those words and the hardware disagree, every conversion is off by
  the same factor. Told 162 MHz on a 243 MHz bus, a game's alarms fire a
  third early and anything timed by ticks runs 1.5 times as fast:
  - timeouts and retries;
  - animation or logic stepped by elapsed time;
  - the pacing of sound and streaming.
- So the loader writes the Wii's clocks, the ones the console really
  runs at, and logs them as `GameCube: clock 729 MHz CPU and 243 MHz
  bus, time base 60.750 MHz, as the game is told (HW_CLOCKS ...)`.
  - It reads HW_CLOCKS (with hardware access) for that line only, to
    show the SPEED bit agrees.
  - It does not follow the register: nothing sets GameCube speed in Wii
    mode, and the blob's own waits (the drive timing in `gc_rt.c`) count
    the Wii's time base too. A register reading otherwise is reported as
    a misreading.
- A game could also build a GameCube clock into its own code instead of
  reading those words. The loader looks for `lis` and `ori`/`addi` pairs
  that build 162000000, 486000000 or 40500000 into one register and
  makes them build the running speed's values. Each one changed gets its
  own line in `boot.log`. This catches only a full Hz value built in
  place with immediates; folded constants (`ticks / 40500`, a multiply
  by a magic number) and floats or doubles loaded from data are not
  detected, so "clock values: none" in a log is not proof that the
  game's timing is right. Luigi's Mansion (PAL), Super Mario Sunshine
  and Super Mario Eclipse have none built in place (checked
  2026-10-18). A second, log-only scan looks through the DOL's data
  sections for big-endian floats and doubles of 162e6, 486e6 and
  40.5e6 and logs their section address; nothing is rewritten.

*What stays different from a GameCube.* With the timers true, game time
passes at the right rate, but work that is bound by the CPU, memory or
GPU finishes sooner:
- A game that drops frames on a GameCube drops fewer or none.
- One locked to the display's retrace keeps its frame rate.
- Loops that wait by counting instructions end early.

For speedruns that count lag, this is the remaining difference, and only
the clock switch above would remove it.

**Leaving and crashing.** Holding HOME (HOME and - on a Wii Remote
alone; L, R, Z and Start on a GameCube controller) for two seconds goes
where Settings > Return to says, as a Wii game's HOME Menu does: the
RiftWii channel first, then the Homebrew Channel if it started RiftWii and
the channel cannot be used, then the Wii Menu, through
`ES_LaunchTitle` with the ticket view the loader read. The memory card is
written back first. Normal gameplay does not write SD trace records;
`sd:/riftwii/gc/test/trace` enables them for diagnostics. PAD trace requests
are coalesced and written by the heartbeat. After Bluetooth start-up,
discovery with no connected remote also runs from the heartbeat, rather
than each PADRead; a new connection can take longer to be noticed, and
connected remotes keep frame-paced service.
A crash ends in `gc_on_fatal`: a record in
the trace, then the menu's own message (drawn by FreeTypeGX's
`RenderTextCoverage` before the launch) over the picture the VI shows, and
A goes back. The memory card's struct starts with two words of code that
end there too, so a jump into it (seen in Wind Waker and Metroid Prime)
is recorded with the jumper's LR and CTR instead of running its fields. At its next start the menu turns the trace's crash into
`gamecrash.txt` (`src/data/gccrash.cpp`) and offers a problem report.

**Reset.** A short press of the Wii's Reset button restarts the game, as
a GameCube's does (`src/patching/gcreset.cpp` finds the functions). The
game's own OSResetSystem runs its reset functions, stops the sound and
turns the picture off; where the SDK would then have the hardware reset
the console (PI's reset register) or reboot the disc through the
apploader, the blob puts MEM1 back as the loader left it at the jump,
from a copy kept in MEM2, and starts the game at its entry again
(`gc_on_hot_reset`). The memory card is written back first; the Wii
Remotes stay connected. The VI goes back on as the first start found
it, in the game's TV format and blanked: the SDK's hot reset switches it
off, which on a GameCube the IPL undoes, and a VIInit that finds it off
sets it up as NTSC (Doshin the Giant PAL then waited for a retrace that
never came). The graphics chip is reset too (PI's FIFO reset register,
as the menu's GX_AbortFrame does before the first start): a GameCube's
reset resets it, and Super Mario Sunshine, which parks it at FIFO
breakpoints every frame, slept in its first GXDrawDone after a restart
on a Wii. The blob also watches the button itself (PI's bit 16) at
each pad read: a press the game has not answered with its own
OSResetSystem within two seconds is answered by calling it for the game.
OSResetSystem asked for the start-up menu goes back to RiftWii instead.
The restart is used only for games it is known to work in
(`gc_restart_known_to_work`, `src/patching/gxpatches.cpp`: Super Mario
Sunshine USA and Super Mario Eclipse, by their exact IDs); in every other
game Reset, and the game's own OSResetSystem, go back to RiftWii as
holding HOME does. Cheats start again with the game: their handler and
list are in the debugger area (`0x80001800`–`0x80003000`), inside the
low memory that is put back, so keep them there. The test switch is
`sd:/riftwii/gc/test/reset`: two presses made by the blob.

**Two-disc games** (`runtime/gc/gc_disc.h`, issue #173). Home lists a
game's `disc2.iso` with its disc 1 (`gc_second_disc_of`); at the launch
`find_second_disc` (`wii/launch/gclaunch.cpp`) passes the folder's disc
2 of the same game ID to the plugin, which checks its header
(`src/disc/gcdiscs.cpp`), grows the FST's room at MEM1's top to the
larger of the two discs' FSTs (the SDK reads the new disc's FST there and
refuses one larger than 0x8000003C says), and puts disc 2's runs, its RVZ
reader (sharing disc 1's buffers) and the change's state in MEM2; with
packs, disc 1's FST is kept in MEM2 too, as disc 2's overwrites the one
in MEM1. The context gets one word, `disc_change`, in its alignment
padding. DVDLowStopMotor (libogc's command 0xE3) then opens the cover;
about two seconds later the blob's own alarm puts the other disc in
(the runs `ctx->runs` points to) and, once the SDK waits on the cover
(DICVR's mask), sets DICVR's interrupt bit and runs the game's DI
interrupt handler from the OS's table (0x80003094), so the SDK reads the
new disc's ID and FST as from a drive. A wrong disc gets another stop
of the motor, which changes back. Reset puts disc 1 back in. The trace
writes a `disc` record at each change. Without a disc 2 nothing of this
is set up and StopMotor is answered as before. The test switch is
`sd:/riftwii/gc/test/discchange` (with a disc 2 beside the game): the
blob stops the motor itself at the 1500th and 2200th pad reads.

**Physical memory cards** (`wii/storage/gcrealcard.cpp`,
`src/fs/gcreal.cpp`): the Saves page has read-only tools for either Wii
slot, including slot B with no card in A. libogc CARD mounts/unlocks the
card; RiftWii reads its flash with synchronous 512-byte EXI read commands.
`InitGcRealCards` reserves one 40 KiB, 32-byte-aligned MEM1 workarea before
the menu allocates its heap. Mount and raw reads share it: libogc's
`EXI_Dma` masks the pointer with `0x03FFFFE0`, so an ordinary MEM2 heap
allocation would DMA over unrelated MEM1 and leave its own buffer unread.
`gc_real_dma_range` rejects a misaligned, overflowing or non-MEM1 range
before any DMA starts. A missing early reservation fails safely.
Every transfer finishes before its aligned DMA buffer is reused, including
on removal. Sizes are bounded to ordinary 4–128 Mbit cards with 8 KiB
sectors. No physical-card write, format or repair operation is called.
The detach callback stays sticky for the whole session. The five system
blocks are read again after the confirmation and SD staging; removal or a
changed header/directory/BAT cancels before the target image changes.
Only an owned mount is unmounted. An existing EXI attachment or transfer
is refused; close restores the previous EXI callback and leaves other
devices' locks alone. If SRAM holds channel A at close, detachment waits
for its unlock callback. The workarea stays allocated until process exit,
including after removal or a failed mount, so callbacks cannot outlive it.

libogc 3.1.0's mount validates directory/BAT checksums and BAT free counts;
it does not compare the formatted header's flash ID with the console's.
A card formatted on another console is accepted. A locked physical card
still needs libogc's DSP-assisted unlock before normal flash reads. An
already-unlocked card skips that exchange and checks only SRAM's cached
flash-ID byte sum. RiftWii validates the header and layout with `gccard.cpp`
and does not write or repair the physical card. Mounting a card is not passive:
libogc clears its status, unlocks it when necessary, and records its flash
ID in that slot's SRAM cache, including slot B. `Open` logs a line when that
SRAM ID changes. None of these operations writes the card's flash. A copy
asks `gc_real_have_room` for the card's copy, the SD image and the plan's
working image before reading (the DOL has no exceptions, so a failed vector
growth would abort the menu), and `Read` reports progress after each 512-byte
sector for the menu's bar.

`gc_real_plan` selects the exact game code plus maker for this game's own
image, or every save for the source encoding's shared image. It exports
64-byte entries plus save blocks as GCI data and uses `gc_import_one`:
collisions stay untouched, bad chains are counted, and block/directory
limits stop further additions. Encoding mismatches are refused. The menu
previews selected/added/skipped/unusable counts and the destination, backs
up an existing target through `gc_backup_card`, then stages a complete new
image. `gc_real_commit` checks the source again before renaming the old
image to `.old` and the staged image into place; existing `.old` recovery
rules apply. A new image can be created before its first game launch.
Whole-card backups preserve every flash byte, including unused blocks, in
`sd:/riftwii/gc/backups/real/<date>_<time>.raw`; these explicit backups are
not pruned automatically. Unformatted/unmountable cards cannot be dumped.

With **Real card in slot A**, the menu checks slot A before launching and
offers the game's own SD card if it cannot mount/validate the real card or
its encoding differs. Accepting changes the Memory card choice to its own
SD image; Cancel stays in the menu. The launch checks again (also for
headless starts), passes the physical header ID in the request, and stops
on failure. The plugin restores that ID and its checksum to SRAM and
requires the write to finish. This handles an already-unlocked real card
after an emulated-card launch left another ID in SRAM; the physical
header/serial never changes. A locked card's normal SDK unlock still runs.
No SD image is loaded, backed up, formatted, or hooked for this mode.
vWii skips the real choice when cycling options and dims the physical-copy
tools with an explanation; a persisted real choice offers the SD fallback.
The Saves page explains that real-card saves are outside the SD image.

**Switching programs.** A program the game starts after itself would run
with none of the loader's patches, so every such start goes back through
RiftWii, which starts the image again with that program. The loader
makes `sd:/riftwii/gc/exec.txt` at each launch (one sector: `--idle--`,
the image's path and ID); the blob turns it into a headless launch
([HEADLESS.md](HEADLESS.md)) and leaves as holding HOME does, and
RiftWii's next start reads it once. Two kinds of start are caught:
- the SDK's OSExec (Sonic Gems Collection), which ends in
  `__OSBootDolSimple` (`find_gc_boot_dol`, hooked as `gc_on_boot_dol`):
  `gc_dol=`, `gc_restart=`, `gc_arg=`, and the next start boots that DOL
  with its arguments in BI2 and `OSExecParams` at `0x800030F0`;
- a bonus disc's menu (the Zelda Ocarina of Time / Master Quest disc
  D43E01, the Wind Waker demo disc PZLE01) starting a game packed in a
  TGC image on its disc, with a loader of its own. The loader finds the
  disc's `.tgc` files in its FST and their headers (`src/disc/gctgc.cpp`,
  up to eight), and gives the blob where each TGC and its program lie on
  the disc. The menu's read of a TGC's header, or its first read of a
  TGC's program (`gc_on_tgc_read`, rule in `runtime/gc/gc_tgc.h`, which
  leaves before the read is served) writes `gc_tgc=<the TGC file's
  offset>`. The header counts because the Collector's Edition menu reads
  the program with a loader of its own (`execD.img`) that drives the
  disc interface itself, past the blob's DVDLowRead hook. The next start
  takes the game from the TGC instead of the disc: its packed boot.bin
  and BI2, its DOL, and its FST with every file's offset moved onto the
  outer disc (the FST counts from the header's virtual file-area start;
  the file area's real start and the TGC's own offset are added), so the
  blob serves its reads from the image as it is. DVDLowReadDiskID reads
  the packed boot.bin's ID (`disc_id_offset`). The offset in exec.txt
  must be the start of a file of the disc's own FST, and every size and
  offset the TGC's header and FST give must lie inside that file, or the
  disc starts as itself (its menu), the reason in the log. Packs are left off for such a game, and
  its own OSExec switches run as made: exec.txt cannot name a program
  inside a TGC.

Only for an image on the SD card, with a way back. If IOS refuses to
leave, exec.txt goes back to `--idle--` and the start runs as the game
made it.

**Programs the game loads itself.** EA's James Bond games (007:
NightFire, Agent Under Fire, Everything or Nothing) start a small loader
as the disc's program. It reads the game itself from an ELF file on the
disc with its own code and jumps to it; that program later loads a second
copy of the loader, which loads the driving missions' ELF, and back.
Each is a whole program with its own SDK, so none of the loader's patches
would reach them, and the game stopped on a black screen. These switches
do not go back through RiftWii: the game's own state passes from one
program to the next in memory. Instead, as Nintendont patches a program
as the game reads it:
- the loader finds the disc's `.elf` files (`find_gc_fst_files_ending`,
  up to eight), reads each one's segments (`src/patching/gcprograms.cpp`,
  host-tested) and runs the same patches over it as over the disc's own
  program (`patch_program` in `plugins/gamecube/gcnative.cpp`), from
  RiftWii's part of MEM1 as it was before any program's patches;
- what a program's patches change in its code and data becomes a list of
  words by disc offset (`struct gc_programs` in MEM2, built by the
  loader). The blob writes them into every read of the disc that brings
  those bytes, piece by piece (`gc_programs_patch` in
  `runtime/gc/gc_programs.h`, from `gc_read_step`);
- a program's first two words become `li r0,index` and a branch to
  `gc_hook_program`. There `gc_on_program_start` drains the old
  program's I/O, writes the card back, sets RiftWii's words in MEM1 to
  that program's (the hooks' replay slots, the context's function
  addresses, the ARAM stubs: whatever any program's patches changed in
  them, the disc's own included; the context's flags excepted, which every
  program gets all of, and the memory card, which stays on for every
  program when any hooks it; the instruction cache is invalidated for
  each),
  forgets the old program's drive command, alarms, music and card
  transfer (`gc_forget_game`, as Reset does) and runs the program's two
  words from `gc_replay_program`. It runs on a stack of its own in MEM2,
  the registers kept in `gc_program_saved`.

Reset's restart and the switches through exec.txt stay the disc's own
program's. Not with packs (they move the disc's files), a game from a TGC
image, or a second disc (the patches are for disc 1's offsets).

**Memory cards** (`src/fs/gccard.cpp`): one image per game,
`sd:/riftwii/gc/cards/<ID>.raw`, or a shared `shared.raw` (`shared-jp.raw`
for Japanese games), formatted the first time. The game page's Saves tile
opens it: saves with their icons, copied out as `.gci`, deleted, or added
from `sd:/riftwii/gc/saves`. Before each start (Settings' Back up saves,
on by default) the loader copies the card the game will use to
`sd:/riftwii/gc/backups/<card>/<date>_<time>.raw` (`src/fs/gcbackup.cpp`),
`<card>` being the card file's name (the game's ID, or `shared`), keeping
the newest five and skipping the copy while the card matches the newest.
The Saves page's Restore an older save lists them by date and puts the one
picked back over the card, after a backup of the card as it is now, so a
restore can be undone the same way: that copy's name ends in `-undo`, and
the list marks it "(before you restored)". Each row's second line, how
many saves and blocks the copy holds, comes from its directory alone
(`gc_card_space`). Before mounting an existing card, `gc_card_flash_id`
validates the header's stored checksum pair and formatted fields, then
recovers its flash ID by subtracting the format-time sequence from its
serial. The host stores that ID and its complemented byte-sum checksum
in slot A's SRAM, as a GameCube's mount does; the card image and its
serial stay byte exact, preserving saves bound to that identity. Invalid
headers stay unchanged for the game to report as damaged. The host checks
both SRAM fields and changes only those 13 bytes when needed. This runs
while libogc's EXI and interrupts still work, before `shut_down_for_game`;
the host waits for a deferred RTC write before returning success. A
commit failure is logged and the launch goes on, so the game may report
the card as damaged. The game's own SDK later reads the RTC
over EXI, independently of libogc's cached SRAM. A host without the setter
logs that limitation and continues. A newly formatted card copies SRAM's
ID into its header, so it takes the same path and only a bad SRAM
checksum gets repaired. A later newly formatted card reads SRAM's current
ID, including this change, consistently.

## Memory

| Where | What |
| --- | --- |
| `0x80000000`–`0x80003400` | Low-memory globals; the Gecko code handler at `0x80001800` and its code list to `0x80003000` (in Wii and GameCube games); before a homebrew app starts, the return stub at `0x80001800`–`0x80002400` (`include/riftwii/returnstub.h`) |
| `0x80A00000`–`0x81200000` | The RiftWii loader (link address in `Makefile.wii`) and its heap |
| Under `0x80900000`, and `0x80900000`–about `0x80940000` (start-up only) | The packed `boot.dol`: its table and Zstandard frames, then the stub with its workspace and stack (`runtime/dolpack/`, `tools/pack_dol.py`). The stub unpacks every section to its address above, clears the BSS, moves the command line to where a loader would have put it for the unpacked DOL, and jumps to crt0; after that it is free memory |
| `0x81200000` | The game's apploader, while it runs |
| `0x80002300`–`0x80003000` | With a code build whose code list is elsewhere: veneers into code in MEM2 (the resident runtime's or the virtual SD card's) |
| `0x80002FE0`–`0x80003000` | Return to RiftWii's stub for code builds and games that clear MEM1's top (RE4, older LEGO games, Ghostbusters). Below the OS globals and past every veneer; an ordinary cheat list ends before it. Other games keep the stub above their MEM1 arena, away from mod loaders' low-memory code |
| Top of the game's MEM1 arena | Resident runtime code, then the pad blob, the screenshot blob, the crash blob and the Wii Remote log's blob below it. A WiiWare or Virtual Console game has no FST, so its arena runs to `0x81800000`: the blobs are carved from there, never below the end of the loader's heap or of the game's program (`channel_extras_floor`) |
| `0x90000000`–`0x90800000` | Left alone by the loader: an IOS reload stages its kernel here |
| `0x90800000`–`0x90809000` | The restart snapshot and handoff (`wii/core/restart.hpp`) |
| `0x90809000`–about `0x908ED000` | The start-up stash's copy: translations and GPL, zstd; no font data (`wii/core/stash.hpp`, [MEMORY.md](MEMORY.md)); arena 2 starts above it |
| From the end of `.bss` (start-up only) | The start-up stash as `boot.dol` loads it, until crt0 has copied it to MEM2; then the MEM1 heap |
| Bottom of the game's MEM2 arena | Resident runtime data (or the virtual SD card's code and state, about 49 KB plus 12 bytes for each piece of the image on its card), then the pad state, the screenshot state (a few KB: its picture is written straight from the frame buffer), then the crash blob's state (about 5 KB), then the Reset's request block (about 1.5 KB) |
| `0x90000000`–`0x91000000` | A GameCube game's ARAM (in a GameCube game only) |
| `0x91000000`–`0x91200000` | A GameCube game's memory card image |
| `0x91200000`–`0x91300000` | A GameCube game's data buffers: the SD bounce buffer, the I/O worker's stack and staging buffer, the disc audio ring, the trace record, the Wii Remotes' state, the crash screen (its card and the strip of glyphs the register dump is made from) and the disc runs (and an RVZ's reader) |
| `0x91300000`–`0x91500000` | A GameCube game from an RVZ: Zstandard's workspace, a group as stored, and two decoded groups (about 430 KB with 128 KiB chunks) |
| `0x91500000`–`0x92D00000` | A GameCube game's restart (Reset): the table of what MEM1 gets back, then MEM1's pieces as they were at the jump (the game's sections, BI2, the FST, low memory: about 4 MB for Super Mario Eclipse), and the 4 KB stack the blob puts them back on at the top |
| Under a GameCube game's FST | The GameCube blob and its context, the code moved from IOS's counter, the ARAM stubs, the memory card's struct, the way back's block and the DI registers' stand-in (an address ending in `0x6000`), then BI2's copy (`plugins/gamecube/gcnative.cpp`); the game's arena ends below them. Mario Golf and Mario Power Tennis put their stack and ArenaLo at `0x817dc000`, so this stays small |
| The top 384 KiB of the loader's MEM2 (below `0x933E0000` under IOS58), once a plugin is used | The plugin arena: the loaded plugins' images, `.bss` and state (`wii/core/plugins.cpp`, [PLUGINS.md](PLUGINS.md)); arena 2's high end is lowered under it, and given back when the plugins shut down at the menu's close. A GameCube launch loads the `gamecube` plugin there after that, and its code runs from there to the jump, above everything the launch writes in MEM2 (it checks) |
| `0x933E0000` and up | IOS |

`wii/core/memlimits.cpp` keeps the loader's heap between the end of its own
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
MEM1 figures at start and at each "Heap check". The translations and
GPL use a start-up stash above BSS that crt0 copies to MEM2; their MEM1
becomes heap. No font data is embedded: M PLUS 1 SemiBold is read in full
from `font.ttf` beside `boot.dol` into MEM2 before the GUI starts. The
copy stays readable across unmounts and updates, including new glyphs;
Korean's separate file is retained the same way on first use. A missing
or damaged card font falls back to the Wii Menu's NAND font, then to
libogc's readable recovery console if neither face is available ([MEMORY.md](MEMORY.md), with a breakdown of
what takes MEM1).

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
(`wii/core/restart.cpp`). `Makefile.wii` wraps crt0's `__CheckARGV`, which
runs before `.bss` is cleared, so every start copies the image's
writable data (`.ctors` to `.sdata`, about 20 KB) to `0x90800000`. A
restart shuts libogc down, copies that back and jumps to `__app_start`:
the program starts exactly as it did from the Homebrew Channel. A
handoff record next to the copy says why; the new start reloads IOS (so
no handle of the old run survives) and shows the reason on Home.

**A Wii game's own Reset.** The HOME Menu's Reset (and the console's
Reset button) has the game restart itself: its OSRestart shuts the
devices down and, for a disc game, reads the disc's apploader again,
which loads main.dol and the file table over the running game, then jumps
to the entry point. RiftWii is not there to load it, so every change of
the launch would be gone: the runtime's hooks (a pack's files), the
memory patches, the cheats' hook. Putting each of them back from inside
the restart means standing in for pieces of the SDK (its IOS reload, the
apploader), and a game on a USB drive or the card depends on d2x
surviving the reload, which only a Wii can show. So the restart goes
through RiftWii instead, by the paths that already exist, the same shape
as the GameCube program switch (`gc_on_boot_dol`).

At the launch the loader makes `sd:/riftwii/restart.txt`
(`prepare_restart_request` in `wii/launch/boot.cpp`): one sector holding
the headless launch (`docs/HEADLESS.md`, `headless_launch_arguments` in
`src/patching/launchargs.cpp`) that starts this game again, built by the
menu from what it launched with (`RestartArguments` in
`wii/menu/gameextras.cpp`), with `--idle--` first. After the IOS reload it
reads the RiftWii channel's ticket view, finds OSRestart
(`find_os_restart`, `src/patching/osrestart.cpp`) and gives the runtime
the sector's place on the card, a copy of the sector with `--launch` first
and the title and view (`struct rt_restart_block` in `rt_hook.h`, at the
bottom of the MEM2 arena with the runtime's data). The runtime hooks
OSRestart (`rt_reset_os_restart` in `rt_entry.S`, with a replay slot for
the displaced word as the IPC hooks have). When the game calls it
(`rt_reset_on_os_restart`, `rt_hook.c`), the runtime opens `/dev/es` through
the game's synchronous IOS_Open, then writes the sector
(a CMD25 on `/dev/sdio/slot0`, or d2x's sector write, through the game's
own synchronous IOS_Ioctlv, built as the savegame engine builds a write),
waits 50 ms for the card, and launches the channel through ES. A refused
open leaves the request idle; a refused write or launch closes the handle
before the game's own restart continues. No extra ES handle is held during
play: ES has only three contexts, and mods such as Retro Rewind open ES
themselves to sign an authentication salt. When Return to RiftWii is
patched into the game's `__OSLaunchMenu` for the same channel, the
runtime opens nothing: it writes the sector, waits, and leaves through the
game's own OSReturnToMenu (found by `find_os_return_to_menu`; the loader
sets `rt_restart.return_to_menu` after `apply_return_to`), with interrupts
on, as the HOME Menu's Wii Menu button does. The game then shuts its
devices down (the disc, Bluetooth, USB, the NAND) before the channel
starts; a launch straight from OSRestart with all of them running hung on
a Wii U with an SD game under d2x (a black screen, the request armed),
while the Wii Menu button reached the channel from the same games. The launch does
not return when IOS takes it; RiftWii's next start reads `restart.txt`
once (`HeadlessArguments`, `wii/launch/headless.cpp`) and starts the game
again. When the write or the launch is refused the sector goes back to
`--idle--` and the function runs from the replay slot, the game's own
restart. Not hooked, with the reason in `boot.log`: no channel installed,
a game that checks its own code, a runtime placed in MEM2, no card handle,
a launch that does not fit a sector. `debug_off = reset` turns it off.

Two things restart:
- a launch that fails (A on the error screen, or two minutes untouched);
- a crash. `wii/core/crash.cpp` replaces libogc's panic function: it notes
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
| `usb:/riivolution/` | Mod packs on a FAT32 or NTFS USB drive: listed through the menu's mount (`usb_xml_names`), read at launch through d2x's `/dev/usb2` (`wii/storage/umsdev.cpp`, an `ImageVolume`); table runs of kind `RT_KIND_USB`, and small NTFS files kept in their MFT record become MEM replacements (`CompiledMod::mem`). Code builds on USB are refused at Start (`usb_mod_folders`, `ModPlaceProblem`) |
| `sd:/riftwii/settings.txt` | Settings |
| `sd:/riftwii/menu_ios.txt` | The menu IOS slot |
| `sd:/riftwii/choices/<ID>.txt` | Per-game choices (packs, options, saves, cheats, picture) |
| `sd:/riftwii/choices/<ID>.video` | Which borders the game drew last time |
| `sd:/riftwii/cheats/<ID>.txt` | The game's cheat list |
| `sd:/riftwii/cheats/gamehacking/` | GameHacking.org's Wii zip and `index.txt` (which entry holds which game), the fallback when rc24 has no cheats for a game; replaced when over 30 days old and a game asks |
| `sd:/riftwii/saves/<ID>/` | Saves kept on the card |
| `sd:/riftwii/history.txt` | Recently played |
| `sd:/riftwii/order.txt` | Home's own order, one game a line as `drive:ID` (`src/data/homeorder.cpp`) |
| `sd:/riftwii/case_colors.txt` | The player's own case colors for the Home shelf, one a line as `ID rrggbb`; a full six-character ID is one game, a three to five character ID covers every game it starts (`src/art/casecolour.cpp`) |
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
| `sd:/games/<ID>/game.iso` | A GameCube game |
| `sd:/riftwii/patches/*.xdelta` | Patch-only GameCube mods, built into `sd:/games/<ID>/` once |
| `sd:/riftwii/gc/cards/<ID>.raw`, `shared.raw`, `shared-jp.raw` | GameCube memory cards |
| `sd:/riftwii/gc/saves/*.gci` | GameCube saves copied out, or to add to a card |
| `sd:/riftwii/gc/backups/real/` | Explicit whole physical-card `.raw` backups, not pruned |
| `sd:/riftwii/gc/dolphin/` | Dolphin's `GC` folder, copied whole (`USA/Card A/*.gci`, `MemoryCardA.USA.raw`...), for Bring your Dolphin saves (`gcimport.hpp`) |
| `sd:/saves/`, `usb:/saves/` | Nintendont's memory cards (`<ID4>.raw`, `ninmem.raw`, `ninmemj.raw`, slot B names), read and never written, for Bring your Nintendont saves (`gc_nintendont_scan`, `gcimport.hpp`); a card is read into a MEM2 buffer, one at a time |
| `sd:/riftwii/gc/trace.bin`, `trace-crash.bin` | The last GameCube launch's trace (`tools/gc_trace.py`), and the last one with a crash, once offered |
| `sd:/riftwii/gc/controls_seen.txt` | The controls card was shown |
| `sd:/riftwii/gc/exec.txt` | A GameCube game's start of another program (OSExec, or a bonus disc's menu starting a game from a TGC image): `--idle--` until then, a headless launch after, read once by RiftWii's next start (see "Switching programs") |
| `sd:/riftwii/gc/test/press`, `leave`, `crash`, `crashblack`, `streamonly`, `nowiden`, `keeparam`, `fakeremote`, `fakepro`, `rumbletest`, `novihook`, `nomotor`, `handover480p`, `handover480i`, `avekeep`, `reset`, `exec`, `discchange` | Test switches, in a folder of their own: presses on port 1, holding the way back, a crash on purpose (and on a blanked screen), the disc music alone, 16:9's hook without the widening, the ARAM addresses as the SDK gives them, a made-up Wii Remote through the controls map instead of the Bluetooth (its results in the trace's `projection_callers` words, which that run does not need; `fakepro`: a made-up Wii U Pro Controller in its place), the game's own PADControlMotor called for the test remote's port every 120 pad reads, rumble then stop (with `fakeremote`: the trace's `bt_rumble` word counts the calls and the remote's motor switches), VIConfigure left as the game made it (480p only through the game's own mode), PADControlMotor not hooked, an NTSC game handed over with the VI and video encoder already in NTSC 480p even without 480p (a game given 480p is handed over that way anyway), one given 480p handed over in 480i as earlier builds did, the encoder left as the menu set it rather than in the game's region, two Reset presses (at the 1500th and 2200th pad reads; the first through the OS's own restart, reset 0), and a switch to another program (gc_on_boot_dol) at the 1500th pad read in a game without one: to the disc's own program with the argument `rwtest`, through `sd:/riftwii/gc/exec.txt` and the way back, and with a disc 2 beside the game the motor stopped at the 1500th and 2200th pad reads (a disc change and back, `runtime/gc/gc_disc.h`) |
| `sd:/riftwii/autorun.txt`, `guiscript.txt` | Test scripts (see `DEVELOPING.md`); autorun only works in a test build (`make -f Makefile.wii TEST=1`) |

## Rules the code keeps

- Clean room: no Riivolution code, ever. What was used instead is listed
  in `NOTICE.md`.
- Nothing is hardcoded for a particular game or mod. Hooks are found by
  the structure of the SDK's code.
- The runtime has no globals, no string literals and no library calls;
  everything it needs is in the context the loader fills.
- Failures say what failed and why, on screen and in the log.
