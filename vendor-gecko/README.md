# Gecko code handler

`codehandleronly.bin` is the Gecko code handler without the USB Gecko
debugger: the small PowerPC program that runs every frame inside a game
and applies its enabled cheat codes (Gecko/Ocarina code types). It is
the handler every Wii loader ships (USB Loader GX, WiiFlow, Nintendont
use the same program).

## Source

`codehandleronly.s` is its complete source, and `build.sh` rebuilds the
binary from it and checks the result against `codehandleronly.bin`.
They are the same file, byte for byte (SHA-1
`606e70f30bf66c4d26abd3fdec680000bb07aaef`, 2736 bytes).

- Written for Gecko OS by Nuke and the Gecko authors (brkirch, Link and
  others), 2008.
- The source is `codehandler/codehandleronly.s` from MP2E's Gecko OS
  1.9.3.1 fork (https://github.com/MP2E/Gecko-OS, commit
  `2b717e7b4c77015db3f4ed70f8dba3a16498a0f5`), unmodified. Source SHA-1
  `ad4a37229c4642bbd3e22497f373f61804a5ae65`.
- RiftWii's copy of the binary was taken from USB Loader GX
  (https://github.com/wiidev/usbloadergx, commit
  `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`,
  `source/patches/codehandleronly.h`), converted from its C array to raw
  bytes. USB Loader GX got it from Gecko OS in turn.

## Licence

Neither Gecko OS fork nor USB Loader GX's copy has a licence file or
header for the handler. The Wii loaders that ship it treat it as GNU GPL
software, and RiftWii does the same: it is distributed here under the
GNU General Public License version 2 or later, with its full source
above. `COPYING-GPL-2.0.txt` is the text of that licence. RiftWii as a
whole is GPL-3.0-or-later (`../LICENSE`).

## Use

RiftWii loads it at 0x80001800 (its link address) and puts the code
list at 0x800022A8, the address built into it (`install_cheats` in
`wii/boot.cpp`).
