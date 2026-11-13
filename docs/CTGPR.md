# CTGP-R 1.03 from RiftWii

RiftWii has two ways to start CTGP-R 1.03's pack:

* **The native launch, offline only** (below, "The native launch"):
  RiftWii loads Mario Kart Wii and places CTGP-R's code in it itself,
  with no channel app. Used only with `ctgpnative.txt` next to `boot.dol`
  and its files set up on the card (autorun test builds: `ctgpnative`).
* **The CTGP-R Channel as a Homebrew Channel app** (the rest of this
  page): the default, from a real disc and from a USB or SD image through
  d2x's virtual disc. The native launch is used instead only when a file
  `ctgpnative.txt` sits next to RiftWii's `boot.dol` and its files are on
  the card.

## Installing updates from a scrubbed game image (2026-11-12)

The reported failure is a brief **"Install Error! Please try again."**,
followed by another installation attempt at 0%, with one update remaining.
The console report ends at the handoff to CTGP-R: RiftWii does not keep
running inside the channel and cannot log the installer's return value.

Two isolated tests used the player's CTGP-R v1.03.1190 All Inclusive pack,
the PAL Mario Kart Wii WBFS as Dolphin's inserted disc, and the
[`es-identify-1` release of RiftWii's Felk fork](https://github.com/RiftWii/dolphin-felk-riftwii/releases/tag/es-identify-1):

- CTGP-R's original `apps/ctgpr/boot.dol` booted directly.
- The same app started through RiftWii's CTGP-R XML on upstream commit
  `2d54654b42ae65f3e73bb8c0427921c78058fca0`.

Each had its own 2 GiB FAT32 image and Dolphin user directory. Both used
`disable_ios_exploit = yes`, `disable_patches = yes`, and
`compatible_mode = yes`. Cached Interpreter, MMU and accurate CPU cache
were enabled. The RiftWii run used the unpacked test DOL: the packed DOL
did not reach its framebuffer in this emulator configuration. Folder sync
was off during installation so writes stayed in each raw image.

Earlier updates installed, but the last pending update retried at 0%.
Frame capture of the direct boot also caught the same brief install-error
message. Its remaining `ctgpr/update.dif` was 12,731,484 bytes, with DIA0
package ID `4354475052000000` (CTGPR) and version `01031193` in its header.
The saved image had 1,119,748,096 free bytes, and `blob.bin` had grown from
999,030,784 to 1,001,062,400 bytes. This rules out an entirely unwritable
or full card in that control; it does not authenticate the pending update.

The failing operation was opening an original game file for the delta
patch: `yaz0:dvd:/data_1/files/Race/Course/old_mario_gc.szs`. The installer
reported status 4 after a source-open result of -9; its file error was
ENOENT (2). The disc filesystem had mounted **data_0**, not data_1. The
supplied WBFS's partition table listed the game first and the channel
installer second: the removed update partition had changed the game's
index. Earlier updates did not need that original disc file.

Putting the game back in partition slot 1, with the same offsets and
contents, let the previously stuck 1.03.1193 update finish. The original
channel then installed the later updates and reached "The main
installation has completed!" The separate RiftWii run reached the same
completion message after installing all offered updates. No channel code, signature check,
update payload or original game file was changed.

For the CTGP-R channel's image launch, RiftWii now supplies a separate
512-byte partition-table sector through d2x's fragment list. A game
already in slot 1 needs no change. Otherwise its entry exchanges places
with slot 1; a game-only table gets a second entry naming the same game
partition. The rest of the disc's mappings, and the original image, stay
unchanged. The helper is generic; only the CTGP-R channel launch requests
this layout.

The replacement is `riftwii/channel-partition.bin` on the **same device**
as the image, because a d2x fragment list selects one device. For USB,
RiftWii uses a FAT32 partition of that USB drive, with an exclusive
temporary mount; the image itself may be on another partition. For SD it
uses the SD card. The file is closed and found again through a fresh raw
FAT view, and its sector is read back before it is mapped. If a USB drive
needs this correction but has no FAT32 partition for the header, the
launch stops with the reason in boot.log. An already correct table needs
no writable cache partition. USB DVD drives with 2048-byte sectors are
rejected before any header read: this overlay requires 512-byte sectors.
Layout preparation must succeed before CTGP-R's settings are written.

Host tests check table bounds, unchanged bytes, the game-only alias,
idempotence, fragment splitting, holes, coalescing and physical overflow.
The test-only autorun command `test_channel_layout` exercises the real SD
writer and fragment mapping with synthetic one- and two-partition tables,
checking the replacement sector and the untouched source. Dolphin's CTGP
test uses the equivalent sector materialized in a private WBFS copy:
Dolphin exposes the WBFS as an inserted disc and cannot run d2x's USB/SD
fragment backend. The Wii-specific cIOS path still needs a hardware run;
the console report used IOS248, base IOS38. Keep that limitation separate
from the reproduced missing-path failure and successful corrected-image
installation.

After a CTGP-R channel launch, problem reports now include the free SD
space, sizes of the channel, blob, journal and pending update, and only
the pending update's fixed 32-byte header metadata. They flag a declared
size that differs from the file size. The field positions were checked
against the local update's bytes; no CTGP-R updater implementation is
included. Payloads, signatures, encrypted journal contents and pack assets
are not uploaded. Keep private test cards, console reports and captures
outside the repository.

## The native launch (offline only)

It keeps to these rules:

* **No multiplayer.** The game's network calls are stubbed (they return
  -76, as under the channel without network settings), LAN mode and UPnP
  are never set whatever `sd:/ctgpr/config.ini` says, and Settings >
  Online's server swap is skipped. Nothing in RiftWii turns online on
  (`src/ctgpr/ctgpapply.cpp`, `ctgpfields.cpp`, `wii/launch/boot.cpp`).
* **Anti-cheat intact.** No tamper branch is changed, and CTGP's scanner
  thread runs. Each signed digest list (`CTGPR.HSH`, `CORE.HSH`) has its DSA
  signature checked against CTGP-R's own public list-signing key before it is
  trusted (`src/ctgpr/ctgpdsa.cpp`, `ctgphash.cpp`). Like the channel, DSA
  takes only the first 20 raw header bytes; the package name and version are
  outside those bytes. A list is accepted, as the channel accepts it, when its
  own version label is the version PACKAGES.BIN names; CTGP-R re-signs both
  lists under the same version when it updates. Because the label is not
  signed, an older signed list could stand in for a newer one, and it would
  still only vouch for files CTGP-R signed. For CORE, the code the launch
  depends on (`BOOT.BIN`, `BOAT.BIN`, `BEAT.BIN`) is also pinned by its own
  SHA-1s, so no list can admit different core code; CORE's other files rest
  on the channel's rule alone. This matters here because
  the channel, which would otherwise verify the lists, never runs. Each list's
  per-file SHA-1 is then written into the game's open-file records, where
  CTGP's own code compares each signed file as it reads it. Apart from the
  My Stuff exception below, every file needs a digest regardless of list flags;
  duplicate game paths and conflicting digests for shared sources are refused.
  Only MYSTUFF package records with flag 2 clear and a source below
  `sda:/ctgpr/my stuff/` may be unsigned. That directory/device check ignores
  ASCII letter case and refuses parent (`..`) components, backslashes and
  other devices. Every other resolved file must have a signed digest.
  The checked release's HASH directory has only CORE.HSH and CTGPR.HSH.
  CORE signs the music/font FST lists (`debug/fsnm.bin`, `debug/fsfont*.bin`),
  but neither list authenticates their replacement payloads. `no_music` and
  non-default `font` therefore keep the defaults, with a reason in `boot.log`.
  The directly loaded `deyazhashes.hsh` is checked against CTGPR.HSH's
  `debug/smallfile.sml` digest. Core files are bound to their checked roles:
  BOOT.BIN to `debug/bigfile1.big`, BOAT.BIN to `debug/bigfile7.big`, and
  BEAT.BIN to `debug/bigfile6.big`. The pinned complete CORE body digest binds
  that mapping to this release; substituting another signed core file fails.
  Package lists are capped at eight lists, 8 MiB of input, 8,192 total files
  and 2 MiB of copied paths; each list allows 8,192 tree entries, 4,096 files,
  1,024 bytes per source/destination path and 1 MiB of copied paths. Checked
  allocations refuse malformed lists through the existing files-damaged error.
  The channel button's launch path points at RiftWii's `boot.dol`.
* **Replacement ghost key.** The card's signing key (on `jelly.bin`) and the
  console's MAC are not read. CTGP gets a key RiftWii made to prevent accepted
  signatures (`include/riftwii/ctgpsign.hpp`). A fixed sample made offline
  with that key's signing numbers fails RiftWii's DSA verifier under both the
  supplied public value and CTGP's real list-signing public key. This tests
  neither CTGP's in-game signer nor the ghost server's public key, and proves
  no universal rejection claim. The decompile shows the signer takes its
  key from the loader block; the in-game jelly mount is for content.
  Ghost saving, replay and server rejection with the replacement key remain
  unverified. Earlier console saves/replays used the card's own key.

It starts CTGP-R only with the core this launch is made for (PACKAGES.BIN
core `0x01030412`): the core governs the game addresses the launch uses, so a
different core stops the launch with that on screen and in `boot.log`, before
anything is placed. The core's own code (BOOT.BIN, BOAT.BIN and BEAT.BIN) must
be the tested code, by its SHA-1s, and its signed `CORE.HSH` must vouch for it;
the list itself may be re-issued (CTGP-R re-signs it under the same number).
The CTGPR track package may be a newer version than the
tested `0x01031192`/`0x01031196`: its cups are read from the player's own
install (below), and its files are served only with digests from its signed
`CTGPR.HSH`, whose label must be the version PACKAGES.BIN names. Any list
that CTGP-R's key signs is accepted under that rule, as the channel does:
CTGP-R updates its tracks without always changing the version number (a 1196
card updated in November 2026 has a new `CTGPR.HSH` under the same label). One
exception is known: an earlier 1196 card kept the 1192 list, label and all; the
build knows that list's body digest (`ctgphash.cpp`), so its lagging label is
accepted too.

Native launches also refuse RVZ images: those need RiftWii's resident disc
reader, whose hooks and page table have not been shown to coexist with
CTGP-R's. Use a disc, ISO or WBFS. The earlier console results below cover
disc and USB; they provide no evidence for RVZ.

RiftWii's own extras stay out of the game: no runtime, codes, video or
game preferences, game fixes, GameCube adapter, screenshots, crash
recorder or Return to RiftWii (CTGP's channel button is pointed back at
RiftWii instead, and CTGP has its own crash handler). *Not confirmed:* that the
channel and Rankings buttons return to RiftWii; on a real Wii the channel
button is a known black-screen bug, so leave CTGP with the power button or
Reset.

### Files the native launch needs

Only CTGP-R's own files, as its installer leaves them on the card:

| File | What it is |
| --- | --- |
| `sd:/ctgpr/blob.bin` | CTGP-R's package disk (an encrypted FAT image of its code and data) |
| `sd:/ctgpr/jelly.bin` | CTGP-R's hash disk (its signed lists of file digests) |
| `sd:/ctgpr/config.ini` | CTGP-R's own settings (optional) |

The player needs no other files for it than RiftWii's own (its `ctgpr`
plugin, which every release carries, "Where the code lives" below). CTGP-R's
own constants are not secrets: its package-disk AES key and chunk IV and its jelly key words are
four of its own PPC instruction words, and its row table (`CRT0`, the row
count, three words a row for NTSC-U/NTSC-J/PAL, then 54 region words) is plain
data. RiftWii carries them in its own build (`include/riftwii/ctgptables.hpp`,
`src/ctgpr/ctgprowdata.cpp`, generated by `tools/ctgpr/gen_rowdata.py`). The
course table (the 32 base tracks' order) does not depend on the install and is
baked the same way (`src/ctgpr/ctgpapply.cpp`).

### The cup layouts

CTGP-R's `cup_course_layout_using` setting picks one of three cup layouts, and
the launch reads the chosen one from the player's install, as the channel does:

| Layout | Where it comes from |
| --- | --- |
| 0, the default | the channel program, `/PACKAGES/CHAN/MAIN.DOL` on `blob.bin` |
| 1, alphabetical | `/PACKAGES/CTGPR/alphabeticEN.cup` (`alphabeticJP.cup` when config.ini's `language` is 6), as it is |
| 2, the player's own | `sd:/ctgpr/cuplayout.cup`; without a usable one, no cups (the channel leaves that slot empty too) |

A `.cup` file is 224 bytes: `CUP2`, a cup count (at most 54), then 216 track
numbers, four to a cup. The channel copies it as it is.

The default layout exists only inside the channel program, which MAIN.DOL
carries packed. RiftWii unpacks it itself (`src/ctgpr/ctgpstage2.cpp`): it runs
the part of MAIN.DOL that builds its unpacking table in a small sandboxed
interpreter (only the instructions that code uses, a step limit, and no memory
outside the DOL's own sections and a stack), then maps and inflates the packed
program in 32 KB pieces and keeps only the layout table, which it finds through
the channel's own settings step. This is done only when layout 0 is chosen, and
reads and unpacks about 1.5 MB (a quarter of a second on a computer; the time on
a Wii has not been measured yet). Nothing
of MAIN.DOL runs on the Wii itself.

A layout with more than 54 cups or a track number of `0xff` (no such track) is
refused; for layouts 0 and 1 that stops the launch ("files damaged" on screen,
the reason in `boot.log`), and a damaged `cuplayout.cup` means no cups, as
above. A MAIN.DOL the unpacker does not understand (a future channel built
differently) stops the launch with the "can't be started from RiftWii yet"
message instead of guessing.

When CTGP-R is not installed (no `blob.bin` or `jelly.bin`), the pack goes the
channel-app way, as before. When it is, the launch is native, and it stops with
a sentence that names any problem (`CTGP-R is not installed on the SD card`, a
package disk that does not open, a hash list RiftWii could not read, an untested
version, the `ctgpr` plugin missing or from another RiftWii). The error is on
the failed-launch screen and in `boot.log`; nothing is placed in memory before
every check has passed, so RiftWii never starts the game half set up.

The menu's notes ("offline only" on the game page and at Start) need only
`blob.bin` and `jelly.bin` to be there (looked at once, again after a pack
scan): which CTGP-R version they hold is in
the encrypted package disk, which only the plugin reads, and the menu does not
load the plugin for a note. An untested version is refused when it is started.

### Where the code lives

Everything that reads, decrypts, checks and builds CTGP-R is the `ctgpr`
plugin (`plugins/ctgpr/`, [PLUGINS.md](PLUGINS.md#the-ctgpr-plugin)), loaded
into the plugin arena at the top of MEM2 only when CTGP-R starts, and shut
down again as soon as its plan is built:

| In the `ctgpr` plugin | In the DOL |
| --- | --- |
| The package disks opened and decrypted (`ctgpefa`, `ctgpfat`), the hash lists and their DSA check (`ctgphash`, `ctgpdsa`), the row table and the image linker (`ctgprowdata`, `ctgptables`, `ctgpimage`), the package lists and the record table (`ctgppackages`, `ctgpfiles`), the settings and their writes (`ctgpfields`, `ctgpapply`), the course table, the cup layouts and the channel program's unpacker (`ctgpstage2`), the assembly and the memory map (`ctgplaunch`, `ctgpmmu`), the ghost key (`ctgpsign`); the launch's steps that read them (`plugins/ctgpr/ctgprprepare.cpp`) | Whether a pack is CTGP-R's channel swap and the cheat/other-pack lockout (`ctgp_is_native_stand_in`, `CtgprPackOn`, `ApplyCtgprPrecedence`); the menu's notes; the MEM2 block for the plan's buffers, the plugin's launch and the translated sentences (`wii/launch/ctgpr.cpp`); the raw SD reads the plugin is given (`wii/storage/sdfile.cpp`); the plan checked against the loaded game and written into MEM1, the files put into the game's file system table (`src/ctgpr/ctgpplace.cpp`) and the memory map started (`start_ctgp_memory_map` in `wii/launch/boot.cpp`) |

The plan the plugin fills is the DOL's own `CtgpLaunchPlan`, handed over by
address in `CtgprLaunchRequest` (`include/riftwii/plugin_ctgpr.hpp`): its big
buffers are already in the DOL's MEM2 block, so nothing is copied. The host
tests build all of `src/ctgpr` as before.

### Memory

The launch's big buffers (MAIN.DOL while the default cup layout is unpacked,
BOOT.BIN and BOAT.BIN as read and as linked, the record table, the page table,
the package and hash lists) are `CtgpBytes`, which
draw from one 6 MB MEM2 block (`include/riftwii/ctgparena.hpp`, reserved in
`wii/launch/ctgpr.cpp` with `mem::TryMem2Alloc`) instead of the MEM1-first
default heap. MAIN.DOL (1.5 MB) and the unpacker's memory (the interpreter's
216 KB of cache and store tables, zlib's state and window, a 32 KB piece) are freed before BOOT.BIN is read, so they never add to
the block's peak. The as-read copies are freed once the plan holds the linked ones.
The block is released when the plan is destroyed or the launch fails; a buffer
that finds no room in it uses the heap. `boot.log` says how much of the block
was used at the most.

The code itself is not in MEM1 either: the `ctgpr` plugin (about 150 KB of
the 384 KiB plugin arena) is read from the card into MEM2 when CTGP-R starts
and given back before the game loads. The DOL keeps about 7 KB for CTGP-R
(the lockout, the placement and the launch's frame).

### Checks

Host tests cover each step with made-up data (`tests/ctgp*_tests.cpp`):
the DSA signature check accepts a correctly signed list and rejects a tampered
one (`ctgpdsa_tests`, `ctgphash_tests`, with a throwaway test key, since
CTGP-R's key is not ours to sign with), the ghost-signing key is laid out as
the channel's and a fixed negative signature sample is checked, the baked course table is
checked against its known-good values, the cup-layout files and the channel
program's unpacker are checked on small made-up programs and files
(`ctgpstage2_tests`, `ctgpapply_tests`), online and LAN stay off
whatever config.ini says (enabled or not), the native launch is refused for a
disc that is not Mario Kart Wii, a BOOT/BOAT image that would not fit MEM1 is
refused, and an untested core is refused. The earlier
build of this launch (on 3.1.0) was tested on a PAL Wii from disc and
USB (menus, custom tracks, VS, a full Grand Prix, Time Trial ghost save
and replay) with the card's own signing key; this one replaces that key and
needs a Dolphin and a Wii run. There is no current Dolphin/Wii evidence for
ghost save/replay with this key or launch memory pressure; host size/layout
checks and the build's start-up stash address do not establish either.
The channel button's return to RiftWii (a black screen on a real Wii) is a known open bug.

## How RiftWii starts the CTGP-R Channel app

CTGP-R 1.03's Riivolution XML replaces `main.dol` with the CTGP-R Channel
(`/apps/ctgpr/boot.dol`). `homebrew_app_stand_in` (`wii/launch/modplan.cpp`)
recognises a choice that only does that, and `RunLaunch` (`wii/launch/launchrun.cpp`)
starts the app the way the Homebrew Channel does (`StartHomebrewApp`), not
through the apploader. The save redirect in CTGP's XML cannot apply and is
dropped (the pack's `<savegame external clone>` is not run).

## CTGP-R's save on the SD card (native launch)

This section is about the native launch only (`ctgpnative.txt`). By default
CTGP-R's own channel starts, and it looks after the save itself (its "Copy MKW
save to SD card?" option); RiftWii copies nothing then.

CTGP-R's own code in the game sends every access to Mario Kart Wii's NAND data
(`/title/00010004/524d43xx/data/`) to `sd:/riivolution/save/RMC<R>/` on the
card (R is the game ID's fourth character: E, J, K or P), whatever its
config.ini's `save_to_sd` says. With no such folder the game cannot open its
save and shows "Continue without saving / Return to the Wii Menu".

Before a native launch (`SeedCtgprSdSave`, `wii/storage/d2xsave.cpp`; the names
and the choice of files are `include/riftwii/ctgprsave.hpp`), under the menu's
IOS and before the probe can reload it, RiftWii does what Riivolution's
`clone="true"` does for the pack's XML, which the native launch does not run:

* A folder that already holds `rksys.dat` is left exactly as it is.
* Otherwise the folder is made and the files of the NAND data folder
  (rksys.dat, banner.bin, wc24dl.vff, wc24scr.vff, whatever is there) are
  copied in through one MEM2 block (freed straight after; 256 KiB of it is used), each under a temporary name
  (`<name>.rwtmp`) that is then renamed, rksys.dat last. Nothing on the card is
  overwritten or deleted. If a copy fails, only what that run made is removed
  (a folder it made goes too) and the launch goes on: boot.log says why, and
  the game may start without saving. A failed copy is never turned into an
  empty folder, because a new save made there would shadow the one on the Wii.
* The Wii's save folder is listed strictly: only ISFS "not found" (-106) means
  no save. Any other answer (a permission refusal, an ISFS error) fails the
  seed with the code in boot.log and leaves the card untouched, so no empty
  folder can hide a real save. A folder inside the data folder is skipped
  (logged), and a leftover `*.rwtmp` of an earlier stopped copy is removed
  before a file of that name is written. /dev/fs is closed on every way out.
* A Wii with no save for the game (a new player) gets the empty folder, as
  Riivolution makes it, and the game starts its own save in it. This follows
  Riivolution's clone and RiftWii's runtime clone, where a missing NAND folder
  is an empty save; it has not been run in Dolphin with CTGP-R.

The game page's Saves row says "SD card" and its note names the folder for a pack
that starts CTGP-R natively.

* **Disc:** the app starts under the menu's IOS with hardware access, before
  any probe or IOS reload.
* **ISO/WBFS on USB or SD:** d2x is set up to serve the image as the disc,
  then the app runs under the cIOS. CTGP reloads IOS itself (its channel asks
  for IOS37), which on its own would drop d2x and the virtual disc. Since RC2
  the activation arms d2x's IOS reload block first (below), so those reloads
  land in the same cIOS.
* **RVZ:** refused. RiftWii's PPC resident code reads an RVZ; d2x only holds
  its stub, and nothing of RiftWii survives into CTGP's later stages.

## Hardware access under the cIOS

CTGP's `meta.xml` asks the Homebrew Channel for hardware access
(`<ahb_access/>`), and an IOS reload ends it. On 2.7.0 RC6 a tester's
Wii (USB, NTFS, RMCP01, IOS249) did everything up to the handoff:
d2x accepted the reload block and served the disc, and the app started
under IOS249 with "hardware access off". The CTGP channel then showed a
green screen.

Since 2.7.0 RC7, `keep_hardware_access` (`wii/launch/boot.cpp`) changes the
running IOS's ES right before the reload into the cIOS so the cIOS keeps
hardware access, as USB Loader GX and WiiFlow do with libruntimeiospatch's
`IosPatch_AHBPROT`. It does the same to the cIOS's ES once d2x is set up,
for CTGP's own reloads. Only the Homebrew Channel app path does this, not
ordinary game launches. `boot.log` shows `IOS<n>'s ES keeps hardware
access on for the next IOS` for each, and `Homebrew app: IOS<n>, hardware
access on` when it worked.

## Starting the channel from a USB or SD image

For an image, d2x serves it as the disc, then the channel app runs under
the cIOS. Three things make that work:

* **No argument block.** CTGP-R 1.03's channel hashes its first stub segment
  at 0x80004000 in its start-up memory check, and that segment holds the
  libogc `_arg` block at 0x80004008. RiftWii's loader used to fill that
  block (the channel's own path, `dolboot_run_line`), so the check failed,
  the channel restarted itself, and the restart's IOS reload used up d2x's
  one disc carry ("insert the disc"). RiftWii now starts CTGP-R's channel app
  without writing the block (`dolboot_run_no_args` in
  `channel/common/dolboot.c`, the `write_args` flag of `StartHomebrewApp` in
  `wii/launch/channel.cpp`), as the Homebrew Channel and the apploader leave
  a program. Other apps still get their arguments.
* **MEM2 protection start.** The channel turns MEM2 protection on and then
  clears MEM2 below its start, which IOS leaves at 0x13620000; d2x's plugins
  live in 0x13400000 to 0x1361ffff. Before it starts the app, RiftWii moves
  the protection start to 0x13400000 (register 0xCD8B420C, only with
  hardware access) so the channel's clear leaves them. The next IOS reload
  puts the start back.
* **config.ini keys** (`SetCtgpDefaults`): on a virtual disc RiftWii sets
  `[sd] compatible_mode`, `[exploit] disable_patches` and
  `[exploit] disable_ios_exploit` to `yes` in `sd:/ctgpr/config.ini`, so the
  channel reloads into the cIOS it started on (not the game's IOS) and does
  not rewrite ES patterns in the IOS area of MEM2, where d2x's own modules
  live. The channel writes `no` for these itself, so the value is set, not
  only added (`riftwii/ctgpconfig.hpp`, `tests/ctgpconfig_tests.cpp`).

`boot.log` names each step: the channel launch chosen, the d2x virtual disc
set up, the protection move, and the app start.

Checked on a Wii with a USB image: the channel opens without restarts, finds
d2x's virtual disc, and an offline CTGP-R race runs. Dolphin cannot test
this: its HLE IOS has no d2x.

## CTGP's own IOS exploit

At startup the CTGP channel patches the running IOS's ES itself when it has
hardware access, then checks the patches with a fakesigned ES_Identify. When
that check fails without hardware access, it runs its own IOS exploit
through `/dev/sha` and then waits for hardware access with no time limit. A
cIOS where the exploit does not take would hang there.

The channel skips the exploit when `sd:/ctgpr/config.ini` says so in its
`[exploit]` section (`disable_ios_exploit = yes`; booleans are `yes`/`no`,
and CTGP only writes keys that differ from the default). Since 2.7.0 RC9,
RiftWii adds that line before it starts `sd:/apps/ctgpr/boot.dol`
(`SetCtgpDefaults`, `wii/launch/launchrun.cpp`; the INI merge is
`riftwii/ctgpconfig.hpp`, tested in `tests/ctgpconfig_tests.cpp`; section
and key names match whatever their case). From a real disc a value the
player set is left alone. From a USB or SD image that key and the two others
above (`[sd] compatible_mode`, `[exploit] disable_patches`) are set to `yes`
whatever the player had; `boot.log` gives each old value. The keys do not
affect networking: online play comes from CTGP's patches to the game's code.
`boot.log` shows e.g. `CTGP: config.ini [exploit] disable_ios_exploit = yes
(was not set)`.

## d2x IOS reload block

ES ioctlv `0xA0` on `/dev/es`, two 4-byte inputs: mode 2 and the cIOS slot
to load in place of any IOS the program asks for (as USB Loader GX's
`BlockIOSReload` sends it). Mode 0 with one input clears it.
`di::set_ios_reload_block` (`wii/launch/di.cpp`) sends it; `tests/d2xreload_tests.cpp`
runs the real `wii/launch/di.cpp` against a mocked IOS (`tests/wii_mock/`) to check
the vectors, alignment, slot bounds (200-255) and error handling.

It is armed in `activate_image_game` right after the F6 reset-disable,
**before the first partition probe**: d2x hides its ES commands once ES has
identified the disc's title. A new fragment configuration (a fresh cIOS
reload) forgets it.

If the probe or the app's start fails, RiftWii tries to clear it; otherwise
every later IOS reload, including the menu's own, would land in the cIOS.
Whether d2x accepts the clear after a probe has identified the title is
unknown. When it refuses, the error tells the player to restart the console.
`boot.log` shows `d2x keeps IOS<n> across the program's own IOS reloads`
when armed and `IOS reload block cleared` / `could not be cleared` after a
failure.

## What a console test must show

1. A USB or SD ISO/WBFS of Mario Kart Wii with CTGP's pack enabled reaches
   the CTGP channel, then an offline race.
2. Save and load work, and relaunching works.
3. A failed start (rename `/apps/ctgpr/boot.dol`) logs the clear, and the
   menu can still start another game.
4. A real disc, as the control.

Dolphin cannot test this: its HLE IOS has no d2x.

## Research notes (CTGP-R 1.03.1190 All Inclusive)

Why the resident runtime route (the pack's `main.dol` loaded as the game)
does not work:

1. `install_resident` needs `IOS_IoctlAsync`; the host `ipcscan` finds no
   IPC-shaped function in the packed launcher, the unpacked boot runtime or
   the unpacked channel.
2. The channel uses synchronous IPC for its disc commands (disc ID `0x70`,
   partition open `0x8B`, reads `0x71`, unencrypted reads `0x8D`); the
   resident redirects only the asynchronous path.
3. The first executable is not the last: CTGP unpacks a boot runtime at
   `0x80780000` and the channel at `0x80800000`. Hooks in the stub do not
   carry over.
4. CTGP has its own IOS reload paths (boot runtime and channel; the channel
   asks for IOS37 on its low-IOS path). `preserve_current_ios` only covers
   RiftWii's own launch preparation.

If d2x cannot satisfy CTGP's IOS setup, the next step is an IOS-side virtual
disc that survives ES reloads, starting from the MIT-licensed
[CTGP-R MSC launcher](https://github.com/mkwcat/ctgpr-msc) (`ios/IOS/EmuES.cpp`
shows the reload lifecycle; its `/dev/di` proxy is disabled and there is no
implemented disc resource manager).

These notes came from Ghidra pseudocode and the original binaries under
`D:/AI Projects/CTGPR DECOMP/`; no CTGP code or assets are in RiftWii.
Addresses describe that build only.

| Input | SHA-256 |
|---|---|
| `apps/ctgpr/boot.dol` | `b4c6ba27bc22813ecad686e918e5dafe9cd2ce8afa84427aa60dd865d2fe4556` |
| extracted `boot_internal.dol` | `17a329273369e1586677a0016ab354f41e49d971d0f596d9f65c7d3f6f2ab6d6` |
| extracted `chan_main_internal.dol` | `e66fdf5cf14cbc48bc50132c9ca9de42d46993740557b89eead9e010ce5e1cc2` |
