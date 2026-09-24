# Third-party notices

RiftWii is licensed under the GNU General Public License v3.0 or later
(`LICENSE`). It links the components below; their licences are compatible
with GPL-3.0-or-later and their notices are reproduced in the vendored files.

| Component | Location | Version / origin | Licence |
| --- | --- | --- | --- |
| pugixml | `vendor-pugixml/` | 1.15, https://github.com/zeux/pugixml | MIT (`vendor-pugixml/LICENSE.md`) |
| libwiigui | `vendor-libgui/source/libwiigui/`, `video.cpp`, `input.cpp`, `audio.cpp`, `gettext.cpp` (not built; `wii/i18n.cpp` replaces it), `filebrowser.cpp`, `menu.cpp` (template) | 1.07 snapshot by Tantric, https://github.com/dborth/libwiigui | GPL (version unspecified upstream; used under GPL-3.0-or-later) |
| FreeTypeGX | `vendor-libgui/source/FreeTypeGX.*` | Armin Tamzarian, bundled with libwiigui | GPL-3.0-or-later (header in file). RiftWii changed `charToWideChar` to decode UTF-8 (for the translations and game names). |
| WiiDRC (Wii U GamePad) | `vendor-libgui/source/wiidrc.*`, `vendor-libgui/source/input.cpp` (GamePad scan) | FIX94, 2017 (MIT header in file) | MIT. Direct-I2C GamePad reads; no IOS calls, safe no-op on plain Wii and under Dolphin. |
| oggplayer | `vendor-libgui/source/oggplayer.*` | Francisco Munoz "Hermes", 2008 | BSD-3-Clause (header in file) |
| PNGU | `vendor-libgui/source/pngu.*` | frontier (frontier-dev.net), modified by Tantric | No licence text in the vendored files; distributed inside the libwiigui package. Confirm or replace before a public release. |
| libwiigui template assets | `vendor-libgui/source/images/`, `fonts/font.ttf`, `sounds/` | Artwork by mvit and music by Peter de Man per the libwiigui README | Part of the libwiigui template; replace with RiftWii-owned assets before a public release. |
| M PLUS Rounded 1c (menu font) | `wii/font/rounded.ttf`, `wii/font/OFL.txt`; embedded brotli-compressed as `wii/assets/menufont.bin` | M+ Fonts Project, Bold weight (Google Fonts `ofl/mplusrounded1c/MPLUSRounded1c-Bold.ttf`), subset by `tools/make_menu_font.py` to Latin with accents, kana, JIS level-1 kanji and the characters of the translations and GameTDB's Japanese titles | SIL Open Font License 1.1 (`wii/font/OFL.txt`). The menu's artwork (tiles, buttons, pointer, bar) is drawn by RiftWii at start (`wii/skin.cpp`); it ships no image files. The Homebrew Channel icon (`hbc/icon.png`) is drawn by `tools/make_hbc_icon.py` with the same font. |
| Brainslug (boot sequence) | `wii/boot.cpp`, `wii/ios_reload.cpp`, `Makefile.wii` (link address) | Alex Chadwick 2014, Florian Bach 2020, https://github.com/Chadderz121/brainslug-wii at commit `8ca49384452dcb7d41e90d002ba0f85b4e57bf57`: `src/apploader/apploader.c`, `src/main.c`, `src/di/di.c` | MIT. The apploader entry/init/main/close protocol, the low-memory fields written before the jump and the loader-at-0x80A00000 link trick were adapted from these files; the code in RiftWii is a rewrite, not a copy. |
| libogc (IOS reload) | `wii/ios_reload.cpp` | libogc `ios.c` (`__IOS_LaunchNewIOS`) by Michael Wiedenbauer, Dave Murphy, Hector Martin, https://github.com/devkitPro/libogc | zlib-style (see the file header). `reload_ios` follows the same IPC handshake so that a zeroed ticket view can be passed under Dolphin. |
| libogc (SD slot, USB Gecko) | `wii/sdio.cpp`, `runtime/resident/rt_hook.c` | libogc `wiisd.c` (Michael Wiedenbauer, Dave Murphy, Sven Peter), `usbgecko.c`, `exi.c`, https://github.com/devkitPro/libogc | BSD-style (see the file headers). The /dev/sdio/slot0 command sequence (reset, status, select, block length, 4-bit bus, clock, CMD18 through the SENDCMD vector form), the SENDCMD request layout and the EXI immediate-transfer / USB Gecko command words follow these files; the code in RiftWii is a rewrite. |
| Gecko code handler | `vendor-gecko/codehandleronly.bin` | GeckoOS code handler (`codehandleronly.s`), as shipped by USB Loader GX (https://github.com/wiidev/usbloadergx at commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`, `vendor-gecko/README.md`) | GPL-2.0-or-later. Loaded into the game at 0x80001800 when cheats are on. |
| GameCube controller adapter (WUP-028) | `runtime/rtgcad.*`, `runtime/pad/`, `wii/padhook.*`, `wii/gcadapter.*` | wup-028-bslug by Alex Chadwick 2017 (https://github.com/Chadderz121/wup-028-bslug), Dolphin's `GCAdapter.cpp` and IOS HID v4/v5 emulation, fakemote (embedded-game-controller), wiibrew's /dev/usb/hid (v4), /dev/usb/hid (v5) and /dev/usb/ven pages | wup-028-bslug is MIT; Dolphin and fakemote are GPL-2.0-or-later, read as documentation. The adapter's init and rumble commands, its 37-byte report layout and the HID request sequences follow these; the driver and the PADRead/PADControlMotor hooks are RiftWii's own code, no text copied. |
| BearSSL | `vendor-bearssl/` (built by `Makefile.bearssl`), used by `wii/tls.cpp` | 0.6, https://bearssl.org (`bearssl-0.6.tar.gz`, SHA-256 `6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`), unmodified | MIT (`vendor-bearssl/LICENSE.txt`) |
| TLS trust anchors | `wii/tlsroots.c` | Sectigo Public Server Authentication Root E46 and R46, USERTrust ECC and RSA Certification Authority, from the Mozilla CA list as shipped with Git for Windows; converted with BearSSL's `brssl ta` | Public certificate data |
| USB Loader GX (game patches) | `src/gamelang.cpp`, `src/videopatch.cpp` (TV format conversion) | USB Loader GX (https://github.com/wiidev/usbloadergx): the SCGetLanguage search pattern and its `li r3,<language>` replacement (`patchcode.c`, `langpatcher`), and the Revolution SDK render mode heights its video mode patcher lists (`gamepatches.c`) | GPL-2.0-or-later. Reimplemented, not copied. |
| d2x-cIOS DIP plugin | `wii/di.cpp`, `include/riftwii/usbgame.hpp`, `src/usbgame.cpp` | public d2x-cIOS `source/dip-plugin/{ioctl.h,frag.h,plugin.c}`; command/layout facts only | GPL-3.0-or-later. F6/F9/FA numbers, DEV_USB=1, DEV_SDHC=2, and the native `{size,num,maxnum}` / `{offset,sector,count}` ABI were independently implemented; no plugin implementation text was copied. |

Data fetched at run time, when the Wii is online and "Download names and
cheats" is on: game names from GameTDB (https://www.gametdb.com, the
`wiitdb.txt` lists), cheat files from the GeckoCodes archive kept by
RiiConnect24 (https://codes.rc24.xyz), and the newest release's tag from
GitHub's API (the update check). Neither is shipped with RiftWii.
RiftWii contains no Nintendo keys or assets.

Build-time dependencies that are not vendored: devkitPPC/libogc, libfat,
libpng, zlib, FreeType, libogg/libvorbisidec, brotli, bzip2 (devkitPro
portlibs), each under its own licence.

## Design provenance

RiftWii is a clean-room implementation. No Riivolution source code was
consulted or copied. Behavioural references used, all publicly available:

- The Riivolution patch-format documentation:
  https://riivolution.github.io/wiki/Patch_Format/
- Dolphin's independent parser/patcher (GPL-2.0-or-later), read for
  behaviour only, at commit `f6a67aa6e722350db3fc778e4f881cb60090faa1`:
  `Source/Core/DiscIO/RiivolutionParser.cpp`, `RiivolutionPatcher.cpp`
- WiiBrew hardware/format pages (`/dev/di`, `/dev/sdio/slot0`, Wii Disc,
  Apploader, Memory map).
- Dolphin's HLE (GPL-2.0-or-later), read for behaviour only, at
  `master-5.0-18995`: `Core/Boot/Boot_BS2Emu.cpp` (low-memory setup),
  `Core/IOS/DI/DI.cpp` (command ranges and reply codes),
  `Core/IOS/ES/ES.cpp` and `Core/IOS/IOS.cpp` (ticket views, IOS launch),
  `Core/HW/WII_IPC.cpp` (IPC acknowledge after a reload).
- Brainslug and libogc as listed in the table above for the Wii boot
  path (`wii/boot.cpp`, `wii/ios_reload.cpp`, `wii/di.cpp`).
