# Starting a game through RiftWii from another loader

A loader (USB Loader GX, WiiFlow, a forwarder) can hand a game to RiftWii
to start it with Riivolution packs. RiftWii's menu never opens: it finds
the game, turns on the packs it was given, applies the loader's settings
and boots, showing its progress on a console screen. If something is
wrong it says what on that screen and in `sd:/riftwii/boot.log`, and
waits for HOME, Start or RESET.

## How to call it

Load `sd:/apps/riftwii/boot.dol` (or wherever the user keeps it) and start
it with these arguments, the way a loader starts Nintendont or any
Homebrew Channel app with arguments:

```
argv[0]  sd:/apps/riftwii/boot.dol
argv[1]  --launch
argv[2…] key=value, one per argument
```

A loader that cannot pass arguments can write the same lines, `--launch`
first, to `sd:/riftwii/launch.txt` and start RiftWii without any. RiftWii
deletes the file as it reads it.

A loader that already starts Friivolution can start RiftWii the same way,
unchanged: see "Friivolution's argument" below.

## Keys

`game` or `path` is required (or `from=disc`, for whatever disc is in
the drive). Anything left out is what RiftWii would use for that game
from its own menu (the game's saved choices, then RiftWii's Settings).

| Key | Values | Meaning |
| --- | --- | --- |
| `game` | `RMCE01`, or `RMCE` | The game ID, all six characters or the first four. |
| `from` | `usb`, `sd`, `disc` | Where the game is. Without it RiftWii looks on the USB drive, the SD card, then the disc. Images are found by ID in `wbfs/`, `games/` and the user's `game_folders`. |
| `path` | `usb:/wbfs/Mario Kart Wii [RMCE01]/RMCE01.wbfs` | The image itself, for a loader that knows it (the first part of a split image). It sets `from`. With `game` too, the image must have that ID. |
| `xml` | `sd:/riivolution/ctgp.xml`, `ctgp.xml`, `all` or `none` | A pack to turn on, by its path or by its file name in a `riivolution` folder; repeat it for several. Every pack not named is off; `all` turns on every pack for the game, `none` turns them all off. Without any `xml`, the packs RiftWii has saved for the game are used. Each pack's options are the ones saved for it in RiftWii, else in Riivolution's `sd:/riivolution/config/<ID4>.xml`, else the pack's defaults. |
| `video_mode` | `game`, `system`, `ntsc`, `pal60`, `pal50`, `480p` | The TV format. |
| `video_width` | `game`, `framebuffer`, `704`, `720` | The picture width. |
| `deflicker` | `game`, `off`, `low`, `medium`, `high` | |
| `borders` | `keep`, `remove`, `remove_all` | `remove`: the side bars; `remove_all` also the top and bottom (experimental). |
| `language` | `console`, `ja`, `en`, `de`, `fr`, `es`, `it`, `nl`, `zh-hans`, `zh-hant`, `ko` | The language the game is told the console uses. |
| `cios` | `auto`, `248` … `252` | The d2x cIOS for a USB or SD game. |
| `server` | `off`, `wiimmfi`, `wiilink`, `altwfc`, `custom` | The online server; `custom` uses `wfc_domain`. |
| `wfc_domain` | `example.net` | The custom server's domain (4 to 16 characters). |
| `code_build` | `sd:/projectplus`, `projectplus` or `sd:/projectplus/codes/RSBE01.gct` | A code build to turn on (Project+, REX and the like), by its folder on the SD card, the folder's name, or its code file; repeat it for several. As with `xml`, every pack not named is off, and the two can be given together. Code builds are read from the SD card only. |
| `gct` | `sd:/codes/RMCE01.gct`, or `none` | Gecko codes to run. `none`: no codes. Code builds (Project+) keep their own. |
| `return_to` | `0001000147584c44` (or `00010001-47584c44`), or `menu` | The channel a game's HOME Menu "Wii Menu" button starts, such as the loader's own forwarder; `menu` leaves the Wii Menu. Without it RiftWii's own setting is used (the RiftWii channel, when installed). |

An unknown key or a value RiftWii does not take stops the launch with
the key named, so a loader finds a mismatch at once.

## Example

USB Loader GX starting Mario Kart Wii from the USB drive with CTGP and
its own settings:

```
sd:/apps/riftwii/boot.dol
--launch
game=RMCE01
from=usb
xml=sd:/riivolution/ctgp.xml
video_mode=pal60
language=en
cios=249
gct=sd:/codes/RMCE01.gct
return_to=00010001-554c4e52
```

## Friivolution's argument

Friivolution takes one binary argument from a loader, `FRIIV_CFG`
(described in Friivolution's `launcher/include/FriivConfig.h`), the way
Nintendont takes its own. RiftWii reads the same argument, so a loader
set up to start Friivolution starts RiftWii when its path points at
RiftWii's `boot.dol` instead. RiftWii's `meta.xml` version is past 1.00,
the check such loaders make.

The 384 bytes (big-endian) are: `'FRIV'`, the version (1), the flags,
the game ID's first four characters (or zero), the image path (255
bytes), one pack's file name (64 bytes) and padding. RiftWii turns them
into the arguments above:

| FRIIV_CFG | RiftWii |
| --- | --- |
| Flag 1 (boot) not set | Not a launch: the menu opens, as Friivolution's does |
| Image path, flag 2 set / not set | `path=usb:<path>` / `path=sd:<path>` |
| Empty image path | `from=disc` |
| Game ID | `game=<ID4>` |
| Pack file name | `xml=<name>` |
| No pack named | `xml=all`: every pack for the game, as Friivolution loads them all |
| Flag 4 (no patches) | `xml=none` |

The options each pack uses come from where they always do (RiftWii's
saved choices, else Riivolution's `config/<ID4>.xml`, which is where
Friivolution keeps them). Friivolution's "hold B for the menu" at start
has no counterpart: RiftWii boots.

## For RiftWii's developers

The arguments are parsed in `src/launchargs.cpp` (host-tested in
`tests/extras_tests.cpp`, `friiv_launch_args` for Friivolution's
argument) and run by `wii/headless.cpp`, which takes the
same path as Start on the game page: `ScanPackages`, the loader's picks
on top, `PrepareLaunchExtras`, then `RunLaunch` or `RunBoot`.
