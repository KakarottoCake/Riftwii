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
| `gc_dol`, `gc_restart`, `gc_arg` | `123456768`, `3221225472`, `cd` | RiftWii's own, not for loaders: a GameCube game on the SD card switching to another program on its disc (the SDK's OSExec; Sonic Gems Collection starts its games this way). RiftWii can't patch that program from inside the game, so the game goes back to RiftWii, and RiftWii starts the image again with it. Its blob writes these into `sd:/riftwii/gc/exec.txt`, which the GameCube loader makes at every launch (first line `--idle--` until the switch). RiftWii's next start reads that file once, as it does `launch.txt`, and deletes it. `gc_dol`: the program's offset on the disc, in decimal (0: the disc's own). `gc_restart`: the restart code the game passed. `gc_arg`: each argument, in order. |

| `choices`, `reset` | `SMNP03.txt`, `1` | RiftWii's own, not for loaders: a Wii game's own Reset (below). `choices`: the pack choices file to read, a file name in `sd:/riftwii/choices` (the game's own, or a profile's), where the pack's options and the game's settings were saved. `reset=1`: this launch is the same play started again, so it is not counted in the play history and not sent to RiTag. |

An unknown key or a value RiftWii does not take stops the launch with
the key named, so a loader finds a mismatch at once.

### What a launch may hold

A launch comes from an argument, a file on the card or, for a Reset, the
game itself, so it is checked before anything is read or started:

- No value is longer than 255 bytes, and a line of a launch file is no
  longer than 271. A file is read for its first 16 KB and 64 lines; a file
  that has more is not a launch.
- No `path`, `xml`, `code_build` or `gct` has `..` in it, between slashes
  or backslashes or alone, so a name cannot climb out of its folder. An
  `xml` that is a bare file name has no slash or colon at all, and
  `choices` is a plain file name (letters, digits and `. _ + -`, ending in
  `.txt`, no leading dot, no `..`) that RiftWii joins to
  `sd:/riftwii/choices`.
- At most 32 `xml` and 16 `code_build` keys.

## A Wii game's own Reset

The HOME Menu's Reset (and the console's Reset button) has a Wii disc
game restart itself: the SDK's `OSRestart` reads the disc's apploader
again, which loads `main.dol` and the file table over the running game.
RiftWii is not there to load them, so the restart would come back without
the packs. RiftWii takes the Reset instead and starts the game again
through the same headless launch, with the same packs, choices and
settings, not at its menu:

1. At every launch whose runtime is installed and that has the RiftWii
   channel to go to, the loader makes `sd:/riftwii/restart.txt`: one
   sector of the launch that would start this game again (`game`, `from`,
   `path`, `choices`, one `xml` per pack that is on (`xml=none` for none),
   `code_build`, the settings that are not RiftWii's own, `gct`,
   `return_to`, `reset=1`), with `--idle--` for its first line. RiftWii's
   next start skips such a file, as it does `gc/exec.txt`.
2. The runtime hooks the game's `OSRestart`. When the game calls it, the
   runtime writes the same sector back with `--launch` for the first line
   (it holds a copy, so it composes nothing), through the card handle it
   already has, and starts the RiftWii channel through `/dev/es` (the
   ticket view was read at the launch). The channel starts RiftWii.
3. RiftWii's start reads `restart.txt` once, as it does `launch.txt`, and
   deletes it, and starts the game as the launch says: the log line
   `The game's own Reset: SMNP03 starts again, with the packs and choices it had`.
4. Anything that goes wrong on the way leaves the game's own restart to
   run, as it would without RiftWii, and the request back as `--idle--`
   so nothing starts later: the card refused the write, IOS refused the
   channel's launch.

Without the RiftWii channel installed (or without a ticket for it), with a
game that checks its own code, with no runtime in the launch, with a launch
that does not fit one sector (511 bytes) or one RiftWii would refuse, the
`OSRestart` hook is not installed, `boot.log` says why in a `Reset:` line,
and the Reset restarts the game without its packs, as before. When the
hook goes in, its `Reset:` line says that, with OSRestart's address and
the request's sector. Either way the line is decided and written while
the log is still open (`plan_restart`, before the card goes to the
runtime and the log ends); a launch that reloads the IOS ends the log
earlier, so its line only says the request is ready and the rest is on
screen. The
channel's title is the RiftWii channel whatever Settings > Return to
says: it is where RiftWii starts. `debug_off = reset` leaves the Reset to
the game ([DEVELOPING.md](DEVELOPING.md)).

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

The arguments are parsed in `src/patching/launchargs.cpp` (host-tested in
`tests/extras_tests.cpp`, `friiv_launch_args` for Friivolution's
argument) and run by `wii/launch/headless.cpp`, which takes the
same path as Start on the game page: `ScanPackages`, the loader's picks
on top, `PrepareLaunchExtras`, then `RunLaunch` or `RunBoot`.
