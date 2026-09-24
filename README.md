# RiftWii

RiftWii loads Wii game mods from your SD card into games played from
the disc, a USB drive or the SD card; the game files themselves are never
changed. You pick one or more mod packs from a menu,
and the game starts with the mods applied. It reads the same XML mod
packs that Riivolution uses, and it was written from scratch without
copying Riivolution code (see `NOTICE.md`). Licence: GPL-3.0-or-later
(`LICENSE`); third-party notices in `NOTICE.md`.

> 2.0 Beta. The menu and the mod engine are checked by host tests and in
> the Dolphin emulator, and players run RiftWii on real Wiis. SD and USB
> game loading depend on d2x and are the least proven, as is the new
> GameCube adapter support. Please report anything odd as a GitHub issue
> (see [Reporting a problem](#reporting-a-problem)).

## About AI assistance

Yes, RiftWii was written with AI assistance, and I won't pretend
otherwise. But it was not vibecoded. Every step was supervised closely,
because even the best models make terrible calls when you just throw
them at a project and walk away. Left alone, they suggested "fixes" like
patching discs at random based on assembly fingerprints to make a
problem go away, and seemed to think that was fine. I can't imagine what
a truly vibecoded version of this would look like. The design decisions,
the testing on real hardware and the refusal to ship shortcuts like that
are mine.

This is a hobby project. As a kid I wanted to play mods straight from a
USB drive, and Riivolution never allowed it; its developer was firmly
against USB loading. RiftWii exists to finally get past that.

## What you need

- A Wii (or a Wii U in Wii mode) with the Homebrew Channel.
- An SD card, FAT32, for RiftWii itself and your mod packs.
- Your games, any of:
  - the game disc (works on any Wii, nothing else needed);
  - `.wbfs` images (split `.wbf1`, `.wbf2`, ... too) or `.iso` images on
    the SD card or a USB drive. These need a **d2x cIOS** installed in
    slot 249, 250 or 251. A USB drive may be FAT32 or NTFS. Both need
    512-byte sectors. Compressed `.rvz` images are not supported.

## Installing

1. Download `riftwii-vX.Y.Z-beta.zip` from the
   [releases](https://github.com/KakarottoCake/Riftwii/releases).
2. Copy the zip's `sd-card` folder onto your SD card, merging it with
   what is there. That puts RiftWii in `sd:/apps/riftwii/`.
3. Put your mod packs (an XML file plus the folders it names, as their
   authors ship them) into `sd:/riivolution/` (or
   `sd:/apps/riivolution/`), the same places Riivolution uses.
4. Game images go in `wbfs` (`.wbfs`) or `games` (`.iso`) at the top of
   the SD card or the USB drive. The zip's `usb-drive` folder shows
   where.
5. Start RiftWii from the Homebrew Channel.

## Using RiftWii

### Home

RiftWii reads the SD card and the USB drive and shows your games as
tiles, with the disc drive first. At first it shows only games that
have mod packs (they carry a **MODS** tag). The round button at the
bottom left (or **1**) switches between games with mods, all games and,
once you have played something, **Recently played**, then
**Favourites** once you mark a game as one on its page. **Minus** (L on
a GameCube controller) jumps to the next game starting with another
letter, A to Z. RiftWii remembers the view and opens on the last game
you played. When the Wii is online,
games show their real names from GameTDB (Super Mario Galaxy 2, not the
disc's SUPER MARIO GALAXY MORE), in the menu's language.

### A game's page

Pick a game to open its page. It shows how often you played it, and
these rows:

- **Mods** opens the packs made for this game. Each pack has an On/Off
  switch; once it is on, its settings show under it, with arrows on
  either side of the value (Minus steps back). A pack whose XML is broken
  shows the error under it. Settings that several packs share show once,
  as in Riivolution.
- **Saves**: *On the Wii* saves as usual. *SD, from Wii save* keeps this
  game's saves on the SD card, starting from a copy of the Wii's save
  (`sd:/riftwii/saves/<ID>/clone`); *SD, fresh start* starts a new one
  (`sd:/riftwii/saves/<ID>/fresh`). While a pack that brings its own
  saves is on, the row reads *Kept by the pack*.
- **Cheats** opens the game's cheat list. When the Wii is online, the
  first visit downloads the latest cheats from the GeckoCodes archive
  (**Download** gets them again later). Tick the ones you want; **Use
  cheats** turns them all on or off. The list is a plain text file,
  `sd:/riftwii/cheats/<ID>.txt`, in the format other loaders use, so you
  can add your own on a computer. Codes with values to fill in (`XXXX`)
  show *Edit first* until you do.
- **Picture width**, **Deflicker** and **Black borders** change how the
  game draws its picture, like USB Loader GX's video settings:
  - *Picture width*: 720 fills the TV from side to side; many games
    draw 640 pixels and leave black bars that old TVs hid. *Framebuffer*
    matches the game's drawing width; 704 is the broadcast-safe width.
  - *Deflicker*: the filter that blurs the picture to hide interlace
    flicker. *Off* gives the sharpest picture, especially over
    component or HDMI.
  - *Black borders*: *Remove* stretches the picture over the bars at
    the top and bottom. After a game has run once, the page tells you
    which borders it left.
- **Video mode** makes the game use another TV signal: *NTSC (480i)*,
  *PAL 60 Hz*, *PAL 50 Hz*, *480p* (needs a component cable) or *The
  console's* setting. *Game's own* leaves it to the game.
- **Game language** tells the game the console is set to another
  language. Pick one the game has: a game missing it may stop (Super
  Mario Galaxy 2 from the US has no German, and freezes).
- **cIOS** (SD and USB games) picks the d2x slot the game runs under,
  248 to 252. *Automatic* uses the Menu IOS slot, else 249, 250, 251.
- **Online server** lets the game play online again (below).

  *Default* follows Settings, where the same rows apply to every game.

**Start** (or Plus) boots the game with what you chose; with nothing on,
the game starts as it is. While it starts, its progress prints on
screen. Your choices are saved for each game.

In every list, hold A and move the Wii Remote to drag it (a quick flick
keeps it going); the D-pad works too.

### Settings

The gear at the bottom right (or **2**) opens Settings. The note under
the list explains the row you are on.

- **Language**: English, Español, 日本語, Português, Italiano, or *Wii* to
  follow the console. A translation can be corrected by putting a copy of
  `wii/lang/<lang>.po` at `sd:/riftwii/lang/<lang>.po`.
- **Picture width**, **Deflicker**, **Black borders**, **Video mode**,
  **Game language**, **Game cIOS**, **Online server**: the defaults for
  every game.
- **Download names and cheats**, and **Get the latest game names**.
- **GameCube adapter**, and **Check the GameCube adapter** (below).
- **Menu IOS**: IOS 58, or a d2x cIOS slot (248 to 252). Pick the slot that has
  fakemote to use USB DS3/DS4 pads as Wii Remotes.
- **Find network packs (RiiFS)** and **Copy network packs again** (below).
- **Look for games again**, **Check for a new version** (on GitHub; with
  downloads on, RiftWii also looks once a day at start and says so on
  Home), and **Leave RiftWii** (HOME does that too).

### Network packs (RiiFS)

Packs can come from a PC running a RiiFS server, as with Riivolution.
Put an XML in `sd:/riivolution` with
`<network protocol="riifs" address="192.168.1.20" port="1137"/>`, or
turn on *Find network packs* in Settings to look for servers on your
network. A server's packs show with `@ address` after their name.
RiftWii copies what a launch needs into `sd:/riftwii/riifs/` first (only
files whose size changed), then boots from the card. Saves stay on the
card. More in `docs/RIIFS.md`.

### Online play

Nintendo's Wi-Fi Connection closed in 2014; **Online server** points a
game at a replacement:

- *Wiimmfi* (https://wiimmfi.de). Mario Kart Wii gets Wiimmfi's own
  patch, as USB Loader GX applies it.
- *WiiLink WFC* (https://wfc.wiilink.ca), for the games in WiiLink's
  list: the game downloads WiiLink's patch when it connects, as WiiLink's
  own launcher does it. Online communications credit to WiiLink WFC.
- *AltWFC* (zwei.moe).
- *Custom*: the server in `wfc_domain = <domain>` in
  `sd:/riftwii/settings.txt` (4 to 16 characters, as it replaces
  "nintendowifi.net").

Nothing changes while packs are on: Mario Kart Wii distributions bring
their own online setup. On AltWFC or a custom server, Mario Kart Wii
also gets the fix for its remote code execution hole (Wiimmfi's and
WiiLink's patches fix it themselves).

### Mario Kart Wii distributions

Pulsar packs (Retro Rewind and others) save their settings, ghosts and
leaderboards to the SD card, as they do under Riivolution. CT-CODE
packs that replace the game's `main.dol` (CTGP Revolution 1.02) work
too.

### GameCube controller adapter for Wii U

The Nintendo adapter (WUP-028) or a copy that works like it. Turn on
**GameCube adapter** in Settings and plug the adapter's black USB plug
into the Wii (the grey one only adds power for rumble). In games that
support the GameCube controller (Mario Kart Wii, Super Smash Bros. Brawl
and others), its controllers fill the ports that have no controller
plugged in, rumble included. **Check the GameCube adapter** shows what
each port reports before you start a game.

It needs a menu IOS with USB HID: IOS 58 (the default) or a d2x cIOS;
the game then keeps that IOS. Games without GameCube controller support
ignore it, and so do mods that bring their own controller code (mkwcat's
NSMBW project) or read the controller hardware directly (Gecko codes
that add GameCube controls to NSMBW).

### For pack authors

RiftWii reads the whole documented Riivolution patch format
(<https://riivolution.github.io/wiki/Patch_Format/>): `<file>` (with
`offset`, `fileoffset`, `length`, `resize` and `create`), `<folder>`,
`<memory>` (plain, `search` and `ocarina`, `value` or `valuefile`),
`<savegame>`, `<network>`, `<macro>` and `<param>`, `shiftfiles`, and the
`{$__gameid}`, `{$__region}`, `{$__maker}`, `{$__ngid}` and param
placeholders. Unknown attributes and elements are tolerated and noted in
the log. Packs match a game by its `<id>` (game ID, revision and disc
number).

It reads an XML the way Riivolution does, so a pack that works there
works here:
- XMLs are read from `sd:/riivolution` and `sd:/apps/riivolution`. A
  `root` without a leading `/` starts in the XML's folder, and no
  `root` means that folder.
- Only `yes` and `true` (any case) mean yes; any other value means no.
- A hex value with an odd number of digits loses its last digit.
- A `{$name}` no param sets becomes empty. An option's params win over
  its choice's, and over a macro's.
- An option that `<macro>`s copy is a template: the copies take its
  place in the list and it is not shown itself.
- Anything after the last `>` in the file is ignored.
- The choices are shared with Riivolution through its own file,
  `sd:/riivolution/config/<first 4 letters of the game ID>.xml`. A game
  RiftWii has never saved choices for starts from that file, and every
  change in RiftWii is written back to it, so both loaders turn on the
  same mods.

The log notes each yes/no value, hex value and cut-off text read this way.
`<shift>` and `<dlc>` are not supported yet.

### Controls

A Wii Remote (pointer or D-pad), Classic Controller, GameCube controller
or Wii U GamePad (same button names; X stands in for 1). The GameCube
control stick and the Classic Controller's left stick move a pointer
like a Wii Remote's; the D-pad moves the highlight. With no pointer on
screen, only the highlighted tile or row answers to A.

## Troubleshooting

**My games don't show up.** Home first shows only games that have mod
packs: press **1** for all games. Images must be in `wbfs` or `games` at
the top of the SD card or USB drive. The status line at the bottom of
Home says what went wrong with a drive.

**"No d2x cIOS in 249-251: games cannot boot yet".** SD and USB images
need a d2x cIOS. The disc drive works without one.

**The USB drive isn't found.** Try a drive with its own power supply.
If the menu IOS is set to a cIOS, it must be based on IOS 58 for USB
drives to work in the menu; otherwise set **Menu IOS** back to IOS 58.

**A pack says "Broken".** Its XML has an error, shown under the pack.
Fix the file on a computer and come back.

**A mod doesn't start.** RiftWii never starts a game half patched: the
screen names the pack, option and file that stopped it. A memory patch
whose file is missing is skipped with a warning (as in Dolphin). Press A
to go back to RiftWii (it also goes back by itself after two minutes),
or HOME to leave to the Homebrew Channel.

**RiftWii crashed.** It shows what happened, saves it to
`sd:/riftwii/crash.txt` and starts again (A, RESET, or after a minute).
Please send that file with a report.

**The game shows its own error ("An error has occurred") while it
loads.** Send the logs below with your report.

**The GameCube adapter does nothing.** Open **Check the GameCube
adapter**: it says whether the adapter is found and shows what each
port presses. The menu IOS must be IOS 58 or a d2x cIOS, and the game
must support the GameCube controller.

### Reporting a problem

Open a GitHub issue with:

- `sd:/riftwii/session.log` (the menu) and `sd:/riftwii/boot.log` (the
  last launch), and `sd:/riftwii/crash.txt` if RiftWii crashed;
- your Wii model, System Menu version and which cIOS you have;
- what you did, and what the screen showed (a photo helps).

## For developers

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): how a launch works and
  which file owns what.
- [`docs/DEVELOPING.md`](docs/DEVELOPING.md): building (CMake host tests,
  `make -f Makefile.wii` for the Wii app), testing in Dolphin, releasing.
- [`docs/README.md`](docs/README.md): every other document.
