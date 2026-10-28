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

`format` is exactly `1`. Any other version, including a future one, is
refused with a line naming the supported version. `dol` may be relative to
the app folder, including subfolders, or an absolute `sd:/` or `usb:/`
path ending in `.dol`. Game folders are absolute SD or USB paths without
a trailing slash. Empty path components, `.` and `..`, backslashes, other
devices and control characters are refused. Extensions have no dot and
contain letters and digits only; matching ignores their case.

## Settings and arguments

Every setting has `label`, `type` and `default`. `help` is optional. Names
are 1–32 lowercase letters, digits or underscores. `game`, `game_name`
and `game_folder` are reserved. Author-supplied labels, help and choice
values pass through RiftWii's translation lookup. English is the fallback;
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
| `{preset}` or another declared setting name | Selected choice or number |
| `{frameskip}` or another declared switch name | The switch's `on` words when enabled; no words when disabled |

Placeholders may be part of a word (`--game={game}`), except a switch
placeholder must occupy a whole word. Unknown or incomplete placeholders
reject the file. Switch `on` words may use the three game placeholders,
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
| Discovery | 16 add-ons; first 1024 app entries on each drive |
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
games too. Their system names are badges, and their tiles use their names
without downloading covers. A game's page has Play, its declared settings,
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
