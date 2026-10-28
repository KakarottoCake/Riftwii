# Third-party notices

RiftWii is licensed under the GNU General Public License v3.0 or later
(`LICENSE`). It links the components below; their licenses are compatible
with GPL-3.0-or-later and their notices are reproduced in the vendored files.
RiftWii's own files that follow someone else's code name each holder in
an `SPDX-FileCopyrightText` line at the top; this table has the detail
(which file, which functions, which commit).

| Component | Location | Version / origin | License |
| --- | --- | --- | --- |
| Wii Menu sequence envelope and mixing behavior | `src/disc/wiisequence.cpp` | [OGWS community decompilation](https://github.com/doldecomp/ogws/tree/e5a4d369011062fe243aa63f6c2e2853854c395d), commit `e5a4d369011062fe243aa63f6c2e2853854c395d`, `src/nw4r/snd/snd_{EnvGenerator,Util,Channel,SeqTrack,Bank,SoundArchivePlayer,BasicSound}.cpp`: envelope units and the attack rule below the fastest settings, 3 ms updates and amplitude ramps, squared sequence volumes, linear bank/archive volumes, and signed pan range. | Project CC0-1.0; retail behavior read as documentation only. No source text or console tables copied: the fastest attack settings (109 to 127) follow RiftWii's own rule, and RiftWii generates its own amplitude lookup table and uses its own fixed voice renderer. Checked against the Wii Menu recorded in Dolphin. |
| Channels moved to the SD card by the Wii Menu (`content.bin` layout; format reference) | `include/riftwii/contentbin.hpp`, `src/data/contentbin.cpp`, `wii/storage/movedchannels.cpp`, `wii/menu/otpkey.cpp` | DarkMatterCore's wad2bin commit `6cda54f38286fa6b266887733a7c76cb36b7ec70`, `source/bin.{c,h}` and `source/wad.h` (parts A to F, the "Bk" header and its included-contents bitfield, contents encrypted with the console's PRNG key and the content index as IV); Yet Another BlueDump Mod (`DarkMatterCore/bluedump-mod`) commit `fc41ee1c5f9b6e2b86448c4723b367849ee15081`, `source/yabdm.c` and `source/otp.{c,h}` (finding the "Bk" header without the SD key, shared contents left out, the PRNG key and console ID in OTP, another Wii's key from BootMii's keys.bin) | GPL-3.0-or-later. Read as documentation; no source text copied. RiftWii independently finds the header by its backup size, reads only the plain parts and the console-key contents, and stages the contents in the title's NAND folder as the Wii Menu does. |
| Direct channel boot context and SD mount lifetime (behavior references) | `wii/launch/boot.cpp`, `include/riftwii/nandchannel.hpp`, `src/data/nandchannel.cpp`, `wii/storage/d2xsave.cpp` | USB Loader GX commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`, `source/Channels/channels.cpp` (`LoadChannel`: zero BI2 and expected IOS fields), `source/Controls/DeviceHandler.cpp` and `PartitionHandle.cpp` (SD unmount destroys the handle and shuts its interface down); WiiFlow Lite commit `69c0d4d61984f4ed6fca51b4520320cadfac960d`, `resources/wiiflow_game_booter/source/ChannelHandler.cpp` (`LoadChannel`: BI2 and channel application type); d2x cIOS commit `33ad1eeeb8f562df99e7d7ca428fdd36e0e31be7`, `source/fat-module/{main.c,fat_wrapper.c,fs.h}` (SD mount ABI and generic FAT error -101); Dolphin commit `e1d35f666cc35f6ead55d32ea2679df44d04da72`, `Source/Core/Core/IOS/ES/ES.cpp` (title identity and UID/GID behavior) | USB Loader GX and WiiFlow channel handlers: zlib headers, projects GPL-3.0; d2x GPL-3.0; Dolphin GPL-2.0-or-later. Read as documentation; no source text copied. RiftWii independently reserves BI2 outside its program, BSS, loader and extras, restores its pointer after shutdown, supplies channel boot fields, and releases its SD interface before d2x mounts. |
| ES handle lifetime (behavior references) | `wii/launch/boot.cpp`, `wii/launch/resident.*`, `runtime/resident/rt_hook.*` | USB Loader GX at commit `c14c1bdd10ca60c7f30d26034eba489289f890e2`, `source/usbloader/GameBooter.cpp` (`GameBooter::BootGame`: closes the temporary ES handle after reload-block/return-to setup); Nintendont at commit `09a072e54b93371765f5d5b9f222b2f25ca987fb`, `loader/source/main.c` (`main`: closes libogc's ES handle before opening ES for an immediate kernel transition); Dolphin at commit `2c805fee8f232d090c454381bb1b8422242345e1`, `Source/Core/Core/IOS/ES/ES.h` and `ES.cpp` (three ES contexts; opens fail when all are active) | USB Loader GX GPL-3.0-or-later, Nintendont used with its developers' permission, Dolphin GPL-2.0-or-later. Read as documentation only; no source text copied. RiftWii opens ES only when its own Reset hook runs and closes it if the transition fails. |
| libasnd (streaming API behavior reference) | `wii/menu/wiimenusounds.cpp` | devkitPro/libogc `libasnd/asndlib.c` at commit `959871c08ed80d1a63b258bc207218623ea1b0e5` (https://github.com/devkitPro/libogc/blob/959871c08ed80d1a63b258bc207218623ea1b0e5/libasnd/asndlib.c), Hermes: pending SetVoice/AddVoice updates and readiness API behavior | BSD-3-Clause. Read to clarify the API only; no source text copied. RiftWii retains a prepared PCM block and retries transient mixer refusal with a bounded timeout. |
| libogc (physical memory card protocol reference) | `wii/storage/gcrealcard.cpp`, `include/riftwii/gcreal.hpp`, `src/fs/gcreal.cpp`, `plugins/gamecube/gcnative.cpp` | devkitPro/libogc `libogc/card.c` and `libogc/exi.c` at v3.1.0, commit `c70bdf2fc36bf6c6c12d946ae3ad70d604366d1b` (https://github.com/devkitPro/libogc/blob/c70bdf2fc36bf6c6c12d946ae3ad70d604366d1b/libogc/card.c): CARD mount/unlock and SRAM behavior, read command/address fields and card-ID read latency, the MEM1-only EXI DMA address mask and EXI lock/detach callbacks; Michael Wiedenbauer and Dave Murphy | zlib-style license (the installed libogc header). Interface/protocol facts only; RiftWii's synchronous read-only adapter is its own implementation, with no save-manager source used or source text copied. |
| Dragon Quest X's WFS drive (format reference) | `include/riftwii/wfs.hpp`, `src/fs/wfs.cpp`, `include/riftwii/wfstitle.hpp`, `src/fs/wfstitle.cpp`, `wii/storage/wfsdrive.cpp`, `tests/wfs_image.hpp` | koolkdev's wfslib commit `5d440ceec7d0c633bbb4f542996b690ef5ae0518` (https://github.com/koolkdev/wfslib: block, area, quota, directory tree and file layouts, the block encryption IV and hashes) and wfs-tools commit `98b5979d9652e6b51cd240e5a7dfa5289cbc9303` (https://github.com/koolkdev/wfs-tools); Dolphin commit `22b581c670d84e60d11c1b02d38e1c7789ae2dfa`, `Source/Core/Core/IOS/WFS/` and `Source/Core/Core/IOS/VersionInfo.cpp` (IOS59's WFS devices, the installed title's layout on the drive) | wfslib and wfs-tools MIT, Dolphin GPL-2.0-or-later. Read as documentation; no source text copied. RiftWii's reader is its own, read-only, and was checked against wfslib on drives wfslib wrote. |
| pugixml | `vendor-pugixml/` | 1.15, https://github.com/zeux/pugixml | MIT (`vendor-pugixml/LICENSE.md`) |
| libwiigui | `vendor-libgui/source/libwiigui/`, `video.cpp`, `input.cpp`, `audio.cpp`, `gettext.cpp` (not built; `wii/core/i18n.cpp` replaces it), `filebrowser.cpp`, `menu.cpp` (template) | 1.07 snapshot by Tantric, https://github.com/dborth/libwiigui | GPL (version unspecified upstream; used under GPL-3.0-or-later). Three of RiftWii's files grew from the template and name Tantric as a holder: `wii/menu/rift_menu.cpp` (from `menu.cpp`: the GUI thread with `HaltGui`/`ResumeGui`/`UpdateGUI`, `MainMenu` and the menu loops), `wii/main.cpp` (from `demo.cpp`: the start-up order and `ExitApp`) and `wii/menu/skin.cpp` (`DrawRgb5a3`, after `Menu_DrawImg` in `video.cpp`). RiftWii's build has no C++ exceptions, so `GuiWindow` and `GuiKeyboard` (`gui_window.cpp`, `gui_keyboard.cpp`) check the element list's length in their loops instead of catching `std::out_of_range` from `_elements.at()` (an element's handler can remove others mid-loop). |
| FreeTypeGX | `vendor-libgui/source/FreeTypeGX.*` | Armin Tamzarian, bundled with libwiigui | GPL-3.0-or-later (header in file). RiftWii changed `charToWideChar` to decode UTF-8 (for the translations and game names), adds only the FreeType modules the menu font needs, and lets `InitFreeType` take a font collection's face and report a font it can't read (Settings > Menu font), adds `FontHasChar` (a Korean menu needs a font with Hangul), and adds `RenderTextCoverage`, which draws text into a plain bitmap through a size of its own, centred or left-aligned and narrowed for a 16:9 TV (a GameCube game's crash screen, drawn without GX), tells the menu's GX trace of each glyph it draws (`Menu_TextureHook`, settings.txt `debug_gx`), and adds `SetMissingGlyphSource`: a character the face lacks is asked of a second source (the full menu font on the SD card, `wii/menu/fullfont.cpp`) and cached like the face's glyphs, its texture from that source's memory. The bitmap source's glyphs (the Wii Menu's font) are made at the screen's own pixels and drawn texel for pixel from a whole pixel (`Menu_GlyphPixels`, `Menu_SnapToPixel` in `video.cpp`), with float vertex positions, so a 16:9 menu's narrower units don't resample them; glyphs made again after a display change free their old textures once the frame is drawn (`FreeRetiredGlyphs`). |
| WiiDRC (Wii U GamePad) | `vendor-libgui/source/wiidrc.*`, `vendor-libgui/source/input.cpp` (GamePad scan) | FIX94, 2017 (MIT header in file) | MIT. Direct-I2C GamePad reads; no IOS calls, safe no-op on plain Wii and under Dolphin. |
| oggplayer | `vendor-libgui/source/oggplayer.*` | Francisco Munoz "Hermes", 2008 | BSD-3-Clause (header in file) |
| PNGU | `vendor-libgui/source/pngu.*` | frontier (frontier-dev.net), modified by Tantric | No license text in the vendored files; distributed inside the libwiigui package. Not compiled: `Makefile.wii` excludes `pngu.c`, and the covers read PNGs with RiftWii's own `src/art/pngdecode.cpp`. |
| libwiigui template assets | `vendor-libgui/source/images/`, `fonts/font.ttf`, `sounds/` | Artwork by mvit and music by Peter de Man per the libwiigui README | In the source tree as part of the libwiigui package, but none of them is in the program: RiftWii's menu draws its own artwork (`wii/menu/skin.cpp`) and uses its own font (below). The menu's hover and press sounds and the two HOME Menu sounds (`sounds/button_over.pcm`, `button_click.ogg`, `home_open.ogg`, `home_close.ogg`) are no longer the template's: `tools/make_sounds.py` makes them from sine waves. The press and HOME sounds are stored as Ogg Vorbis and decoded into MEM2 at start by `wii/menu/menusounds.cpp`; RiftWii changed `vendor-libgui/source/filelist.h` (the Ogg arrays, and the decoded `*_pcm` pointers it declares) and `libwiigui/gui_sound.cpp` (a sound that was not decoded plays nothing; replacements are resolved at playback so Settings can change the source live) for that. `bg_music.ogg` is not linked into the program. |
| RiftWii channel boot program (design) | `channel/loader/`, `channel/forwarder/start.S` | FIX94's tiny vWii NAND Loader (https://github.com/FIX94/tiny-vwii-nand-loader, GPL-2.0), DacoTaco's OpenDolBoot (https://github.com/DacoTaco/priiloader, `tools/OpenDolBoot`, GPL-2.0), the Homebrew Channel's channel stub and start-up (https://github.com/fail0verflow/hbc, `wiipax/stub`, `channel/channelapp/stub/crt0.S`, GPL-2.0), libogc's IPC layer, libogc's L2 start-up (`tuxedo/early_init.S` `PPCEarlyInit` at v3.1.0 and v3.2.0, `libogc/cache_asm.S` `L2GlobalInvalidate` at v1.8.11, https://github.com/devkitPro/libogc, zlib-style), read as documentation | The two-stage layout (a small boot program with a section at `0x80003400` and a virtual entry, which reads the real program from another content) and the CPU set-up follow these, and so does the L2 invalidate in both `start.S` files (HID4 L2CFI off, L2I, wait for L2IP); the code is RiftWii's own, no text copied. |
| Packed DOL (design) | `runtime/dolpack/`, `tools/pack_dol.py` | FIX94's dolxz (https://github.com/FIX94/dolxz, commit `bf73653618facd06c1f5c98e81215d2298aefc40`, MIT), read as documentation | The idea of a DOL whose sections are compressed behind a small stub that unpacks them at start follows dolxz; the stub, the table format, the packer and the move of the command line are RiftWii's own, with Zstandard (the vendored decoder) where dolxz uses xz. No text copied. |
| Menu music: "Insect Factory (Wii-style music)" | `hbc/music.ogg`, shipped as `sd:/apps/riftwii/music.ogg` | Zane Little Music, from the album "Another Bag of Chips" (https://opengameart.org/content/insect-factory-wii-style-music), converted from the WAV to Ogg Vorbis (quality 2) | CC0 1.0 (public domain dedication). Credited here as thanks; CC0 does not require it. |
| Korean translation | `wii/lang/ko.po`, from the `KO` table in `tools/lang_source.py` | DDinghoya (pull requests 20 and 24) | GPL-3.0-or-later, contributed to RiftWii. |
| French translation | `wii/lang/fr.po`, from the `FR` table in `tools/lang_source.py` | MidyGamy (pull request 22) | GPL-3.0-or-later, contributed to RiftWii. |
| M PLUS Rounded 1c (menu font) | `wii/font/rounded.ttf`, `wii/font/core.ttf`, `wii/font/OFL.txt`; the core embedded zstd-compressed (`Makefile.wii`), the whole font shipped as `apps/riftwii/font.ttf` (`tools/make_release_zip.py`) | M+ Fonts Project, Bold weight (Google Fonts `ofl/mplusrounded1c/MPLUSRounded1c-Bold.ttf`), subset by `tools/make_menu_font.py` to Latin with accents, kana, JIS level-1 kanji and the characters of the translations and GameTDB's Japanese titles, its outline points rounded to multiples of 8 font units; `core.ttf` is the subset of that below U+3000, U+3000-U+30FF, U+FF00-U+FFEF and the menu's text | SIL Open Font License 1.1 (`wii/font/OFL.txt`). The menu's artwork (tiles, buttons, pointer, bar) is drawn by RiftWii at start (`wii/menu/skin.cpp`); it ships no image files. The Homebrew Channel icons (`hbc/icon.png`, `channel/installer/hbc/icon.png`) and the RiftWii channel's banner, icon, start screen and sound (`channel/art`, `channel/forwarder/data`) are drawn by `tools/make_channel_art.py` and `tools/make_channel.py` with the same font. |
| Noto Sans KR (Korean menu font) | `wii/font/korean.ttf`, `wii/font/Korean-OFL.txt`; shipped beside the DOL, never embedded | Adobe and the Noto authors, version 2.004-H2, Google Fonts [commit b38c5c93af322c45f633e17ac440ec1e6c94d489](https://github.com/google/fonts/tree/b38c5c93af322c45f633e17ac440ec1e6c94d489/ofl/notosanskr). `tools/make_korean_font.py` fixes weight 700, keeps KS X 1001 Hangul, Jamo and the Korean translation and GameTDB titles, removes hinting and layout tables, rounds to 8 units per 1000 em and removes implied outline points; renamed RiftWii Korean. | SIL Open Font License 1.1 (`wii/font/Korean-OFL.txt`, shipped next to the font). |
| Kenney Input Prompts (controller and button pictures) | `vendor-kenney/input-prompts/` (14 PNGs and the pack's `License.txt`); made into alpha masks in `src/art/inputicons_data.cpp` by `tools/make_input_icons.py`, drawn in the theme's colour by `src/art/inputicons.cpp` | Kenney (www.kenney.nl), Input Prompts 1.5 (https://kenney.nl/assets/input-prompts, `kenney_input-prompts_1.5.zip`): the Wii Remote, Nunchuk, Classic Controller, Classic Controller Pro and GameCube controller pictures and the Wii Remote's button pictures | CC0 1.0 (public domain dedication); credited as the author asks, though it is not required. The About page's controllers, the button prompts' badges and the Controllers tile use them. |
| Age rating marks (ESRB, PEGI, CERO, GRAC, GSRR) | `wii/assets/rating_*.png` (27 PNGs, 64 pixels high), embedded by `Makefile.wii` and drawn on a game's About page by `wii/menu/ratingbadge.cpp` | The rating boards' current marks as drawn on Wikimedia Commons (https://commons.wikimedia.org): "ESRB 2013 Early Childhood/Everyone/Everyone 10+/Teen/Mature/Adults Only 18+/Rating Pending.svg", "PEGI 3/6/7/12/16/18.svg", "CERO A/B/C/D/Z.svg", "GRAC All (전체이용가)/12 (12세이용가)/15 (15세이용가)/19 (19청소년이용불가).svg" and "GSRR G/P/PG 12/PG 15/R logo.svg", made into PNGs with resvg at 64 pixels high, padded to a multiple of 4 wide (GX textures) and compressed with optipng | Public domain on Wikimedia Commons (simple shapes and text). The marks are the trademarks of the ESRB, PEGI S.A., CERO, the GRAC and the TESRI; RiftWii shows them only to say how a game is rated. |
| Brainslug (boot sequence) | `wii/launch/boot.cpp`, `wii/launch/ios_reload.cpp`, `Makefile.wii` (link address) | Alex Chadwick 2014, Florian Bach 2020, https://github.com/Chadderz121/brainslug-wii at commit `8ca49384452dcb7d41e90d002ba0f85b4e57bf57`: `src/apploader/apploader.c`, `src/main.c`, `src/di/di.c` | MIT. The apploader entry/init/main/close protocol, the low-memory fields written before the jump and the loader-at-0x80A00000 link trick were adapted from these files; the code in RiftWii is a rewrite, not a copy. |
| libogc (IOS reload) | `wii/launch/ios_reload.cpp` | libogc `ios.c` (`__IOS_LaunchNewIOS`) by Michael Wiedenbauer, Dave Murphy, Hector Martin, https://github.com/devkitPro/libogc | zlib-style. Reimplemented, no text copied; the file names its holders in `SPDX-FileCopyrightText` lines. `reload_ios` follows the same IPC handshake so that a zeroed ticket view can be passed under Dolphin. |
| libogc (SD slot, USB Gecko) | `wii/storage/sdio.cpp`, `runtime/resident/rt_hook.c` | libogc `wiisd.c` (Michael Wiedenbauer, Dave Murphy, Sven Peter; SD v2 fallback audited at commit `959871c08ed80d1a63b258bc207218623ea1b0e5`), `usbgecko.c`, `exi.c`, https://github.com/devkitPro/libogc | BSD-style. Reimplemented, no text copied; the files name their holders in `SPDX-FileCopyrightText` lines. The /dev/sdio/slot0 command sequence (reset, status, select, block length, 4-bit bus, clock, CMD18 through the SENDCMD vector form), the SENDCMD request layout and the EXI immediate-transfer / USB Gecko command words follow these files. When IOS cannot initialise an inserted SD v2 card, the raw client follows libogc's reopen, host reset/power/clock setup and CMD0/8/55/41/2/3 identification, with bounded polling and validation of the voltage/check pattern and RCA. The code in RiftWii is a rewrite. |
| Virtual SD card | `runtime/rtvsd.*`, `runtime/vsd/`, `wii/hooks/vsdhook.*`, `wii/hooks/vsdimage.*` | wiibrew's /dev/sdio/slot0 page, libogc `wiisd.c` (as in the row above), Dolphin's `SDIOSlot0` (https://github.com/dolphin-emu/dolphin, GPL-2.0-or-later), the SD Association's SD Physical Layer Simplified Specification | Dolphin was read as documentation, for behaviour only: host controller registers read back as written, RESETCARD answering the card's address, the card event left pending. The CID, CSD and SCR layouts and CRC7 follow the specification. The card model, the blob and its hooks are RiftWii's own code, no text copied. |
| GameCube controller lifecycle (behaviour reference) | `src/patching/symsearch.cpp`, `runtime/gc/gc_ports.h` | projectPiki's Pikmin 2 decompilation (https://github.com/projectPiki/pikmin2, commit `999140f3521e22619547d5d61862a6577ea73882`), `src/Dolphin/pad/Pad.c` and `src/Dolphin/si/SIBios.c`; YAGCD's SI hardware documentation (https://hitmen.c02.at/files/yagcd/yagcd/chap5.html) | The reference repository declares CC0-1.0. Read as documentation only to check the SDK's disabled-port, reset and SI type-cache behaviour; no implementation copied or linked. The structural finder was developed against the player's F-Zero GX and Melee executable dumps; the port reservation and test fixtures are RiftWii's own code. |
| Zstandard (decoder) | `vendor-zstd/` (`zstddeclib.c`, `zstd.h`, `zstd_errors.h`) | 1.5.7, https://github.com/facebook/zstd; `zstddeclib.c` generated by its `build/single_file_libs/combine.py` | BSD-3-Clause (`vendor-zstd/LICENSE`; zstd is dual BSD/GPLv2, used under BSD). Local change: optional `RIFTWII_ZSTD_CHECKPOINT` calls in Huffman literal loops and every 32 sequences let the GameCube I/O worker return from its alarm during decoding; disabled in all other builds. |
| Gecko code handler | `vendor-gecko/codehandleronly.bin`, its source `vendor-gecko/codehandleronly.s` | Gecko OS (Nuke, brkirch, Link and the Gecko authors, 2008). Binary as shipped by USB Loader GX (https://github.com/wiidev/usbloadergx at commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`); source from MP2E's Gecko OS 1.9.3.1 fork (https://github.com/MP2E/Gecko-OS at commit `2b717e7b4c77015db3f4ed70f8dba3a16498a0f5`), unmodified. `vendor-gecko/build.sh` rebuilds the binary from the source, byte for byte (`vendor-gecko/README.md`) | GPL-2.0-or-later (`vendor-gecko/COPYING-GPL-2.0.txt`; neither upstream has a license file, see the README). Loaded into the game at 0x80001800 when cheats are on. |
| GameCube controller adapter (WUP-028) | `runtime/rtgcad.*`, `runtime/pad/`, `wii/hooks/padhook.*`, `wii/controllers/gcadapter.*` | wup-028-bslug by Alex Chadwick 2017 (https://github.com/Chadderz121/wup-028-bslug), Dolphin's `GCAdapter.cpp` and IOS HID v4/v5 emulation, fakemote (embedded-game-controller), Nintendont's USB HID driver (FIX94 and contributors, https://github.com/FIX94/Nintendont, `kernel/HID.c`), wiibrew's /dev/usb/hid (v4), /dev/usb/hid (v5) and /dev/usb/ven pages | wup-028-bslug is MIT; Dolphin and fakemote are GPL-2.0-or-later, read as documentation. Nintendont is GPL-2.0; its developers allow RiftWii to use its code with attribution, given here. The adapter's init and rumble commands, its 37-byte report layout and the HID request sequences follow these; the driver and the PADRead/PADControlMotor hooks are RiftWii's own code, no text copied. |
| libwiiinput (USB keyboard, pen tablets, uDraw GameTablet) | `wii/controllers/usbkeyboard.*`, `wii/controllers/pentablet.*`, `wii/controllers/tablethid.*`, `wii/controllers/udraw.*`, `wii/controllers/usbinput.*` | quatric's libwiiinput, https://github.com/quatric/libwiiinput at commit `14f2003` (`libinput_keyboard.c`, `libinput_tablet.c`, `libinput_hidparse.*`, `libinput_udraw.c`, `input_tablet.h`); its uDraw decoding follows WiiBrew, Dolphin's emulation and rvlpaint by cmyksoda | GPL-3.0-or-later here, given to RiftWii by libwiiinput's author (the library itself is AGPL-3.0). The tablet, descriptor parser and uDraw files are its code with RiftWii's changes (the GameCube adapter left out of the tablet scan and of libogc's HID lists); the keyboard driver follows its own on libogc's libwiikeyboard; `usbinput.cpp` is RiftWii's. |
| GameCube games in Wii mode (RiftWii's own loader, trial) | `runtime/gc/`, `plugins/gamecube/gcnative.cpp`, `wii/launch/gclaunch.*`, `wii/launch/gcplugin.*`, `wii/launch/gcpatch.*`, `gc_card_format` in `src/fs/gccard.cpp` | Nintendont (FIX94 and contributors, https://github.com/FIX94/Nintendont, master as of 2026-10-10) `kernel/Patch.c`: its DSP microcode patches, i.e. which sound programs (recognised by their first words, length and SHA-1), where their sample address is loaded and the free words used, and the `ori $AC0.M,#0x1000` that points the accelerator at MEM2; libogc (https://github.com/devkitPro/libogc) `card.c` (the formatted card's header, directory and block table layout and their checksums), `system.c` (the SRAM copies, and the order its audio shutdown halts and resets the DSP in, which the restart's audio reset follows), `video.c` (the order its VI start-up writes the registers in, and how its black screen moves the active lines into the blanking, which the hand-over to the next title follows) and lwbt (the order the Bluetooth controller is started in); Swiss (emu_kidid and contributors, https://github.com/emukidid/swiss-gc) for the idea of serving a game's `DVDLow*` functions from the main processor, and for lowering a bi2.bin simulated memory size above 24 MB (`swiss.c`/`patcher.c`, read for behaviour); yagcd, wiibrew, the Bluetooth Core specification and Dolphin (`StreamADPCM`, the memory card device) as documentation; the doldecomp Paper Mario: The Thousand-Year Door decompilation's reconstruction of the SDK's `OSExec.c`, `OSReboot.c`, `OS.c` and `__start.c` (https://github.com/doldecomp/ttyd, `libs/dolsdk2004/src/os/`, commit `62131fc31866f85a12e4817a8dc37f0c25eb5f8c`, MIT), read as documentation for how a game switches to another program on its disc (`__OSBootDolSimple`'s arguments and its "2004/02/01" apploader date, the `OSExecParams` block at 0x800030F0, the argument block in BI2) | Nintendont is GPL-2.0; its developers allow RiftWii to use its code with attribution, given here. The DSP patch table's values come from it; the patching code is RiftWii's own. The AX programs' rewrite (`src/patching/gcdsp.cpp`, which code moves where and the format-dependent MEM2 bit) re-implements `PatchAX_Dsp` step for step under that permission, and was checked against it byte for byte on Mario Party 4's program. libogc is zlib/BSD-style and Swiss and Dolphin are GPL-2.0; they were read for layouts and behaviour only, no text copied. |
| BearSSL | `vendor-bearssl/` (built by `Makefile.bearssl`), used by `wii/online/tls.cpp` | 0.6, https://bearssl.org (`bearssl-0.6.tar.gz`, SHA-256 `6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14`), unmodified | MIT (`vendor-bearssl/LICENSE.txt`) |
| TLS trust anchors | `wii/online/tlsroots.c` | Sectigo Public Server Authentication Root E46 and R46, USERTrust ECC and RSA Certification Authority, and ISRG Root X1 (Let's Encrypt, which release downloads redirect to), from the Mozilla CA list as shipped with Git for Windows; converted with BearSSL's `brssl ta` | Public certificate data |
| WiiLink WFC launcher pieces | `vendor-wwfc/` (used by `wii/launch/wfc.cpp`) | WiiLink's wfc-patcher-wii (https://github.com/WiiLink24/wfc-patcher-wii at commit `0d9d7a697d7fb299ecc629f2c5da071d116369f2`, `launcher/source`): the hook code, the game address table and the downloader payload; renamed, otherwise unmodified (`vendor-wwfc/README.md`). Online communications credit to WiiLink WFC - https://wfc.wiilink.ca | GPL-2.0-or-later, offered beside that project's own license (`vendor-wwfc/LICENSE`) |
| USB Loader GX (channel identity) | `src/data/nandchannel.cpp` (`make_fake_ticket`), `wii/storage/nandchannels.cpp`, `wii/launch/boot.cpp` | Channel identification and the synthetic RSA-2048 ticket layout from `source/Channels/channels.cpp` (`Identify_GenerateTik`, `Channels::Identify`), audited at commit `92af55325c368f9a9a24566a0c93142fd0df6099` (https://github.com/wiidev/usbloadergx). Copyright 2010 dude, 2011 Miigotu and Dimok | zlib (the source file's notice); USB Loader GX as a whole is GPL-3.0-or-later. Reimplemented in RiftWii, not an upstream source copy. Installed tickets are preferred; acceptance of synthetic tickets on any particular IOS is not claimed. The unverified channel low-memory and separate d2x changes from this PR were removed. |
| USB Loader GX (online servers, return to channel) | `src/patching/wfcpatch.cpp`, `wii/launch/wfc.cpp`, `src/patching/returnto.cpp`, `wii/launch/boot.cpp` | USB Loader GX (https://github.com/wiidev/usbloadergx) `gamepatches.c`: `PrivateServerPatcher` and `domainpatcher` (after ToadKing's wiilauncher-nossl), Leseratte's Wiimmfi patches (`do_new_wiimmfi`, including the Wiimmfi team's binary patch and the error 51420 fix, copied as data; `do_new_wiimmfi_nonMKWii`) and the Mario Kart Wii RCE fix (`patch_error_codes`); the return-to-channel patch (`PatchReturnTo`, giantpune's, in `src/patching/returnto.cpp`), d2x's return-to ioctl (`PatchNewReturnTo`, in `wii/launch/boot.cpp`) and the order the patches are applied in (Wiimmfi's last) | GPL-3.0 (USB Loader GX's license, per its maintainers; `gamepatches.c` carries no header of its own). The Wiimmfi blob is copied unchanged; the rest reimplemented. Each file that follows it carries `SPDX-FileCopyrightText` lines for its holders. |
| USB Loader GX (game patches) | `src/patching/gamelang.cpp`, `src/patching/videopatch.cpp` (TV format conversion) | USB Loader GX (https://github.com/wiidev/usbloadergx): the SCGetLanguage search pattern and its `li r3,<language>` replacement (`patchcode.c`, `langpatcher`: Copyright (C) 2008 Nuke, GeckoOS for USB Gecko), and the Revolution SDK render mode heights its video mode patcher lists (`gamepatches.c`) | `patchcode.c` GPL-2.0-or-later; `gamepatches.c` GPL-3.0. Reimplemented, not copied. Each file that follows it carries `SPDX-FileCopyrightText` lines for its holders. |
| USB Loader GX (per-game fixes, cIOS choice) | `src/patching/gxpatches.cpp`, `src/patching/gxkirby.inc` (generated by `tools/gx_kirby_table.py`), `include/riftwii/gxpatches.hpp`, `src/patching/codehook.cpp`, `wii/launch/dolswitch_stub.S`, `wii/launch/boot.cpp`, `wii/storage/usbcatalog.cpp` | USB Loader GX (https://github.com/wiidev/usbloadergx at commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`): `source/patches/kirbypatch.c` (the MetaFortress patch for Kirby's Return to Dream Land: patch by crediar, ported to the other regions by dcx2, container12345 and blackb0x), copied as data; from `gamepatches.c`, `patch_nsmb`, `patch_pop` and `patch_re4` (their words copied as data), `patch_sdcard`, `anti_002_fix` (WiiPower), `PatchFix480p` with `find_safe_space`, the Japanese consoles' VI bit, `exclude_game`, `vidolpatcher` (WiiPower's "VIDTV patch", RiftWii's Region video fix), `speaker_patch`, `motor_patch` and `PatchAspectRatio` (their search words copied as data); from `usbloader/disc.c`, `PatchCountryStrings` (WiiPower's, RiftWii's Region strings fix); from `patchcode.c`, the idea of the multidol hook (a game's jump to another DOL sent through a stub that hooks the new one; RiftWii's own stub, `wii/launch/dolswitch_stub.S`); from `usbloader/GameBooter.cpp` and `system/IosLoader.cpp`, the automatic cIOS choice (the d2x base matching the game's IOS, Brawl's and SpongeBob's Boating Bash's exceptions, bases 56-60 for SD), reading a d2x cIOS's information block and d2x's IOS reload block at every launch (`BlockIOSReload`, on by default in GX); from `usbloader/AlternateDOLOffsets.cpp`, `defaultAltDol`'s game IDs and FST indexes (the executable started in main.dol's place, RiftWii's `game_start_file`) | GPL-3.0 (USB Loader GX's license, per its maintainers). The data is copied unchanged; the code reimplemented. Each file carries `SPDX-FileCopyrightText` lines for its holders. |
| USB Loader GX (d2x USB drive) | `wii/storage/umsdev.cpp` | USB Loader GX (https://github.com/wiidev/usbloadergx at commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`) `source/usbloader/usbstorage2.c` (Dimok and Rodries 2011, Kwiirk 2009): the ioctls of d2x's `/dev/usb2` (init, capacity, read sectors and the USB port switch, `'UMS'` + 0x1, 0x2, 0x3 and 0x83) and switching the port before the init, so a drive in the Wii's second USB port starts | zlib. Reimplemented: only the ioctl numbers and their order are taken. The file carries `SPDX-FileCopyrightText` lines for its holders. |
| USB Loader GX (emulated NAND saves) | `wii/storage/d2xsave.*`, `wii/storage/d2xnandemu.*`, `wii/launch/launchrun.cpp` (the base 56/57 rule for a game on the SD card), `include/riftwii/sharednand.hpp`, `src/data/sharednand.cpp`, `wii/storage/sharednandfind.*` | USB Loader GX (https://github.com/wiidev/usbloadergx at commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`): from `source/usbloader/NandEmu.cpp` (Dimok's, after Mighty Channel 11 and TriiForce), d2x's /dev/fs ioctlv 100 with the SD mode and the path, and the FAT module's 0xF0 that mounts the card for it; from `source/usbloader/GameBooter.cpp` (`SetupNandEmu`), unmounting the card before the emulation is turned on (the card's SD driver shut down with it, not only the file system) and mounting it again after, and the rule that a game on the SD card keeps its save there only on bases 56 and 57; from `source/usbloader/SavePath.cpp` (Dimok, 2011), the emulated NAND's folders made first and the game's `title.tmd`. For the Saves choice "SD, emulated NAND": from `source/settings/CSettings.cpp` and `source/settings/CGameSettings.cpp`, where the emulated NAND is (`NandEmuPath` in `GXGlobal.cfg`, default `sd:/nands/01/`, and a game's own in its `game:<ID6>` line of `GXGameSettings.cfg`) and how those lines are written, so the same folder is found | GPL-3.0 (USB Loader GX's license; `CSettings.cpp`, `CGameSettings.cpp` and `GameBooter.cpp` carry GPL-3.0-or-later headers, `SavePath.cpp` Dimok's zlib header, `NandEmu.cpp` none of its own). Reimplemented: only the ioctl numbers, the folder names, the setting names and the default path are taken. The files that follow it carry `SPDX-FileCopyrightText` lines for its holders. |
| USB Loader GX (RiiTag) | `include/riftwii/riitag.hpp`, `src/net/riitag.cpp`, `wii/online/riitagsend.*` | USB Loader GX (https://github.com/wiidev/usbloadergx at commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`) `source/network/Wiinnertag.cpp` by Dimok (2011): the `Wiinnertag.xml` format (top-level `<Tag URL Key>` elements), its `{ID6}` and `{KEY}` placeholders and one GET of the filled-in URL when a game starts, so a player's existing file works as it is | zlib (Dimok's header in that file). Reimplemented: RiftWii reads the file with pugixml and sends with its own HTTP client; no text copied. Each file carries `SPDX-FileCopyrightText` lines for its holders. |
| USB Loader GX (Wii Menu font) | `include/riftwii/sysfont.hpp`, `src/disc/sysfont.cpp`, `include/riftwii/brfnt.hpp`, `src/disc/brfnt.cpp`, `wii/menu/wiifont.*`, `vendor-libgui/source/FreeTypeGX.*` (`SetBitmapGlyphSource`) | USB Loader GX (https://github.com/wiidev/usbloadergx at commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`) `source/SystemMenu/SystemMenuResources.cpp` (Dimok, giantpune, blackb0x): finding the Wii Menu's font among the NAND's shared contents through `/shared1/content.map` by its SHA-1, the Korean Wii's own first on a Korean console, and taking the TrueType file out of the content's U8 archive. From `source/SystemMenu/SystemMenuResources.cpp` and `source/banner/WiiFont.cpp` (giantpune and Dimok, 2012, zlib): the Wii Menu's bitmap font, `wbf1.brfna`, in another shared content found by its SHA-1, and the BRFNT/RFNA layout (its FINF, TGLP, CWDH and CMAP sections, the three kinds of character map, the Huffman-packed glyph sheets of an RFNA). The third hash is copied as data; the reader is RiftWii's own, and its Huffman decoder follows GBATEK's description of Nintendo's compression formats. The two hashes are copied as data | GPL-3.0-or-later (that file's header). The lookup and the U8 reader are reimplemented. The font itself is Nintendo's: RiftWii reads it from the player's own Wii while the menu runs and never ships or stores it. Each file carries `SPDX-FileCopyrightText` lines for its holders. |
| USB Loader GX (GameCube banners, design only) | `include/riftwii/gcbanner.hpp`, `src/disc/gcbanner.cpp` (`gc_banner_urls`, `check_gc_banner_download`), `wii/online/online.cpp` (`DownloadGameCubeBanner`), `wii/menu/banners.cpp` | USB Loader GX (https://github.com/wiidev/usbloadergx at commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`) `source/network/ImageDownloader.cpp` and `source/usbloader/GetMissingGameFiles.cpp`, its custom banners: the banner server's address (banner.rc24.xyz), asking for a game's banner by its six-character ID and then by its first three characters (RiftWii's own rule: the second only for a regional copy of a game, never a mod, and no redirects), and keeping a download only when it is an IMET banner | GPL-3.0 (USB Loader GX's license, per its maintainers). Design only, nothing copied: RiftWii's own code checks the whole banner (`parse_opening_bnr`) and bounds its size, and draws its own frame for games the server has no banner for (not GX's `custom_banner.bnr`). |
| d2x-cIOS FAT module (SD mount for the emulated NAND) | `wii/storage/d2xnandemu.cpp` | public d2x-cIOS (https://github.com/wiidev/d2x-cios) `source/fat-module/{module.h,main.c,fat_wrapper.c,fs.h}`, `source/cios-lib/sdio.c` and `source/ffs-plugin/{plugin.c,plugin.h}`; command and error facts only: 0xF0 mounts the SD card and 0xF1 unmounts it and stops the module's own SD driver; /dev/fs ioctlv 100 with neither the SD (1) nor the USB (2) bit turns the emulation off while no title runs, and 0x100 is the full-NAND flag; the module runs its own driver on `/dev/sdio/slot0`; its mount answers -4 when that driver cannot start the card and -101 (`FS_EFATAL`) when it mounts a file system through none of the partition entries (a failed read of the card included) | GPL-3.0-or-later. Read as documentation of the interface; no source text copied. RiftWii's own code releases its card driver before the mount (the suspected cause of #128's -101 is libogc's driver left running next to the module's, not confirmed on hardware), retries once, sends 0xF1 when the emulation does not come on, turns it off (mode 0) and sends 0xF1 when the launch goes no further with it, and keeps off the card while the emulation is on. |
| d2x-cIOS DIP plugin | `wii/launch/di.cpp`, `include/riftwii/usbgame.hpp`, `src/fs/usbgame.cpp` | public d2x-cIOS `source/dip-plugin/{ioctl.h,frag.h,plugin.c}`; command/layout facts only | GPL-3.0-or-later. F6/F9/FA numbers, DEV_USB=1, DEV_SDHC=2, and the native `{size,num,maxnum}` / `{offset,sector,count}` ABI were independently implemented; no plugin implementation text was copied. |
| Friivolution launch argument (interface) | `src/patching/launchargs.cpp` (`friiv_launch_args`), `wii/launch/headless.cpp` | Friivolution by december (https://git.gay/december/friivolution), `launcher/include/FriivConfig.h` at commit `287b6d12bfb4`: the layout of the `FRIIV_CFG` argument a loader passes | Read as an interface description so loaders can start RiftWii the way they start Friivolution; RiftWii's reader is its own code, nothing copied. Friivolution is a Riivolution fork: none of its other code was read. |
| libwbfs (WBFS layout) | `include/riftwii/wbfspart.hpp`, `src/fs/wbfspart.cpp`, `src/fs/usbgame.cpp` | libwbfs by Kwiirk and contributors, as used by Wiimm's WBFS tools (https://wit.wiimm.de); layout facts only, checked against `wwt DUMP` | GPL-2.0. The header fields, the disc table, the disc-info size, the free-block table's place in the first block and the slot count it leaves were independently implemented; no libwbfs text was copied. |
| GameCube font encoding (behavior reference only) | `src/patching/gcfont.cpp`, `include/riftwii/gcfont.hpp`, `plugins/gamecube/gcnative.cpp` | Mario Party 4 community decompilation (https://github.com/mariopartyrd/marioparty4, commit `5b01c79b4910525e3fc5aa312eddd6d014e7f337`), `src/dolphin/os/OSFont.c`: the cached encoding, NTSC decision and VI DTV bit 0x2; checked against the player's Giftpia, Kururin Squash and US Mario Party 4 executables | Repository dedication CC0-1.0; SDK behavior read as documentation only. No source text or game instruction bytes copied; RiftWii's matcher and synthetic tests are its own. |
| Priiloader (how to tell it is installed) | `src/data/nandchannel.cpp` (`priiloader_copy_path`), `wii/storage/nandchannels.cpp` (`PriiloaderInstalled`) | DacoTaco's Priiloader installer (https://github.com/DacoTaco/priiloader at commit `45eaa45d091f1e4f4b7dd1d01a9c409a8de34e3e`, `src/Installer/source/installer.cpp`, `InitializeInstaller` and `PriiloaderInstalled`), read as documentation | GPL-2.0. A fact only: the installer keeps the Wii Menu's boot content as a copy whose content ID has 1 for its first hex digit, and looks for that copy to know Priiloader is there; RiftWii does the same check in its own code (the HOME Menu's Priiloader or Restart button), no text copied. |

Data fetched at run time, when the Wii is online and "Download names and
cheats" is on: game names and cover art from GameTDB
(https://www.gametdb.com, the `wiitdb.txt` lists and
art.gametdb.com's covers, stored shrunk on the SD card, and, when the
player asks on a game's About page, its database `wiitdb.zip` in the
menu's language for game details), cheat files from the GeckoCodes archive kept by
RiiConnect24 (https://codes.rc24.xyz) and, for a Wii game that has none
there, from GameHacking.org (https://gamehacking.org/text/, the newest
`text_wii_YYYYMMDD.zip`: one request for the listing and one for the zip, only
when a game's cheats are asked for and no copy under 30 days old is on the
card; kept in `sd:/riftwii/cheats/gamehacking/`. The site's robots.txt asks
crawlers for `Crawl-delay: 30`; RiftWii doesn't crawl, and makes these two
requests back to back at most once in about 30 days), animated banners for GameCube
games, and banners for the custom Wii games GameTDB lists, in the Channels view, made by the
community and hosted by RiiConnect24 (https://banner.rc24.xyz, stored on
the SD card as they come), and the newest release's tag from
GitHub's API (the update check). None is shipped with RiftWii, except
for two lists derived from GameTDB's database (https://www.gametdb.com,
`wiitdb.xml`): the codes of the games it lists with no online play,
`src/data/offlinegames.inc`, made by `tools/make_offline_games.py` for the game
page's Online server row, and the games whose case is not white,
`src/art/casecolours.inc`, made by `tools/make_case_colours.py` for the Home
shelf's boxes (the player can add their own in `sd:/riftwii/case_colors.txt`).
The "Would You Look at the Time" theme (Settings' "Disable clock on Home
screen" row) shows a soyjak cut from a soyjak.party image,
`themes/TheTime/soyjak.png` (cut out by `tools/timesong/cutout.py`), used
with soyjak.party's permission; it is not under the GPL and is not
RiftWii's. Its song, `themes/TheTime/music.ogg`, is RiftWii's own, made
by `tools/timesong/timesong.py` (GPL-3.0-or-later like the rest).
RiftWii contains no Nintendo keys or assets.

Build-time dependencies that are not vendored: devkitPPC/libogc, libfat,
zlib, FreeType, libogg/libvorbisidec, bzip2 (devkitPro portlibs), libmad
(GPL-2.0-or-later, with libogc's MP3 player, for the player's own .mp3 menu music),
each under its own license. libpng is not linked: `wii/menu/ftnopng.c` answers
FreeType's colour-bitmap PNG calls with a failure (the menu font has no
such glyphs). brotli is not linked either: `wii/menu/ftnobrotli.c` fails FreeType's
WOFF2 unpacking (the menu font is a plain TTF).

## Design provenance

RiftWii is a clean-room implementation. No Riivolution source code was
copied. For 2.0, Riivolution's source was read for behaviour
only (how its choices files and macros work), never copied. Behavioural references used, all publicly available:

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
  path (`wii/launch/boot.cpp`, `wii/launch/ios_reload.cpp`, `wii/launch/di.cpp`).
- The RVZ reader (`src/fs/rvz.cpp`, `runtime/rtrvz.c`) follows the format
  description in Dolphin's `docs/WiaAndRvz.md`; no Dolphin code was used.
  Its test images are built from scratch by `tools/rvz/make_test_disc.py`
  and converted with DolphinTool.
- libruntimeiospatch (GPL-2.0; Joseph Jordan, damysteryman, Christopher
  Bratusek, DarkMatterCore, megazig, FIX94), as shipped in USB Loader GX at
  commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`, read for behaviour
  only: `IosPatch_AHBPROT`, which byte sequence in IOS's ES code to look for
  and which byte to change so a reloaded IOS keeps hardware access
  (`keep_hardware_access` in `wii/launch/boot.cpp`). Those bytes are IOS's, not
  the library's; RiftWii's code is its own.
- The Home shelf's case colors (`src/art/casecolour.cpp`,
  `wii/menu/gui_shelf.cpp`) follow how WiiFlow Lite (GPL-3.0) colors its
  cover flow's boxes, read for behaviour only at commit
  `69c0d4d61984f4ed6fca51b4520320cadfac960d`: `source/list/ListGenerator.cpp`
  (`AddISO`: the player's `custom_titles.ini` color, then GameTDB's
  `<case color>`, then a built-in list, with black for GameCube games) and
  `source/gui/coverflow.cpp` (`InternalCoverColor`: New Super Mario Bros.
  Wii red by its first three ID characters; `_drawCoverBox`: the case's
  plastic in that color) and `source/gui/boxmesh.cpp` (the plastic shell
  a little larger than the paper, its bevelled border showing above,
  below and along the open edge). RiftWii's file format, lookup, sizes
  and drawing are its own; no code was used.
- Screenshots (`wii/menu/screenshot.cpp`, `wii/hooks/shothook.cpp`, `runtime/shot/`,
  `runtime/rtshot.c`) follow wiibrew's pages on the Video Interface
  registers, `/dev/fs`, `/dev/usb/oh1` and the Wii Remote's HID reports,
  with BT.601 for the YUV to RGB conversion. The PNG writer
  (`src/art/pngencode.cpp`) follows the PNG specification and RFC 1951
  (Deflate). No code was used from any of them.
- The Wii U Pro Controller translator (`runtime/rtpro.c`, `runtime/wlog/`,
  `wii/hooks/wpadhook.cpp`) follows wiibrew's pages on the Wii Remote, its
  extension controllers, the Classic Controller and the Wii U Pro
  Controller, and the Bluetooth core specification. How the extension
  encryption's 16-byte key and the game's lookup tables make the two 8-byte
  tables was learned from Dolphin's `Source/Core/Core/HW/WiimoteEmu/Encryption.cpp`
  (GPL-2.0-or-later, master as of 2026-10-15); RiftWii's code is its own.
  The lookup tables themselves are not in RiftWii: the loader finds each
  game's own (`find_extension_tables` in `src/patching/wpadlog.cpp`) and the
  translator uses them where they are.
- TGC images on GameCube bonus discs (`src/disc/gctgc.cpp`, `gc_on_tgc_read` in
  `runtime/gc/gc_rt.c`): the header's fields (the magic, the header's size,
  the FST's, the DOL's and the file area's real and virtual offsets) and
  how a file's offset in the packed FST maps onto the TGC follow Dolphin's
  TGC reader (GPL-2.0-or-later, `Source/Core/DiscIO/TGCBlob.cpp` and
  `TGCBlob.h` at commit 239330017c97fa3f6b4b2670ab570e822d6affb0, the last one
  to change either file; master was cf5cb7ef81c3e8e7af5c9d566334219ffda9f68b on
  2026-10-20), read for behaviour only. Dolphin
  presents a TGC as a disc of its own; RiftWii instead moves the files onto
  the outer disc and bounds every field, and its code is its own.
- The GameCube loader's per-game list `gc_keeps_aram_addresses`
  (`src/patching/gxpatches.cpp`) rests on Mario Party 4's sound manager as
  its community decompilation shows it (https://github.com/mariopartyrd/marioparty4,
  `src/msm/msmsys.c` and `src/game/audio.c`, main as of 2026-10-17): what
  the game checks, read for behaviour only; no code was used. Mario Party 5
  and 6 joined it from their own decompilations
  (https://github.com/mariopartyrd/marioparty5, `config/GP5E01_00/symbols.txt`
  for the hang's function, at 5324654; https://github.com/mariopartyrd/marioparty6,
  `src/game/audio.c`, at a56adea): the same sound manager set up the same way.
- The GameCube SIProbe answer for a Wii Remote's port (`gc_ports_probe` in
  `runtime/gc/gc_ports.h`, `find_gc_si_probe` in `src/patching/symsearch.cpp`)
  rests on Mario Kart: Double Dash!!'s community decompilation
  (https://github.com/doldecomp/mkdd, commit `1d2918ca577817ba431baddb78dda24831e0e0bb`,
  CC0-1.0): `libs/JSystem/JUtility/JUTGamePad.cpp` (its `read` takes a port's
  status only when `SIProbe` says a controller or a WaveBird),
  `src/Osako/PadMgr.cpp` and `libs/dolphin/si/SIBios.c` (`SIProbe` as
  `SIDecodeType(SIGetType(chan))`), read for behaviour only and checked
  against the player's own GM4E01 executable; no code was used.
- Game banners and channel icons (`src/disc/bnr.cpp`, `src/disc/tpl.cpp`,
  `src/disc/brlyt.cpp`, `src/disc/brlan.cpp`, `src/disc/wiicrypt.cpp`, `wii/menu/banners.*`,
  `wii/menu/bannerplay.*`, `wii/menu/otpkey.*`, `src/disc/bnrsound.cpp`, `wii/menu/bannersound.*`)
  follow the public format pages: WiiBrew's Opening.bnr (with its sound.bin
  and BNS layout), U8 archive, LZ77, Wii disc, Ticket and Hardware/OTP pages,
  and the Custom Mario Kart wiki's (mkwiiki.org) TPL, BRLYT, BRLAN and BRLAN
  Targets pages; the DSP ADPCM codec follows its public description (frames
  of 14 samples, a coefficient pair and a shift a frame), WAV and AIFF their
  published layouts. AES-128 follows FIPS-197. Which of
  a material's colours a texel's black and white become, the language
  groups and the second texture as an alpha mask (for materials without
  TEV stages of their own) were worked out from games' own banners. The
  order of the fields in a material's TEV stage record, the widescreen
  flag that keeps a pane's shape and the 608-unit view were learned by
  reading USB Loader GX's banner code (`source/banner`, from the Wii
  Banner Player Project and Dimok, zlib; https://github.com/wiidev/usbloadergx
  at commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`), and the view's
  placement measured against the Wii Menu's Disc Channel in Dolphin; no
  code was copied. No key ships with RiftWii: the common key is read
  from the player's own Wii at run time and kept in memory only.
- GameCube memory card images (`src/fs/gccard.cpp`): the header, the two
  directories and two block allocation tables, the 64-byte directory
  entry (also a .gci file's header), the checksum pair, the update
  counters that choose between the copies, the banner and icon formats
  (RGB5A3, CI8 with a shared or its own palette) and their tiling follow
  the memory card chapter of Yet Another GameCube Documentation (YAGCD,
  hitmen.c02.at/files/yagcd). Which free block a new save takes (the
  first free one after the table's last allocated block, the reason the
  table keeps that field) and writing each update into the copy not in
  use (so the newer counter wins only once the copy is whole) are
  RiftWii's reading of what those fields are for. No code from Dolphin,
  Nintendont, libogc or any other project was used.
  The formatted card itself (`gc_card_format`, moved there from
  `plugins/gamecube/gcnative.cpp`) keeps the layout recorded for the GameCube
  loader in the table above.
- A GameCube disc's banner (`src/disc/gcbanner.cpp`): the BNR1/BNR2
  layout (the 96x32 RGB5A3 picture at 0x20, the 0x140-byte records of
  names, maker and description from 0x1820, six of them on a European
  disc) follows YAGCD's banner chapter and wiibrew's Opening.bnr page.
  Its text goes through the memory card's Shift-JIS and Windows-1252
  conversion. The frame RiftWii draws the picture in is its own.
- A GameCube game's 480p, 16:9 and language options (`apply_picture_and_language`
  and its searches in `plugins/gamecube/gcnative.cpp`, `gc_on_vi_configure` and
  `gc_on_projection` in `runtime/gc/gc_rt.c`, their hooks in
  `runtime/gc/gc_entry.S`). Facts: YAGCD's SRAM layout (the language byte
  and its numbers, the flags byte with progressive scan in bit 7) and its
  Video Interface chapter (the DTV register at 0xCC00206E, bit 0 a
  component cable); the GXRenderModeObj and GXSetProjection layouts and
  the matrix GX keeps of a projection, as libogc's public headers
  (`gx.h`, `video_types.h`) declare them; Nintendo's published rule for
  480p on the GameCube (a component cable and the console's setting, or
  B held at start), as the games' own question shows it. Ideas: forcing
  progressive scan and widescreen are options Nintendont also offers
  (named here as the idea only; its source was not read), and widening
  only the perspective projection, by its x scale times 3/4, is the
  common widescreen approach. How each SDK function is found by its
  instructions (the SRAM copy's lock, VIGetDTVStatus, VIConfigure,
  GXSetProjection) was worked out from Super Mario Sunshine's and Luigi's
  Mansion's own code. No code from Nintendont, Swiss, Dolphin or any
  other project was used.
- A two-disc GameCube game's disc change (`runtime/gc/gc_disc.h`,
  `gc_on_stop_motor` and `gc_disc_on_alarm` in `runtime/gc/gc_rt.c`,
  `open_second_disc` in `plugins/gamecube/gcnative.cpp`,
  `src/disc/gcdiscs.cpp`). The behaviour follows Nintendont's (FIX94 and
  contributors, https://github.com/FIX94/Nintendont, GPL-2.0, its row in
  the table above), as RiftWii's PR #227 describes it: the other image
  goes in when the game stops the drive's motor (DI command 0xE3), and
  the DI cover interrupt follows about two seconds later; and Dolphin's
  DVDInterface (GPL-2.0-or-later), whose automatic disc change also
  starts at a stop of the motor. Facts: YAGCD's and wiibrew's DI register
  pages (the cover register's state, mask and interrupt bits), and the
  OS's interrupt handler table at 0x80003040. Behaviour only: neither
  project's code was read or copied for this, so no commit applies; the
  state machine, the second disc's checks and its MEM2 tables are
  RiftWii's own. Nintendont is already credited in the README's Credits
  and `wii/menu/credits.cpp`.

## Emulator cover services

RiftWii downloads emulator covers from [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
via https://thumbnails.libretro.com/, and optionally from [SteamGridDB](https://www.steamgriddb.com/api/v2)
with the player's own key. No service code or artwork is bundled. The source
protocols and the implementation in `src/art/emulatorart.cpp` and
`wii/menu/emulatorcovers.cpp` are independent; no third-party source was used.

The libretro-thumbnails [Credits](https://github.com/libretro-thumbnails/libretro-thumbnails#credits)
thank volunteer contributors, photographers, scanners, collectors and sites,
including MobyGames and Fandom. Original game and promotional art belongs to
its developers and publishers. SteamGridDB's contributors supply its artwork.
These are credits for downloaded content, not a license grant for redistribution.
The public service documentation and credits were checked on 2026-10-28.
