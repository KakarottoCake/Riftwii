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
  starts RiftWii again when it closes.
- **From the Homebrew Channel:** it is listed as RiftWii Channel Installer.

The installer adds, updates or removes the channel. It needs a d2x cIOS in slot
249, 250 or 251, which it switches to first. What it does is logged to
`sd:/riftwii/channel.log`.

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
`sd:/riftwii/channel.log` once it runs, and `build-channel/forwarder.dol`
still runs from the Homebrew Channel or Dolphin by itself.

Dolphin starts NAND titles as a Wii does, so it tests the Wii path of
either package, not the vWii one. It also stalls on a boot program whose
sections are not 32-byte sizes, which `channel/loader/loader.ld` pads as
retail ones are.

The installer reloads IOS 58 before it reads RiftWii back in: a reload
overwrites the bottom of MEM2, where the file is read to.

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
  and icon to video from the same data (Pillow, numpy, ffmpeg).

The formats (U8, IMD5, LZ77, TPL, BRLYT, BRLAN, BNS, IMET) were written from
their public descriptions and checked against a retail banner's layout.

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
