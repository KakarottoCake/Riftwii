# Emulator add-ons

An emulator author can make RiftWii list and start its games by shipping
`riftwii-emulator.ini` beside the emulator's DOL in `sd:/apps/<folder>/`
or `usb:/apps/<folder>/`. RiftWii reads only data from this file. No code
is linked into RiftWii, and a new emulator does not need a RiftWii release.
This is separate from RiftWii's private, version-matched `.rwp` code
[plugins](PLUGINS.md).

RiftWii ships no emulator, BIOS or games. Players supply their own files.
An add-on describes arguments the emulator already understands; it cannot
teach an emulator a new option or make it support a new image format.

## A complete file

This example describes the requested nullDC4Wii argument shape. The author
must choose the actual supported options and values for their build.

```ini
# sd:/apps/nulldc4wii/riftwii-emulator.ini
format = 1
name = nullDC4Wii
system = Dreamcast
covers = Sega - Dreamcast
dol = boot.dol
games = sd:/roms/dreamcast, usb:/roms/dreamcast
extensions = cdi, gdi, chd
args = -game {game} -preset {preset} -speed {speed}

[setting preset]
label = Preset
help = Fixes for one game's graphics or speed.
type = choice
values = auto, powerstone, shenmue, chuchu
default = auto

[setting frameskip]
label = Frame skip
help = Skip frames to keep the game running at speed.
type = switch
on = -frameskip
default = off

[setting speed]
label = Speed
type = number
min = 1
max = 3
default = 1
```

The file is UTF-8 text with LF or CRLF line endings. Keys and setting names
are lowercase. Blank lines and whole lines beginning with `#` or `;` are
ignored. There are no inline comments, escape sequences, comma escapes or
continuation lines. A key may appear once in its section; a setting name
may be declared once. The top-level fields above are required, including
`args` (which may be empty). Top-level fields go before setting sections.
Unknown keys and sections are ignored and logged. Invalid known fields
reject the whole add-on, with the reason in `session.log`.

`covers` is optional: it names the system folder on
[libretro's thumbnail server](https://thumbnails.libretro.com/), exactly as
listed there. For example: `Sega - Dreamcast`, `Sega - Mega Drive - Genesis`,
`Nintendo - Nintendo Entertainment System`, `Nintendo - Super Nintendo
Entertainment System`, `Nintendo - Game Boy Advance`, or `Sony - PlayStation`.
It is a single folder name of at most 64 bytes; paths, control characters,
`.` and `..` are refused. Without it, local covers and the optional
SteamGridDB fallback still work.

`format` is exactly `1`. Any other version, including a future one, is
refused with a line naming the supported version. `dol` may be relative to
the app folder, including subfolders, or an absolute `sd:/` or `usb:/`
path ending in `.dol`. Game folders are absolute SD or USB paths without
a trailing slash. Empty path components, `.` and `..`, backslashes, other
devices and control characters are refused. Extensions have no dot and
contain letters and digits only; matching ignores their case.

## Settings and arguments

Every setting has `label`, `type` and `default`. `help` is optional. Names
are 1–32 lowercase letters, digits or underscores. The six game
placeholders (`game`, `game_name`, `game_folder`, `game_file`,
`game_device`, `game_path`) are reserved and can't be setting names.
Author-supplied labels, help and choice values pass through RiftWii's
translation lookup. English is the fallback;
players may translate them in `sd:/riftwii/lang/<language>.po`.

| Type | Fields | Accepted player values |
| --- | --- | --- |
| `choice` | `values = auto, powerstone, ...` | Exactly one nonempty listed value; the default must be in the list |
| `switch` | `on = -frameskip` | `on` or `off`; the default must be one of these |
| `number` | `min = 1`, `max = 3` | Whole decimal numbers in the inclusive range; the default must fit |

Numbers have canonical decimal spelling (`0`, `-2`, `17`), without a
leading plus or extra zeros; the entire range must fit -1000000 to 1000000.
The player steps values with the usual row controls; there is no text
entry and no way for a saved value to add an undeclared option.

RiftWii splits `args` into words **before substitution**. Spaces and tabs
separate words; single or double quotes group them and are removed. Quotes
must be paired. Backslash is an ordinary character in an argument, not an
escape. Empty words (`""`) are refused because the Homebrew Channel's
argument block cannot preserve them. Quotes and spaces in a substituted
value remain inside that one argument and are never split a second time.

| Placeholder | Replaced by |
| --- | --- |
| `{game}` | Full game path, such as `sd:/roms/dreamcast/Power Stone.cdi` |
| `{game_name}` | Filename without its last extension, such as `Power Stone` |
| `{game_folder}` | Folder containing the game |
| `{game_file}` | Filename with its extension, such as `Power Stone.cdi` |
| `{game_device}` | The game's drive without its colon: `sd` or `usb` |
| `{game_path}` | The game's folder without its drive, such as `roms/dreamcast` |
| `{preset}` or another declared setting name | Selected choice or number |
| `{frameskip}` or another declared switch name | The switch's `on` words when enabled; no words when disabled |

Placeholders may be part of a word (`--game={game}`), except a switch
placeholder must occupy a whole word. Unknown or incomplete placeholders
reject the file. Switch `on` words may use all six game placeholders,
but cannot reference settings. An enabled switch absent from `args` adds
its `on` words at the end, in declaration order. An explicit `{frameskip}`
chooses its position instead; a repeated switch still adds its words once.
An off switch contributes no words.

For the template `-game {game} -preset {preset}`, the emulator receives:

```text
argv[0] = sd:/apps/nulldc4wii/boot.dol
argv[1] = -game
argv[2] = sd:/roms/dreamcast/Power Stone.cdi
argv[3] = -preset
argv[4] = powerstone
```

The standard devkitPPC/libogc startup reads the Homebrew Channel argument
block and passes these to `main(int argc, char** argv)`. `argc` is 5 in
that example; `argv[0]` is the full path of the exact DOL being started.
Use the supplied strings as separate values, rather than splitting spaces
again. RiftWii logs each argument with its index in `boot.log`.

The common homebrew starter installs its usual return stub. A normal
libogc exit follows that stub: back to the Homebrew Channel if it started
RiftWii, otherwise to RiftWii when its channel is installed and Quitting a
game is Back to RiftWii, or to the Wii Menu when that is selected. An
emulator that overwrites the stub or starts another program can change
this behavior. RiftWii keeps the running IOS and hardware access.

## Limits

All limits are refusals, not truncation, unless stated as a scan limit.
Lengths are UTF-8 **bytes**, not visible characters.

| Item | Limit |
| --- | --- |
| Manifest | 16384 bytes; each input line at most 2048 bytes |
| Name and system | 64 bytes each |
| Paths | 512 bytes; each component at most 255 bytes |
| Game folders | 8 |
| Extensions | 16, at most 16 bytes each |
| Settings | 16 |
| Setting name / label / help | 32 / 64 / 256 bytes |
| Choices | 32 per setting, at most 96 bytes each |
| Default or saved value | 96 bytes |
| `args` or a switch's `on` template | 1024 bytes each; 64 words, 512 bytes per word |
| HBC command block | 1024 bytes including DOL path, separators and final extra NUL |
| Discovery | 48 add-ons; first 1024 app entries on each drive |
| WiiFlow plugin files | first 512 entries in each drive's `wiiflow/plugins`, counting one folder level below |
| Game scan | 2048 games total; 16384 entries and 512 folders across all add-ons; root plus 4 nested folder levels |

Scans skip hidden entries, invalid paths, names over 255 bytes and SD
symlinks. Overlapping roots within an add-on do not duplicate a game.
The same game declared by two different emulators can appear twice, since
each has its own settings and start action. Scans run with drive discovery,
and Settings > Finding games > Look for games again repeats them. Reaching
a scan limit logs that the remainder was left out. The normal bounded USB
filesystem reader lists each USB directory before the scan applies its
entry budget.

On Home, the Emulators view is next to Homebrew; All games includes these
games too. Their system names are badges, and their tiles show the covers
described below. A game's page has Play, its declared settings,
Reset, and About naming the emulator. Settings > Games > Emulators lists
the found add-ons and lets players change defaults. Only differences from
defaults are saved per game; untouched values follow later default changes.
Reset returns to the current emulator defaults (or to the file's defaults
when used on the defaults page).

Saved overrides live in `sd:/riftwii/emulators/<hash>.txt`. The full
emulator app folder and game path form a SHA-256 key, so games with the
same filename in different folders keep separate settings. Moving an app
or game gives it a new identity. Settings are never written beside the
third-party emulator or its games, and USB is read only.

## Covers

RiftWii checks these sources in order:

1. Your own PNG: `sd:/riftwii/covers/emulators/<system>/<game name>.png`,
   then the same path on `usb:`. Use the manifest's `system` value for the
   folder, and the game's filename without its final extension for the PNG.
   For `system = Dreamcast` and `Power Stone (USA).chd`, use
   `sd:/riftwii/covers/emulators/Dreamcast/Power Stone (USA).png`.
   These work offline and take priority over downloaded covers.
2. Libretro's `Named_Boxarts`, when `covers` is set. The filename is tried
   first, replacing `&*/:` and the backtick, `< > ? \ | "` with underscores
   as libretro requires. If that fails, RiftWii uses a cached list of the
   system's covers to match the title. Close sequels and ambiguous names
   are left without a cover; region variants prefer the filename's region,
   then USA, Europe, Japan. Use standard No-Intro or Redump names for the
   best results.
3. SteamGridDB, only when enabled and you supply your own API key. Sign in
   at [steamgriddb.com](https://www.steamgriddb.com/) and open
   [Preferences > API](https://www.steamgriddb.com/profile/preferences/api).
   Put the key alone on one line in `sd:/riftwii/steamgriddb.txt`. In
   **Settings > Online**, **SteamGridDB covers** says whether a key was
   found and turns this fallback on or off. It starts off. Only exact
   normalized search titles with one game ID are used, with PNG grids of
   600 by 900 pixels. The key is sent in a header to SteamGridDB's HTTPS
   API only, never in a URL, image request or log.

**Settings > Online > Covers for emulator games** starts on. Turning it
off shows only your own PNGs; it also disables SteamGridDB. The general
downloads switch disables both network sources while keeping pictures
already downloaded on the card. Missing covers and system name lists
last a week; **Download missing covers** clears the missing-cover notes
so Home can try again. Each source keeps its own notes, so enabling
SteamGridDB does not inherit libretro's misses.

PNGs are limited to 2 MiB and 1024 pixels per side, checked before pixel
decoding. Downloaded covers are shrunk to the same 80 by 112 pixel format
as Wii covers, in `sd:/riftwii/covers/emu-<game hash>.rwc`. Home, shelf,
carousel and cover zoom share them. The system name list is limited to
2 MiB and 16384 names of at most 255 bytes each; SteamGridDB answers are
limited to 512 KiB, 256 entries and 16 nested levels. Local system/game
names must be safe single filename components. Covers are read and
downloaded on worker threads so the menu keeps responding.

Libretro's thumbnails come from its volunteer contributors, scanners,
collectors, MobyGames and Fandom; the original artwork belongs to the
games' developers and publishers. SteamGridDB's contributors supply the
optional fallback artwork. No covers are shipped with RiftWii.

## WiiFlow plugin files

Many Wii emulators were made to be started by WiiFlow Lite, through its plugin
files. RiftWii reads the player's own plugin files, so an emulator set up for
WiiFlow appears under Emulators with nothing more to write. RiftWii only reads
them: it never writes, moves or ships them, and an emulator's own
`riftwii-emulator.ini` always wins over a plugin file for the same DOL.

RiftWii looks in `sd:/wiiflow/plugins/` and `usb:/wiiflow/plugins/`, and one
folder below them, as plugin packs lay them out. From each file's `[PLUGIN]`
section it reads:

| WiiFlow key | What RiftWii does with it |
| --- | --- |
| `displayname` | The emulator's name and the badge on its games (else the DOL's name) |
| `dolFile` | A full `sd:/` or `usb:/` path; else a path beside the plugin files; else from each drive's root, in that order. A plugin whose DOL isn't there is skipped quietly: packs list many emulators. |
| `romDir` | The games' folder, on both `sd:` and `usb:` |
| `fileTypes` | `\|`-separated endings (`.smc\|.sfc`). Types with two dots, such as `.bin.zip`, are left out: RiftWii matches the last extension. |
| `arguments` | `\|`-separated, one argument each: `{device}`, `{path}`, `{name}`, `{name_no_ext}` and `{loader}` become `{game_device}`, `{game_path}`, `{game_file}`, `{game_name}` and RiftWii's own `boot.dol` (so an emulator that returns to its loader comes back to RiftWii). Any other placeholder refuses the file. |
| `coverFolder` | The game's cover is also looked for at `wiiflow/covers/<coverFolder>/<file name with extension>.png`, where WiiFlow keeps it |
| `magic` | Required, as in WiiFlow. The magics of WiiFlow's own lists (Wii, GameCube, channels, homebrew) are passed over, as are its music player and ScummVM: RiftWii shows those its own way. |

Plugin files get the same limits as RiftWii's own (16 KiB, 32 arguments,
no path that climbs out of its folder) and declare no settings. Each one
used is named in `session.log`, with its DOL.

Plugins sharing a DOL remain separate when their game folders, extensions
or arguments differ. Equivalent configurations are listed once. Saved
settings and game entries use the configuration's own identity, so two
plugins starting the same game with different arguments stay separate.

## Testing an emulator

First check the emulator's own `main` receives the intended arguments
from the Homebrew Channel. Its `meta.xml` `<arguments>` reach `argv` in
the same way:

```xml
<app>
  <name>My emulator</name>
  <arguments>
    <arg>-game</arg>
    <arg>sd:/roms/dreamcast/Power Stone.cdi</arg>
    <arg>-preset</arg>
    <arg>powerstone</arg>
  </arguments>
</app>
```

Then install the manifest, rescan RiftWii and check Play. Compare
`boot.log`'s `Emulators: argv[...]` lines with what `main` receives. Test
a filename and a choice containing spaces, both switch states, both ends
of a number range, separate values for two games, and changing defaults.
The [tiny argument test app](../tools/dolphin/argv-emulator/README.md)
records `argc`/`argv` without needing an emulator, BIOS or game data.
Use it in Dolphin to check Home, settings, the handoff and returning.
USB drive access and the real emulator also need checks on a Wii.
