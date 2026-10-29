# Gecko code handler

`codehandleronly.bin` is the Gecko code handler without the USB Gecko
debugger: the small PowerPC program that runs every frame inside a game
and applies its enabled cheat codes (Gecko/Ocarina code types). It is
the handler every Wii loader ships (USB Loader GX, WiiFlow, Nintendont
use the same program).

`codehandler.bin` and `codehandlerslota.bin` are the same handler with
the WiiRD debugger built in: each frame it also answers a PC on the
other end of a USB Gecko (WiiRD, Gecko dotNET), which can pause the
game, read and write its memory, set breakpoints and send new codes.
`codehandler.bin` talks to a USB Gecko in memory card slot B,
`codehandlerslota.bin` to one in slot A; the two sources differ only in
which EXI channel's registers they use.

## Source

`codehandleronly.s` is its complete source, and `build.sh` rebuilds the
binary from it and checks the result against `codehandleronly.bin`.
They are the same file, byte for byte (SHA-1
`606e70f30bf66c4d26abd3fdec680000bb07aaef`, 2736 bytes). It checks the
debugger handlers the same way:

| Binary | Source | Binary SHA-1 | Source SHA-1 | Bytes |
| --- | --- | --- | --- | --- |
| `codehandler.bin` | `codehandler.s` | `cf5c54c04962672066f6699e82e46125c0ff4bff` | `757cdf7682d43dd6315207f7ca580383f7fc1a4d` | 4288 |
| `codehandlerslota.bin` | `codehandlerslota.s` | `c410504b47070b53f88e3abaf4daa79273488fc9` | `4a3ed8228ac837677693fc639abe249300f42b15` | 4288 |

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
- The debugger handlers' sources are `codehandler/codehandler.s` and
  `codehandler/codehandlerslota.s` from the same Gecko OS commit,
  unmodified. The binaries were taken from USB Loader GX at the same
  commit (`source/patches/codehandler.c`, `codehandlerslota.c`), again
  converted to raw bytes. WiiFlow Lite
  (https://github.com/Fledge68/WiiFlow_Lite, commit
  `69c0d4d61984f4ed6fca51b4520320cadfac960d`,
  `resources/wiiflow_game_booter/source/codehandler.h`) ships the same
  slot B handler, byte for byte.

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
`wii/launch/boot.cpp`).

With Settings > WiiRD debugger on, `install_cheats` loads a debugger
handler there instead, as USB Loader GX's `load_handler` and WiiFlow
Lite's do: slot B's when a USB Gecko answers in slot B, else slot A's
when one answers there, else the plain handler (a debugger handler
polls its slot every frame, which a memory card there would not
expect). Its code list is at 0x800028B8, right after the handler, and
the word at 0x80002774 set to 1 holds the game paused at its first
frame until the PC lets it go ("pause at start").
