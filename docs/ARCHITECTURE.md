# How RiftWii works

A map of the code for anyone changing it. RiftWii is a Homebrew Channel
app that starts a Wii game with Riivolution-format mod packs applied,
without changing the game's files. It runs in four phases; each section
below names the files that own it.

```
 Homebrew Channel
   └─ RiftWii menu (wii/)  ── reads drives, packs, choices
        └─ plan the launch (src/, wii/modplan.cpp)
             └─ boot the game (wii/boot.cpp)  ── IOS, apploader, patches
                  └─ in the game: resident runtime + pad hook (runtime/)
```

## 1. The menu

| Piece | Files |
| --- | --- |
| Screens (Home, game page, Mods, Cheats, Settings, adapter test) | `wii/rift_menu.cpp` |
| Widgets: the tile grid and the row list | `wii/gui_gamegrid.cpp`, `wii/gui_flowlist.cpp` |
| Artwork, painted at start (no image files) | `wii/skin.cpp`, `src/canvas.cpp` |
| Menu state: the picked game, its packs, the choices | `wii/frontend.cpp`, `src/launch.cpp` (`LaunchModel`) |
| Game images on SD and USB (WBFS, split WBFS, ISO; FAT32 and NTFS) | `wii/usbcatalog.cpp`, `src/usbgame.cpp`, `src/fat32.cpp`, `src/ntfs.cpp` |
| Game names (GameTDB), cheats (GeckoCodes archive) | `wii/online.cpp`, `src/titles.cpp`, `src/cheats.cpp`, `src/http.cpp`, `wii/netsock.cpp` |
| Network packs (RiiFS) | `wii/netpacks.cpp`, `src/riifs.cpp`, `src/riifs_sync.cpp`, `docs/RIIFS.md` |
| Settings, play history, translations | `wii/loadersettings.cpp`, `src/settingsfile.cpp`, `src/playhistory.cpp`, `wii/i18n.cpp`, `tools/lang_source.py` |
| Menu IOS (IOS 58 or a d2x cIOS slot) | `wii/menuios.cpp` |
| GameCube adapter test page | `wii/gcadapter.cpp` (drives `runtime/rtgcad.c` with libogc's IPC) |
| Memory limits: the heap never enters memory a launch overwrites | `wii/memlimits.cpp` |

Everything the menu decides is plain data (`LaunchModel`, the per-game
choices file) and host-tested; the screens only draw and read the pads.
Text goes through libwiigui's `GuiText`, which translates every string
it is given; `tools/lang_source.py` holds the table and checks that
every entry still appears in the sources.

## 2. Planning a launch

| Step | Files |
| --- | --- |
| Parse the XML (the whole documented format) | `src/patch.cpp` (`parse_package`) |
| Resolve choices, params and placeholders for this disc | `src/patch.cpp` (`plan_package`) |
| Turn file and folder patches into byte sources | `src/apply.cpp`, `src/overlay.cpp`, `src/expand.cpp`, `src/source.cpp` |
| Memory patches (plain, search, ocarina) | `src/mempatch.cpp` |
| The redirect table the runtime walks | `src/redirect.cpp`, `runtime/rtable.c` |
| Glue on the Wii: packs to boot options | `wii/modplan.cpp` |

A selected feature the runtime cannot carry out makes planning fail with
the pack, option and file named, so a game never starts half patched.

## 3. Booting

`wii/boot.cpp` does what the System Menu and an apploader would:

1. Pick the IOS (`wii/ios_reload.cpp`). Launches that need the SD card or
   the GameCube adapter keep the running IOS and report the game's own
   to it; the rest reload to the IOS the game asks for.
2. Open the disc (`wii/di.cpp`), or for an SD/USB image reload into a d2x
   cIOS and hand it the image's fragment list (`wii/usbcatalog.cpp`,
   `activate_image_game`).
3. Run the game's apploader. Overrides replace what it loads on the way
   in: the rewritten FST (files that grew or were created move into a
   virtual window above the disc), the data header, a pack's `main.dol`.
4. Install the resident runtime (`wii/resident.cpp`) and the pad hook
   (`wii/padhook.cpp`); apply memory patches, cheats (the Gecko code
   handler, `vendor-gecko/`) and video patches (`src/videopatch.cpp`).
5. Write the low-memory globals and jump to the game.

## 4. Inside the game

**Resident runtime** (`runtime/resident/`). A position-independent blob:
the build links it at two addresses and refuses it if the bytes differ.
It hooks the game's `IOS_IoctlAsync`, found by structure
(`src/symsearch.cpp`), not by per-game addresses, and answers disc reads
from the redirect table: bytes from memory, from the SD card (raw SDIO,
or d2x's SD device when the game itself is on the card), or from the
disc at another offset. Failed SD and disc reads are retried three times.
With `<savegame>` it also answers the game's NAND file calls from a
folder on the card (`runtime/rtfs.c` on the FAT32 engine
`runtime/rtfat.c`), and it serves Riivolution's `file` device for
Pulsar packs.

**Pad hook** (`runtime/pad/`, `runtime/rtgcad.c`). With the GameCube
adapter on, a second blob hooks the game's `PADRead` and
`PADControlMotor` (found by structure too) and runs the WUP-028 driver
through the game's own asynchronous IPC. The adapter's controllers fill
ports with nothing plugged in; rumble goes back to them.

## Memory

| Where | What |
| --- | --- |
| `0x80000000`–`0x80003400` | Low-memory globals; the Gecko code handler at `0x80001800` |
| `0x80A00000`–`0x81200000` | The RiftWii loader (link address in `Makefile.wii`) and its heap |
| `0x81200000` | The game's apploader, while it runs |
| Top of the game's MEM1 arena | Resident runtime code, then the pad blob below it |
| `0x90000000`–`0x90800000` | Left alone by the loader: an IOS reload stages its kernel here |
| Bottom of the game's MEM2 arena | Resident runtime data, then the pad state |
| `0x933E0000` and up | IOS |

`wii/memlimits.cpp` keeps the loader's heap between the end of its own
image and `0x81200000`, and in MEM2 above `0x90800000`. In Dolphin it
fills the reload area with `0xDEADBEEF` at each reload, so a heap that
strays there fails in the emulator as it would on a Wii.

## Files on the SD card

| Path | What |
| --- | --- |
| `sd:/apps/riftwii/` | The app (`boot.dol`, `meta.xml`, `icon.png`) |
| `sd:/riivolution/` | Mod packs: XML files and their folders |
| `sd:/riftwii/settings.txt` | Settings |
| `sd:/riftwii/menu_ios.txt` | The menu IOS slot |
| `sd:/riftwii/choices/<ID>.txt` | Per-game choices (packs, options, saves, cheats, picture) |
| `sd:/riftwii/choices/<ID>.video` | Which borders the game drew last time |
| `sd:/riftwii/cheats/<ID>.txt` | The game's cheat list |
| `sd:/riftwii/saves/<ID>/` | Saves kept on the card |
| `sd:/riftwii/history.txt` | Recently played |
| `sd:/riftwii/titles-<lang>.txt` | GameTDB's game names |
| `sd:/riftwii/lang/<lang>.po` | A translation that overrides the built-in one |
| `sd:/riftwii/riifs/` | Files copied from network packs |
| `sd:/riftwii/session.log`, `boot.log` | The menu's log and the last launch's log |
| `sd:/riftwii/autorun.txt`, `guiscript.txt` | Test scripts (see `DEVELOPING.md`) |

## Rules the code keeps

- Clean room: no Riivolution code, ever. What was used instead is listed
  in `NOTICE.md`.
- Nothing is hardcoded for a particular game or mod. Hooks are found by
  the structure of the SDK's code.
- The runtime has no globals, no string literals and no library calls;
  everything it needs is in the context the loader fills.
- Failures say what failed and why, on screen and in the log.
