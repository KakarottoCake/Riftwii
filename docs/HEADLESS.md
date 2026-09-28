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

## Keys

Only `game` is required. Anything left out is what RiftWii would use for
that game from its own menu (the game's saved choices, then RiftWii's
Settings).

| Key | Values | Meaning |
| --- | --- | --- |
| `game` | `RMCE01` | The six-character game ID. |
| `from` | `usb`, `sd`, `disc` | Where the game is. Without it RiftWii looks on the USB drive, the SD card, then the disc. Images are found by ID in `wbfs/`, `games/` and the user's `game_folders`. |
| `xml` | `sd:/riivolution/ctgp.xml`, or `none` | A pack to turn on; repeat it for several. Every pack not named is off; `none` turns them all off. Without any `xml`, the packs RiftWii has saved for the game are used. Each pack's options are the ones saved for it in RiftWii, else in Riivolution's `sd:/riivolution/config/<ID4>.xml`, else the pack's defaults. |
| `video_mode` | `game`, `system`, `ntsc`, `pal60`, `pal50`, `480p` | The TV format. |
| `video_width` | `game`, `framebuffer`, `704`, `720` | The picture width. |
| `deflicker` | `game`, `off`, `low`, `medium`, `high` | |
| `borders` | `keep`, `remove` | |
| `language` | `console`, `ja`, `en`, `de`, `fr`, `es`, `it`, `nl`, `zh-hans`, `zh-hant`, `ko` | The language the game is told the console uses. |
| `cios` | `auto`, `248` … `252` | The d2x cIOS for a USB or SD game. |
| `server` | `off`, `wiimmfi`, `wiilink`, `altwfc`, `custom` | The online server; `custom` uses `wfc_domain`. |
| `wfc_domain` | `example.net` | The custom server's domain (4 to 16 characters). |
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

## For RiftWii's developers

The arguments are parsed in `src/launchargs.cpp` (host-tested in
`tests/extras_tests.cpp`) and run by `wii/headless.cpp`, which takes the
same path as Start on the game page: `ScanPackages`, the loader's picks
on top, `PrepareLaunchExtras`, then `RunLaunch` or `RunBoot`.
