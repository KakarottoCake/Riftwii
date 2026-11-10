# CTGP-R 1.03 from RiftWii

RiftWii has two ways to start CTGP-R 1.03's pack:

* **The native launch, offline only** (below, "The native launch"):
  RiftWii loads Mario Kart Wii and places CTGP-R's code in it itself,
  with no channel app. Used when its files are set up on the card.
* **The CTGP-R Channel as a Homebrew Channel app** (the rest of this
  page): experimental, and it stops before CTGP's menus on testers'
  consoles. Used when the native launch's files are not on the card.

## The native launch (offline only)

The CTGP-R developers allow it on these conditions, which the code keeps:

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
  outside those bytes. RiftWii therefore also binds the full body's digest to
  a locally checked package/version reference, refusing an unknown reference
  rather than trusting a relabelled list. This matters here because
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

It starts CTGP-R only on the versions this launch is made for (PACKAGES.BIN
core `0x01030412`, CTGPR `0x01031192` or `0x01031196`). The core governs the
game addresses the launch uses, and the CTGPR track package governs the cup
layouts, which are baked into this build for this CTGPR content; a different
core or a different track package stops the launch with that on screen and in
`boot.log`, before anything is placed. (A later step that works the cup layouts
out from the player's own package disk will let the track-package version vary
again.) The two accepted CTGPR numbers are the same content: CTGP republished
PACKAGES.BIN with `0x01031196` without re-versioning `CTGPR.HSH`, whose body
and own label (still `0x01031192`) are unchanged, and the core, tracks,
`CTGP.CRS` and `.cup` files are identical on both. The build binds that one
`CTGPR.HSH` body digest to both numbers (`ctgphash.cpp`), so a card whose
PACKAGES.BIN lists either number verifies and the menu marks it supported.

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

The player needs no RiftWii files for it. CTGP-R's own constants are not
secrets: its package-disk AES key and chunk IV and its jelly key words are
four of its own PPC instruction words, and its row table (`CRT0`, the row
count, three words a row for NTSC-U/NTSC-J/PAL, then 54 region words) is plain
data. RiftWii carries them in its own build (`include/riftwii/ctgptables.hpp`,
`src/ctgpr/ctgprowdata.cpp`, generated by `tools/ctgpr/gen_rowdata.py`). The
course table and the two cup layouts are plain CTGP-R data too, baked into the
build the same way (`src/ctgpr/ctgpapply.cpp`), so RiftWii carries none of those
from a file either. (They used to be read from `CTGP.CRS` and the
`alphabetic*.cup` files on `blob.bin`, but the channel derives the real tables
from those, so copying them verbatim gave the game the wrong cups; a later step
will derive them correctly and the baked copies will go.)

When CTGP-R is not installed (no `blob.bin` or `jelly.bin`), the pack goes the
channel-app way, as before. When it is, the launch is native, and it stops with
a sentence that names any problem (`CTGP-R is not installed on the SD card`, a
package disk that does not open, a hash list RiftWii could not read, an untested
version). The error is on the failed-launch screen and in `boot.log`; nothing
is placed in memory before every check has passed, so RiftWii never starts the
game half set up.

### Memory

The launch's big buffers (BOOT.BIN and BOAT.BIN as read and as linked, the
record table, the page table, the package and hash lists) are `CtgpBytes`, which
draw from one 6 MB MEM2 block (`include/riftwii/ctgparena.hpp`, reserved in
`wii/launch/ctgpr.cpp` with `mem::TryMem2Alloc`) instead of the MEM1-first
default heap. The as-read copies are freed once the plan holds the linked ones.
The block is released when the plan is destroyed or the launch fails; a buffer
that finds no room in it uses the heap. `boot.log` says how much of the block
was used at the most.

### Checks

Host tests cover each step with made-up data (`tests/ctgp*_tests.cpp`):
the DSA signature check accepts a correctly signed list and rejects a tampered
one (`ctgpdsa_tests`, `ctgphash_tests`, with a throwaway test key, since
CTGP-R's key is not ours to sign with), the ghost-signing key is laid out as
the channel's and a fixed negative signature sample is checked, the baked course table and cup
layouts are checked against their known-good values, online and LAN stay off
whatever config.ini says (enabled or not), the native launch is refused for a
disc that is not Mario Kart Wii, a BOOT/BOAT image that would not fit MEM1 is
refused, and untested versions (core or CTGPR) are refused. The earlier
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
dropped.

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
access on` when it worked. Untested on a Wii.

## Status after 2.7.0 RC9.2 (console test, USB image)

On a Wii (USA, d2x in 249, game on a USB drive) RC9.2 stopped right after
`IOS58's ES keeps hardware access on for the next IOS`: the reload into
the cIOS never finished. `keep_hardware_access` wrote its byte through
MEM2's uncached mirror; since 4b26958 it changes the byte inside its
aligned 32-bit word. With that build the whole RiftWii side worked
(d2x serves the image, hardware access on through both reloads, the app
starts) and the CTGP channel opened, after a green, black, green start,
but asked for the Mario Kart Wii disc. So CTGP's channel does not see
d2x's virtual disc. Which of its disc steps fails (its own IOS reload,
the drive's cover status, a drive reset) is not known yet; the console
gives no log past the handover. Starting CTGP from the Homebrew Channel
stays the supported way.

## Why CTGP asks for the disc (diagnostic, 2026-10-02)

A stand-in app started in CTGP's place (same launch, USB image, d2x 249)
logged the state at each step:

- At the handover everything is right: IOS249, hardware access on, ES's
  running title is the game (00010004-RMCE, a disc game), the drive says a
  disc is in and reads `RMCE01`.
- A first IOS reload is redirected by d2x into IOS249 and keeps the game,
  even across a drive reset. After it ES has no running title (-1017).
- A second IOS reload (IOS37) is not redirected: d2x only redirects a
  reload while ES names a disc game as running, and that comes back only
  once the program opens the disc's partition. The real IOS37 then reads
  the real drive (another disc gave CTGP's "this isn't Mario Kart Wii").

A game opens its partition between reloads; CTGP's loader chain reloads
IOS more than once before its disc check, so the game is gone by then.
`channel_installation_ios = 249` in CTGP's config.ini did not change it.
The reloads are in CTGP's encrypted stages (its loader and the channel in
blob.bin, which CTGP updates itself), so RiftWii does not patch them.
CTGP from a USB/SD image stays experimental; start it from the
Homebrew Channel instead.

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
`riftwii/ctgpconfig.hpp`, tested in `tests/ctgpconfig_tests.cpp`). A value
the player set is left alone. The key does not affect networking: online
play comes from CTGP's patches to the game's code. `boot.log` shows
`CTGP: config.ini set to skip CTGP's own IOS exploit`. Untested on a Wii.

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

## On hold (2026-10-04)

A CTGP-R developer (who asked not to be named) offered help: they want
CTGP to run from RiftWii as long as its anti-cheat keeps working. They
sent a tool that decrypts `blob.bin`; it uses the same key and IV scheme
the decompile in `D:/AI Projects/CTGPR DECOMP/` already found, so nothing
new came from it, and none of it goes into RiftWii.

The fix has to come from CTGP's side: its channel reloads IOS twice and d2x
fakes only the first (above), and the reloads live in CTGP's encrypted,
anti-cheat-protected code, which RiftWii must not change. Questions for the
developer when this resumes:

1. What the second reload (into IOS37) is for, and whether CTGP could
   stay on a d2x cIOS that already emulates Mario Kart Wii's disc.
2. A supported "started by a loader" mode (a `config.ini` key, or a value
   RiftWii leaves in memory) that keeps the running cIOS and the disc, with
   every anti-cheat check kept.
3. What the anti-cheat needs from the environment (IOS, untouched ES, no
   hooks in the game) so RiftWii can guarantee it.
4. Whether the disc check needs anything d2x's emulation lacks.

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
