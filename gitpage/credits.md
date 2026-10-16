<!-- SPDX-FileCopyrightText: 2026 RiftWii contributors -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Ishtar
Creator 👑
ishtar.png
mc

# JackW
Contributor
jackw.png
mc

# MidyGamy
Contributor
midygamy.png
mc

# DDinghoya
Contributor
ddinghoya.png
mc

# Ｄｉａ！♪～☆
Website
dia.png
mc

<!-- writing mc under an entry shows it when the list is condensed -->

# pugixml
License: MIT (`vendor-pugixml/LICENSE.md`)

# libwiigui
License: GPL (version unspecified upstream; used under GPL-3.0-or-later). Three of RiftWii's files grew from the template and name Tantric as a holder: `wii/menu/rift_menu.cpp` (from `menu.cpp`: the GUI thread with `HaltGui`/`ResumeGui`/`UpdateGUI`, `MainMenu` and the menu loops), `wii/main.cpp` (from `demo.cpp`: the start-up order and `ExitApp`) and `wii/menu/skin.cpp` (`DrawRgb5a3`, after `Menu_DrawImg` in `video.cpp`).

# FreeTypeGX
License: GPL-3.0-or-later (header in file). RiftWii changed `charToWideChar` to decode UTF-8 (for the translations and game names), adds only the FreeType modules the menu font needs, and lets `InitFreeType` take a font collection's face and report a font it can't read (Settings > Menu font), and adds `FontHasChar` (a Korean menu needs a font with Hangul).

# WiiDRC (Wii U GamePad)
License: MIT. Direct-I2C GamePad reads; no IOS calls, safe no-op on plain Wii and under Dolphin.

# oggplayer
License: BSD-3-Clause (header in file)

# PNGU
License: No license text in the vendored files; distributed inside the libwiigui package. Not compiled: `Makefile.wii` excludes `pngu.c`, and the covers read PNGs with RiftWii's own `src/art/pngdecode.cpp`.

# libwiigui template assets
License: In the source tree as part of the libwiigui package, but none of them is in the program: RiftWii's menu draws its own artwork (`wii/menu/skin.cpp`) and uses its own font (below). The menu's hover and press sounds (`sounds/button_over.pcm`, `button_click.pcm`) are no longer the template's: `tools/make_sounds.py` makes them from sine waves. `bg_music.ogg` is not linked into the program.

# RiftWii channel boot program (design)
License: The two-stage layout (a small boot program with a section at `0x80003400` and a virtual entry, which reads the real program from another content) and the CPU set-up follow these, and so does the L2 invalidate in both `start.S` files (HID4 L2CFI off, L2I, wait for L2IP); the code is RiftWii's own, no text copied.

# Menu music: "Insect Factory (Wii-style music)"
License: CC0 1.0 (public domain dedication). Credited here as thanks; CC0 does not require it.

# Korean translation
License: GPL-3.0-or-later, contributed to RiftWii.

# French translation
License: GPL-3.0-or-later, contributed to RiftWii.

# M PLUS Rounded 1c (menu font)
License: SIL Open Font License 1.1 (`wii/font/OFL.txt`). The menu's artwork (tiles, buttons, pointer, bar) is drawn by RiftWii at start (`wii/menu/skin.cpp`); it ships no image files. The Homebrew Channel icons (`hbc/icon.png`, `channel/installer/hbc/icon.png`) and the RiftWii channel's banner, icon, start screen and sound (`channel/art`, `channel/forwarder/data`) are drawn by `tools/make_channel_art.py` and `tools/make_channel.py` with the same font.

# Kenney Input Prompts (controller and button pictures)
License: CC0 1.0 (public domain dedication); credited as the author asks, though it is not required. The About page's controllers, the button prompts' badges and the Controllers tile use them.

# Brainslug (boot sequence)
License: MIT. The apploader entry/init/main/close protocol, the low-memory fields written before the jump and the loader-at-0x80A00000 link trick were adapted from these files; the code in RiftWii is a rewrite, not a copy.

# libogc (IOS reload)
License: zlib-style. Reimplemented, no text copied; the file names its holders in `SPDX-FileCopyrightText` lines. `reload_ios` follows the same IPC handshake so that a zeroed ticket view can be passed under Dolphin.

# libogc (SD slot, USB Gecko)
License: BSD-style. Reimplemented, no text copied; the files name their holders in `SPDX-FileCopyrightText` lines. The /dev/sdio/slot0 command sequence (reset, status, select, block length, 4-bit bus, clock, CMD18 through the SENDCMD vector form), the SENDCMD request layout and the EXI immediate-transfer / USB Gecko command words follow these files; the code in RiftWii is a rewrite.

# Virtual SD card
License: Dolphin was read as documentation, for behaviour only: host controller registers read back as written, RESETCARD answering the card's address, the card event left pending. The CID, CSD and SCR layouts and CRC7 follow the specification. The card model, the blob and its hooks are RiftWii's own code, no text copied.

# Zstandard (decoder)
License: BSD-3-Clause (`vendor-zstd/LICENSE`; zstd is dual BSD/GPLv2, used under BSD)

# Gecko code handler
License: GPL-2.0-or-later (`vendor-gecko/COPYING-GPL-2.0.txt`; neither upstream has a license file, see the README). Loaded into the game at 0x80001800 when cheats are on.

# GameCube controller adapter (WUP-028)
License: wup-028-bslug is MIT; Dolphin and fakemote are GPL-2.0-or-later, read as documentation. Nintendont is GPL-2.0; its developers allow RiftWii to use its code with attribution, given here. The adapter's init and rumble commands, its 37-byte report layout and the HID request sequences follow these; the driver and the PADRead/PADControlMotor hooks are RiftWii's own code, no text copied.

# BearSSL
License: MIT (`vendor-bearssl/LICENSE.txt`)

# TLS trust anchors
License: Public certificate data

# WiiLink WFC launcher pieces
License: GPL-2.0-or-later, offered beside that project's own license (`vendor-wwfc/LICENSE`)

# USB Loader GX (online servers, return to channel)
License: GPL-3.0 (USB Loader GX's license, per its maintainers; `gamepatches.c` carries no header of its own). The Wiimmfi blob is copied unchanged; the rest reimplemented. Each file that follows it carries `SPDX-FileCopyrightText` lines for its holders.

# USB Loader GX (game patches)
License: `patchcode.c` GPL-2.0-or-later; `gamepatches.c` GPL-3.0. Reimplemented, not copied. Each file that follows it carries `SPDX-FileCopyrightText` lines for its holders.

# USB Loader GX (per-game fixes, cIOS choice)
License: GPL-3.0 (USB Loader GX's license, per its maintainers). The data is copied unchanged; the code reimplemented. Each file carries `SPDX-FileCopyrightText` lines for its holders.

# USB Loader GX (d2x USB drive)
License: zlib. Reimplemented: only the ioctl numbers and their order are taken. The file carries `SPDX-FileCopyrightText` lines for its holders.

# USB Loader GX (RiiTag)
License: zlib (Dimok's header in that file). Reimplemented: RiftWii reads the file with pugixml and sends with its own HTTP client; no text copied. Each file carries `SPDX-FileCopyrightText` lines for its holders.

# USB Loader GX (Wii Menu font)
License: GPL-3.0-or-later (that file's header). The lookup and the U8 reader are reimplemented. The font itself is Nintendo's: RiftWii reads it from the player's own Wii while the menu runs and never ships or stores it. Each file carries `SPDX-FileCopyrightText` lines for its holders.

# d2x-cIOS DIP plugin
License: GPL-3.0-or-later. F6/F9/FA numbers, DEV_USB=1, DEV_SDHC=2, and the native `{size,num,maxnum}` / `{offset,sector,count}` ABI were independently implemented; no plugin implementation text was copied.

# Friivolution launch argument (interface)
License: Read as an interface description so loaders can start RiftWii the way they start Friivolution; RiftWii's reader is its own code, nothing copied. Friivolution is a Riivolution fork: none of its other code was read.

# libwbfs (WBFS layout)
License: GPL-2.0. The header fields, the disc table, the disc-info size, the free-block table's place in the first block and the slot count it leaves were independently implemented; no libwbfs text was copied.

# Our Amazing Community
Everyone who has helped support RiftWii
mc