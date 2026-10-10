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

A loader that passes one file path can name a launch file of its own on
the SD card: `argv[1]` is the file's path (`sd:/...`), and the file holds the
same lines, `--launch` first. RiftWii only reads it, so the file starts the
same game every time. This is how a WiiFlow plugin starts RiftWii (below).

A loader that mounted the USB drive itself must let go of it before it
starts RiftWii (unmount it and shut its driver down, as it does the SD
card). If RiftWii lists no drive after 4 seconds, it reloads the IOS, which
lets go of a drive left open, and waits 10 seconds more.

A loader that already starts Friivolution can start RiftWii the same way,
unchanged: see "Friivolution's argument" below.

## Keys

`game` or `path` is required (or `from=disc`, for whatever disc is in
the drive). Anything left out is what RiftWii would use for that game
from its own menu (the game's saved choices, then RiftWii's Settings).

| Key | Values | Meaning |
| --- | --- | --- |
| `game` | `RMCE01`, or `RMCE` | The game ID, all six characters or the first four. |
| `from` | `usb`, `sd`, `disc` | Where the game is. Without it RiftWii looks on the USB drive, the SD card, then the disc. When it looks on the USB drive, it first waits up to 10 seconds for a drive to be listed (a loader that has just let go of the drive), so give `sd` or `disc` when the game is there. Images are found by ID in `wbfs/`, `games/` and the user's `game_folders`. |
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
| `code_build` | `sd:/projectplus`, `projectplus` or `sd:/projectplus/codes/RSBE01.gct` | A code build to turn on (Project+, REX and the like), by its folder on the SD card, the folder's name, or its code file; repeat it for several. As with `xml`, every pack not named is off, and the two can be given together. A build inside an SD image is named the way RiftWii keeps it: `pplus.raw/Project+/RSBE01.gct`. Code builds are read from the SD card only. |
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

## A WiiFlow plugin

A WiiFlow plugin lists one launch file per entry and starts RiftWii with
the picked file's path. Put `boot.dol` where WiiFlow looks for the
plugin's DOL. A plugin ini such as `wiiflow/plugins/riftwii.ini`:

```
[PLUGIN]
magic=52494657
dolfile=boot.dol
arguments={device}:/{path}/{name}
romdir=riftmods
rompartition=0
filetypes=.txt
displayname=RiftWii
returnloader=yes
```

And one file per game or mod setup, e.g. `sd:/riftmods/Project+.txt`:

```
--launch
game=RSBE01
code_build=Project+
```

The files must be on the SD card. With the game on a USB drive, add
`from=usb`.

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
