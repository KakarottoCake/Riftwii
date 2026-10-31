# The RiftWii channel

A Wii Menu channel that starts RiftWii without the Homebrew Channel.

It holds no copy of RiftWii. Its program (the forwarder,
`channel/forwarder/main.c`) shows the RiftWii logo for about half a second,
then loads `apps/riftwii/boot.dol` from the SD card, or from the USB drive when
the card has none. It hands the file's path over as `argv[0]`, as the Homebrew
Channel does, so RiftWii's own updates keep replacing that file and the channel
never needs reinstalling.

## Installing

The channel has its own installer, a Homebrew Channel app shipped in the
release zip as `apps/riftwii_channel` (`channel/installer`). It carries the
channel, so RiftWii itself carries none of it: RiftWii's memory is not spent
on a package it uses once.

- **From RiftWii:** once, on the first start where the channel could be
  installed and is not, RiftWii offers to open the installer. The answer is
  remembered in `sd:/riftwii/channel_offered.txt`. Settings has
  **RiftWii channel on the Wii Menu**, which opens the installer at any time.
  RiftWii starts it directly (`channel/common/dolboot.c`), and the installer
  starts RiftWii again when it closes. From the prompt that asks to update an
  older channel, **Update now** passes the installer the argument `update`:
  it updates at once, shows the result for four seconds and starts RiftWii
  again, with no button to press. The installer reads only the Wii Remote's
  own buttons and the Wii's own GameCube ports, so a player with a GameCube
  controller on a Wii U's adapter could press nothing there. If no
  cIOS slot can be checked, update mode takes the safe answer (no unchecked
  cIOS), shows that a d2x cIOS is needed and goes back; RiftWii then says the
  channel is still old. The slots are checked with RiftWii's hardware access,
  which a RiftWii started by the installer doesn't have (the installer's
  IOS58 reload takes it away), so from such a start the prompt offers no
  **Update now** and says to start RiftWii from the Homebrew Channel or the
  RiftWii channel (version 9 or newer gives it that access). With that access, **Update now** is not offered when every
  slot with a ticket reads as a cIOS that is not d2x, the only kind the
  installer tries.
- **From the Homebrew Channel:** it is listed as RiftWii Channel Installer.

The installer adds, updates or removes the channel. It needs a d2x cIOS in slot
249, 250 or 251. It sets up video and the SD card under the starting IOS,
then reads each slot's d2x information with the same parser as RiftWii.
When any slot information can be read, only identified d2x slots are tried,
preferring base 58 for the SD card. Its Homebrew Channel metadata asks for
hardware access so it can read the slot information through IOS58's NAND
permission check. The starting AHBPROT register and the chosen search path
are saved to `sd:/riftwii/channel.log`.

If no slot could be checked (for example, hardware access was lost when
RiftWii reloaded IOS), the installer asks **No cIOS slot could be checked.
Try the slots with a ticket anyway?** Above the question it warns that an
unchecked cIOS can freeze the console and says to hold POWER to turn it off.
The prompt waits at least about a second and for all Wii Remote, GameCube
and RESET buttons to be released, so a held A from **Update now** cannot
accept it. If buttons remain held for about three seconds, it goes back
without reloading an unchecked cIOS. A accepts this unchecked fallback;
B goes back without trying a cIOS. An accepted slot remains available for
installing, but is labelled as not checked for d2x. Each attempt is shown
and logged before the card is unmounted and IOS is reloaded; the card is
mounted again afterwards, and a successful reload logs the IOS version
and revision.

Reading cIOS information temporarily opens IOS's NAND permission check.
Both the original permission-check word and memory protection register
must be restored and verified. If either restoration fails, the installer
stops before any IOS reload, including the return-to-RiftWii reload. It
shows and logs that it cannot continue safely and must be restarted from
the Homebrew Channel. Install and Remove are unavailable on this path.

Before opening the installer, RiftWii writes
`sd:/riftwii/channel_installer_opened.txt`. In `leave()`, while SD is still
mounted, the installer deletes that marker only if the installed channel
has reached its package version. Otherwise it rewrites the marker with
`returned` on its first line, including when the player chooses Back or an
install fails. It then cold-starts RiftWii (or exits to the Homebrew
Channel); this normal return has no `ChannelDone` restart
note. An old channel with an opened marker means RiftWii opened the
installer and has not seen it finish. A `returned` marker instead says the
installer ran but the channel is still at its old version. Recovery keeps
the installed and required versions and the update requirement on screen,
adds the appropriate explanation and asks for `channel.log` in a problem
report when the installer was interrupted. It also shows
why updating from here is unavailable, when applicable.

Recovery offers **Not now**, which opens Home for this start. The marker
stays, so later starts still offer **Not now** until the channel is updated
or the channel no longer needs updating. A refused hand-off warm-restarts
with `ChannelDone`, clears any stale marker and shows the actual refusal
reason in place of the interruption explanation.

If SD cannot save the marker, that refusal screen offers **Open anyway**
when the installer can otherwise run. It explains that RiftWii cannot leave
a note on SD, so a hang will not be detected on the next start. Choosing
it explicitly allows the same installer hand-off without a saved marker;
**Not now** and **Exit RiftWii** remain available. A normal installer return
without updating records `returned` if the card can save it, preserving
**Not now** on the next start. When the channel is current or absent, a
refused hand-off shows its reason as a Home notice instead of an update page.

The channel is title `00010001-55465457` (`UFTW`) and runs under IOS 58. A
vWii shows a title in 4:3 when its ID starts with C, D, E, F, H, J, L, M,
N, P, Q, R, S, T, W or X (unless Priiloader's pillarboxing hacks are on);
up to version 5 it was `RFTW`, which the installer removes.

### A Wii package and a vWii package

A Wii and a vWii start a channel's boot program (content 1) differently,
and no one layout started on both, so the channel comes in two packages.
The installer carries both and installs the one for the console it runs
on: a vWii has the Wii U's boot program for channels, BC-NAND
(`00000001-00000200`), and a Wii never does.

- **Wii:** the CPU starts at physical `0x3400` with address translation off,
  whatever the DOL's entry field says (every retail channel has a small
  section there). Two contents: **0** the banner, **1** the forwarder, with
  `channel/forwarder/start.S` added at `0x80003400` as its first section
  (`tools/make_channel.py`, `wii_forwarder`). That sets the CPU up as the
  Homebrew Channel does for a program (caches, BATs, segments, HID4), clears
  the BI2 pointer at `0x800000f4` and jumps to the forwarder's entry
  (libogc's, `0x80003f00`), which the entry field keeps.
- **vWii:** BC-NAND loads the boot program and jumps to its entry field,
  with translation on. It never started the forwarder as the boot program
  (770 KiB across low MEM1): no line of its log was written. Three
  contents: **0** the banner, **1** a boot program of about 4 KiB
  (`channel/loader`), **2** the forwarder, linked at `0x80100000` above it.
  The boot program is laid out as FIX94's vWii NAND loader (the one tools
  like WiiForwarder2vWii put into forwarders) and OpenDolBoot are:
  `channel/loader/start.S` at `0x80003400`, the entry field at `vwii_entry`
  just after it. It sets the CPU up the same way, then `loader.c`, with no
  library, opens `/dev/es` through the IPC registers, reads content 2 into
  MEM2 (`0x91000000`), copies its sections into place and jumps to it; with
  nothing to start it goes back to the Wii Menu (`check_loader` checks the
  layout). On a Wii this one did not start.

The installer is linked at the usual `0x80004000`; all of it is below
RiftWii (`0x80a00000` up). The forwarder writes `Channel started` to
`sd:/riftwii/channel.log` once it has mounted the SD card (and, on the next
line, how long the card took to start and to mount), and
`build-channel/forwarder.dol` still runs from the Homebrew Channel or
Dolphin by itself.

After the intro, the forwarder reads the SD card and the USB drive on a
thread of its own and shows each step under the logo: starting the SD
card, mounting it, reading `boot.dol`, trying the USB drive, starting
RiftWii. A step that takes too long (10 seconds to start or mount the SD
card, 20 for the USB drive, 60 to read `boot.dol`) ends on an error screen
that names it, then back to the Wii Menu: on some vWii consoles the
channel stopped for good after the intro with nothing on screen (issue
#228). A call stuck in IOS cannot be cancelled, so that screen touches
neither the card nor the drive. During the steps and on that screen, POWER
turns the Wii off and RESET goes back to the Wii Menu (before, libogc's
default ignored a short POWER press). `make -f Makefile.channel
CHANNEL_TEST_HANG=<step>` builds a forwarder that stops at that step
(`StepId` in `main.c`; 1 is mounting the SD card), to see that screen in
Dolphin; never for a release. The forwarder is rebuilt whenever the flag
changes (a stamp in `build-channel/obj`), so a plain `make -f
Makefile.channel` afterwards builds the normal one again, no clean needed.

Dolphin starts NAND titles as a Wii does, so it tests the Wii path of
either package, not the vWii one. It also stalls on a boot program whose
sections are not 32-byte sizes, which `channel/loader/loader.ld` pads as
retail ones are.

The installer reloads IOS 58 before it reads RiftWii back in: a reload
overwrites the bottom of MEM2, where the file is read to.

libogc closes every IOS handle from 0 to 31 as it starts (`SYS_PreMain`),
for a program started by another under the same IOS. The forwarder skips
that (`channel/forwarder/startup.c`, through the linker's `--wrap`): the
system starts the channel under an IOS just loaded for it, and straight
after a game left while it was still loading (Wii Sports, back to RiftWii
from its HOME Menu at once) one of those closes could go unanswered, a
black screen before `main`. Since version 8.

The title asks ES for hardware access (its TMD's access rights, bit 0), as
the Homebrew Channel's does: RiftWii started from the channel then has
AHBPROT off, as from the Homebrew Channel, for what needs the hardware (the
Wii Menu's font behind IOS's NAND permission check, the GameCube adapter).
Before version 9 it asked for none, and those were off when started from
the channel.

The Wii boot program empties the CPU's L2 cache before libogc starts
(`channel/forwarder/start.S`), with HID4's L2CFI off so nothing in it is
written back. A launch turns the L2 off but keeps what it holds, and
libogc 3.1.0's start-up turns it back on without the invalidate it means
to do (`PPCEarlyInit` sets L2CE where it means L2I; libogc 3.2.0 fixed the
bit, and writes the lines back first). After leaving some games for the
channel (Wii Fit Plus, among others) the channel ran on the game's cached
data and stopped on a black screen, at a different point each time, while
the Wii Menu, the Homebrew Channel and the Everybody Votes Channel, whose
start-ups empty the L2, came up. `tools/make_channel.py` refuses a Wii
boot program without that clear (`L2_CLEAR`). Since version 11.

The vWii loader (`channel/loader/start.S`) has no such clear: channels 11
and 12 had it and never started on a vWii (12 skipped it by the CPU's PVR,
which did not help). Since 13 the vWii loader is the one of 10 again, byte
for byte, and `tools/make_channel.py` refuses a vWii loader with the clear
(`L2_CLEAR`). A vWii needs channel 14 (`ChannelVersionNeeded`), a Wii 11.

## The banner

All of it is drawn for RiftWii; no Nintendo data is used.

- **Art:** `tools/make_channel_art.py` (Pillow, numpy) draws the pictures into
  `channel/art/`, which is committed, so the build needs only the standard
  library: a nebula backdrop, clouds, sparkles, two energy rings, the rift,
  and each letter of the word with its glow, in RiftWii's menu font
  (`wii/font/rounded.ttf`, SIL OFL). It also draws the installer's Homebrew
  Channel icon.
- **Animation:** `tools/make_channel.py` places and animates them.
  `banner_start` (about 1.8 seconds) plays once when the channel is picked:
  the sky fades up, the rift tears open with a flash, the rings spin up and
  the letters fly out of the rift one by one and settle. `banner_loop`
  (8 seconds) follows: the letters bob, the rift flickers, the rings turn,
  sparks rise and a shine crosses the word. The icon loops a smaller scene.
- **Sound:** made by the same script, timed to `banner_start`: a whoosh, a zap
  as the rift opens, a note for each letter and a closing chord, encoded as
  DSP-ADPCM.
- **Preview:** `tools/preview_channel.py channel/art <dir>` renders the banner
  and icon to video from the same data (Pillow, numpy, ffmpeg); the banner as
  a 16:9 Wii Menu shows it, the icon at 4:3. For still frames from the built
  content, write content 0 with an eighth argument to the build step that
  `make -f Makefile.channel` runs (`python3 tools/make_channel.py build
  build-channel/loader.dol build-channel/forwarder.dol build-channel/start.bin
  build-channel/forwarder_wii.dol channel/art build-channel build-channel`
  writes `build-channel/00000000.app`; another folder must exist first), then
  run `build-host/bannerdump build-channel/00000000.app <existing output
  dir>`. `bannerdump` is a host tool, built with the host
  tests (`cmake --build build-host --target bannerdump`), not by
  `Makefile.channel`. `BANNER_WIDE=1` includes the Wii Menu's 16:9 stretch and
  shape correction (unset for 4:3).

The formats (U8, IMD5, LZ77, TPL, BRLYT, BRLAN, BNS, IMET) were written from
their public descriptions and checked against a retail banner's layout.

Since channel version 15, the banner and grid icon keep their foreground
shapes on a 16:9 Wii Menu. A parent pane uses BRLYT flag `0x04`, as retail
logos do; the Menu narrows that group by 3/4 before stretching the screen.
The nebula and clouds stay outside it and fill the width. The banner's
foreground moves up 36 layout rows, centring the word and rift above the
Wii Menu / Start bar and bringing the tagline into view. The icon's
foreground moves down 4 rows to centre its word in the visible 128x96 tile.
Only the parents change: all animation tracks, keys and timing stay the same.
Both layouts keep their existing 608x456 size, and no texture grows.

The forwarder's start animation is drawn by the CPU, which takes longer than
one screen refresh. Since version 15 it draws each frame into a second,
hidden framebuffer and shows it at the next retrace, so the TV never shows a
frame half drawn (before, the flash could cover only the top of the screen).

## Building

`make -f Makefile.channel` builds the forwarder, then the package
(`tools/make_channel.py build`), then `build-channel/installer.dol`, which
embeds the package. `Makefile.wii` does not need any of it.

## Signing and encryption, on the console

The package's TMD and ticket are unsigned, and its contents unencrypted.
The installer does both on the console:

- **Title key:** a fixed key, encrypted with the console's common key by ES
  itself (`ES_Encrypt` with key handle 4). The common key never leaves IOS and
  is not in this repository. The installer checks the step by decrypting the
  key again.
- **Contents:** AES-128-CBC with that key, with the content index as the IV.
- **Signatures:** the TMD and ticket are fakesigned: a zeroed signature, and a
  counter in unused bytes chosen so the SHA-1 starts with a zero byte. The
  d2x cIOS's signature patches accept this.
- **Certificates:** read from the console's own `/sys/cert.sys`.

Dolphin checks real signatures, so the install only works on a Wii; there the
installer says so. Dolphin can still boot `build-channel/forwarder.dol`
directly, which tests the forwarder: the session log then says
`Started from sd:/apps/riftwii/boot.dol`.
