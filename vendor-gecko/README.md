# Gecko code handler

`codehandleronly.bin` is the Gecko code handler without the USB Gecko
debugger: the small PowerPC program that runs every frame inside a game
and applies its enabled cheat codes (Gecko/Ocarina code types). It is
the handler every Wii loader ships (USB Loader GX, WiiFlow, Nintendont
use the same program).

- Origin: GeckoOS for USB Gecko, Copyright (C) 2008 Nuke and the Gecko
  authors (brkirch, Link and others), GNU GPL version 2 or later.
- Taken from USB Loader GX (https://github.com/wiidev/usbloadergx,
  commit e25c4f3501ed957b7db73f79c51fdf00715ab2e2,
  `source/patches/codehandleronly.h`), converted from its C array to raw
  bytes. 2736 bytes, SHA-1 606e70f30bf66c4d26abd3fdec680000bb07aaef.

RiftWii loads it at 0x80001800 (its link address) and puts the code
list at 0x800022A8, the address built into it (`install_cheats` in `wii/boot.cpp`).
