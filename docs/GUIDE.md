# RiftWii guide

Everything about installing and using RiftWii. The [README](../README.md) has the short version.

## What you need

- A Wii (or a Wii U in Wii mode) with the Homebrew Channel.
- An SD card, FAT32, for RiftWii itself and your mod packs.
- Your games, any of:
  - the game disc (works on any Wii, nothing else needed);
  - `.wbfs` images (split `.wbf1`, `.wbf2`, ... too) or `.iso` images on
    the SD card or a USB drive. These need a **d2x cIOS** installed in
    slot 249, 250 or 251. A USB drive may be FAT32 or NTFS. Both need
    512-byte sectors;
  - Dolphin's compressed `.rvz` images, from the same places (below).

## Installing

1. Download `riftwii-vX.Y.Z-beta.zip` from the
   [releases](https://github.com/KakarottoCake/Riftwii/releases).
2. Copy the zip's `sd-card` folder onto your SD card, merging it with
   what is there. That puts RiftWii in `sd:/apps/riftwii/`.
3. Put your mod packs (an XML file plus the folders it names, as their
   authors ship them) into `sd:/riivolution/` (or
   `sd:/apps/riivolution/`), the same places Riivolution uses. Packs
   can also go in `usb:/riivolution/` on the USB drive, FAT32 or NTFS
   (see below). Code builds (Project+, REX) must stay on the SD card.
4. Game images go in `wbfs` (`.wbfs` or `.iso`, also in `Title [ID]`
   folders the way USB Loader GX keeps them) or `games` (`.iso`, `.rvz`)
   at the top of the SD card or the USB drive. The zip's `usb-drive` folder shows
   where. Games kept somewhere else can be added with a line in
   `sd:/riftwii/settings.txt`, folders split by `;`:
   `game_folders = /Wii Games; usb:/iso/wii` (a plain `/path` is looked
   for on both drives; `sd:/` or `usb:/` in front limits it to one). A
   drive with several partitions is read from the one holding those
   folders, `wbfs` or `games`.
5. Start RiftWii from the Homebrew Channel. The first time, a short
   tour shows the basics (Settings > Tutorial shows it again).

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
disc's SUPER MARIO GALAXY MORE), in the menu's language. The line at the
bottom says how many games the view holds.

With Screen Burn-In Reduction on in the Wii's own settings, the menu dims
after five minutes without input, as the Wii Menu does. Any button, or
moving the pointer, brings it back; that press does nothing else.

The tiles show each game's cover, the name of the one you point at
under them. Covers come from GameTDB while Home is open (about a second
each, the page on screen first) and are kept in `sd:/riftwii/covers`;
a game GameTDB has no cover for shows its name. **Home tiles** in
Settings switches to name tiles.

### A game's page

Games you start show on the Wii Message Board with how long you
played, as discs from the Wii Menu do (`message_board = off` in
`sd:/riftwii/settings.txt` turns that off).

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
keeps it going); the D-pad works too. A longer list has up and down
arrows on its right edge: point at one and press A to move a row, or hold
A to keep going. Each arrow goes away at its end of the list.

### Settings

The gear at the bottom right (or **2**) opens Settings. The note under
the list explains the row you are on.

- **Language**: English, Español, 日本語, Português, Italiano, or *Wii* to
  follow the console. A translation can be corrected by putting a copy of
  `wii/lang/<lang>.po` at `sd:/riftwii/lang/<lang>.po`.
- **Picture width**, **Deflicker**, **Black borders**, **Video mode**,
  **Game language**, **Game cIOS**, **Online server**: the defaults for
  every game.
- **Home tiles**: covers or names.
- **Wii Menu button**: *Back to RiftWii* makes the Wii Menu button of a
  game's HOME Menu start RiftWii again (it needs the RiftWii channel);
  *Wii Menu* leaves it as it was.
- **Menu sounds** (Normal, Quiet, Off) and **Menu music**: the music is
  `music.ogg` (Ogg Vorbis, up to 6 MB) from `sd:/riftwii/`, or else the one
  the release puts in `sd:/apps/riftwii/`, looped while the menu is open.
- **Download names and cheats**, and **Get the latest game names**.
- **GameCube adapter** (experimental), and **Check the GameCube adapter** (below).
- **Menu IOS**: IOS 58, or a d2x cIOS slot (248 to 252). Pick the slot that has
  fakemote to use USB DS3/DS4 pads as Wii Remotes.
- **Find network packs (RiiFS)** and **Copy network packs again** (below).
- **Look for games again**, **Updates** (Stable or Beta, below),
  **Check for a new version** (on GitHub; with downloads on, RiftWii also
  looks at every start and asks before it updates), and **Leave RiftWii**,
  which opens the HOME Menu (below).
- **Tutorial** shows the short tour of the basics again: a new SD card
  starts with it, once.
- **Credits and licence**: RiftWii's licence (the GNU GPL, version 3 or
  later, in full), where its source is, and who its parts come from.

### HOME Menu

HOME on Home (or **Leave RiftWii** in Settings) opens the HOME Menu, as
on the Wii: **Homebrew Channel**, **Wii Menu**, **Priiloader** (its menu;
the Wii Menu if Priiloader is not installed) or **Power off**. **Close**,
B or HOME goes back, so a HOME pressed by mistake costs nothing. Without
a pointer, the D-pad moves between the buttons, starting on Close. The
bar at the bottom shows each Wii Remote's batteries.

### RVZ games

Dolphin's compressed `.rvz` images play straight from `games` on the SD
card or the USB drive, without turning them back into an ISO, and packs
work on them as on any game. RiftWii checks each RVZ when you pick it:

- Zstandard (any level) or no compression, with chunks of 32 to 128 KiB,
  plays. Most RVZs are made this way.
- Chunks of 256 or 512 KiB play after a warning (press Start twice).
- Larger chunks, bzip2, LZMA and LZMA2, WIA files and GameCube images are
  refused, with the reason.

The SD card must be in the Wii even for an RVZ on the USB drive: RiftWii
keeps a small index of each game in `sd:/riftwii/rvz/`. An RVZ on the
drive is read through d2x (the drive needs 512-byte sectors). An RVZ
copied in more than 1024 pieces has to be copied again. More in
[RVZ.md](RVZ.md).

### Burned discs (experimental)

A Wii game burned to a DVD-R or DVD+R (the `.iso` as it is) plays from
the disc drive on older Wiis only: later Wii drives read nothing but
Nintendo's own discs, and no software changes that. To see whether
yours can, play a movie DVD in WiiMC.

The Wii's own IOS refuses a burned disc, so when the Disc tile cannot
read one, RiftWii offers to restart its menu under your d2x cIOS for
that session. d2x reads the burn as a plain DVD, and the game then
starts as from any disc, mods included. With the menu IOS set to a d2x
slot in Settings, burned discs work straight away.

**Burned discs are at your own risk.** A burned disc reflects less light
than a pressed one, so the drive works harder and retries more, and a
drive that plays many of them can wear out sooner. RiftWii says so
before each launch of one. Use good discs (Verbatim DVD-R), burn slowly
(4x or less) and re-burn a disc that loads slowly. Games on a USB drive
or the SD card, and a USB DVD drive (below), do not use the Wii's drive
at all.

### USB DVD drives (experimental)

On a Wii whose own drive cannot read burned discs, a USB DVD drive can:
burn the game's `.iso` as it is to a DVD-R or DVD+R, put it in the drive
and plug the drive in instead of a USB hard drive (d2x uses one USB
device at a time). RiftWii lists the disc among the USB games and d2x
plays it straight from the drive, mods included. Pressed Nintendo discs
do not work: computer DVD drives cannot read them.

Most USB DVD drives need more power than one Wii USB port gives: use a
drive with a Y-cable in both ports (the data plug in port 0, the one on
the edge of the Wii) or one with its own power supply. Expect disc-like
loading, a little slower on seeks. d2x reads the drive every 10 seconds
while it is idle, so it does not go to sleep mid-game.

### Network packs (RiiFS)

Packs can come from a PC running a RiiFS server, as with Riivolution.
Put an XML in `sd:/riivolution` with
`<network protocol="riifs" address="192.168.1.20" port="1137"/>`, or
turn on *Find network packs* in Settings to look for servers on your
network. A server's packs show with `@ address` after their name.
RiftWii copies what a launch needs into `sd:/riftwii/riifs/` first (only
files whose size changed), then boots from the card. Saves stay on the
card. More in [RIIFS.md](RIIFS.md).

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

### Code builds (Project+, REX and other Gecko code mods)

Some mods are not Riivolution packs at all. Project+ and builds made
from it (REX, for Super Smash Bros. Brawl) are a big Gecko code file
that loads the mod's files from the SD card while the game runs. They
usually come with their own launcher or a USB loader set up for them.
You don't need either: RiftWii runs the codes itself, and the game can
be on a disc or a USB drive.

1. Copy the build's folder to the top of the SD card, as the build's
   instructions say (`sd:/Project+`, `sd:/rex_`, with the `pf` folder
   inside).
2. Keep the build's `gameconfig.txt` (Project+ and Legacy XP call it
   `gc.txt`) inside the build's folder, or copy it to the top of the SD
   card. It says where the codes go in memory, and RiftWii needs it for
   any build bigger than a handful of codes.
3. Open the game's page, then **Mods**. The build shows up as
   **Project+ (codes)** (or whatever its folder is called). Turn it on
   and press Start.

RiftWii finds a build by its code file, named after the game
(`RSBE01.GCT` for Brawl, any case), in a folder at the top of the card
or in that folder's `codes` folder. Plain code files work too: put
`<game ID>.gct` in `sd:/codes` and it shows up as **sd:/codes**.

If a build doesn't show up (its code file has another name, or sits
deeper), pick it yourself: **Add a code build...** at the bottom of the
Mods page lets you open the build's folder on the SD card and pick its
`.gct` file. It's turned on and remembered for that game.
**Remove from the list** under it takes it off again (the files stay on
the card).

You can skip the build's `apps` folder (the launchers). Cheats you pick
for the game run alongside the build.

A few things to know:

- Play the game from the disc or a USB drive. The build reads the SD
  card while the game runs, so RiftWii won't start it with the game on
  the SD card. The build's folder has to be on the SD card too: one on
  the USB drive is found and refused, with a note saying to move it.
- If Start says the codes don't fit, `gameconfig.txt` (or `gc.txt`) is
  missing or is not the build's own. Copy the one that came with the
  build.
- Tested in Dolphin with Project+ 3.2 and REX. Older builds that go in
  `sd:/codes` with `gameconfig.txt` at the top of the card (Project M,
  Brawl Minus, Legacy XP's setup for USB loaders) use the same files.
- `boot.log` lists the codes, where they went and what `gameconfig.txt`
  changed, if something goes wrong.

### GameCube controller adapter for Wii U

The Nintendo adapter (WUP-028) or a copy that works like it (a Mayflash
with its switch on Wii U). Plug the adapter's black USB plug into the Wii
before you start the game (the grey one only adds power for rumble). In
games that support the GameCube controller (Mario Kart Wii, Super Smash
Bros. Brawl and others), its controllers fill the ports that have no
controller plugged in, rumble included. Its controllers work the RiftWii
menu too, like a GameCube controller in the Wii's own ports.
**Check the GameCube adapter** shows what each port reports before you
start a game.

**GameCube adapter** in Settings is **Automatic** at first: the adapter
is used when it is plugged in as the game starts, and the game is left
alone when it is not. **On** always sets it up, so it can be plugged in
during the game; **Off** never does. `boot.log` says what was found.

It needs a menu IOS with USB HID: IOS 58 (the default) or a d2x cIOS;
the game then keeps that IOS. On a Wii U it needs IOS 58. It stays off
for games read from the USB drive (and RVZ games or packs read from it),
where it broke the game's disc reads. **On** tries it there anyway (an
experiment: if the game then fails to read its disc, go back to
**Automatic**). RVZ games are the exception: the adapter stays off for
every RVZ game, even with **On**, until the conflict is solved
([issue #4](https://github.com/KakarottoCake/Riftwii/issues/4)). Games without GameCube controller support
ignore it, and so do mods that bring their own controller code (mkwcat's
NSMBW project) or read the controller hardware directly (Gecko codes
that add GameCube controls to NSMBW).

### For pack authors

RiftWii reads the whole documented Riivolution patch format
(<https://riivolution.github.io/wiki/Patch_Format/>): `<file>` (with
`offset`, `fileoffset`, `length`, `resize` and `create`), `<folder>`,
`<memory>` (plain, `search` and `ocarina`, `value` or `valuefile`),
`<savegame>`, `<shift>`, `<network>`, `<macro>` and `<param>`, `shiftfiles`, and the
`{$__gameid}`, `{$__region}`, `{$__maker}`, `{$__ngid}` and param
placeholders. Unknown attributes and elements are tolerated and noted in
the log. Packs match a game by its `<id>` (game ID, revision and disc
number).

It reads an XML the way Riivolution does, so a pack that works there
works here:
- XMLs are read from `sd:/riivolution` and `sd:/apps/riivolution`, and
  from `usb:/riivolution` and `usb:/apps/riivolution` (listed with
  "@ USB"). A `root` without a leading `/` starts in the XML's folder,
  and no `root` means that folder.
- A pack on the USB drive reads its files from that drive while the
  game runs. This needs a d2x cIOS: an SD or USB game already runs
  under one; for a disc, set Menu IOS to your d2x slot in Settings. The
  drive may be FAT32 or NTFS, with 512-byte sectors (not 4K-sector
  drives). Files must not be compressed or sparse on NTFS; files small
  enough for NTFS to keep inside its file table are carried in memory.
  The pack's save folder and Riivolution's `file` device stay on the SD
  card, so a Pulsar pack (Retro Rewind and the like) on USB keeps its
  settings, ghosts and leaderboards on the SD card.
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

- `<shift source destination>` makes the destination disc file read the
  source's data (after the pack's other patches). Both must be disc
  files; otherwise the shift is skipped with a note.

The log notes each yes/no value, hex value and cut-off text read this way.
`<dlc>` (Rock Band downloadable content) is not supported.

### Controls

A Wii Remote (pointer or D-pad), Classic Controller or GameCube
controller, with the same button names on all of them. The GameCube
control stick and the Classic Controller's left stick move a pointer
like a Wii Remote's; the D-pad moves the highlight. With no pointer on
screen, only the highlighted tile or row answers to A.

The power button, on the Wii or on a Wii Remote, turns the Wii off from
the menu: the screen fades out and the Wii goes fully off (red light).
The HOME Menu's power off follows the Wii's own setting instead (yellow
with WiiConnect24 on).

## Stable and beta versions

Stable versions (2.1.0, 2.1.1, ...) are the ones testers have checked on
real Wiis. Beta versions (2.1.1-beta, 2.2.0-rc1, ...) come more often and
carry new fixes and features that are not checked yet. **Settings >
Updates** picks which ones RiftWii offers: **Stable** only stable
versions, **Beta** every new version. A stable version starts on Stable
and a beta on Beta.

**Experimental** features are in stable versions too, but they are not
yet confirmed on real Wiis and may not work on yours. The menu marks
them (a note before the launch, or "Experimental" in the Settings note):

- **The GameCube controller adapter** in games (it works in the menu on a
  Wii U). It stays off for RVZ games
  ([issue #4](https://github.com/KakarottoCake/Riftwii/issues/4)).
- **RVZ games**: if one does not start or play, use a WBFS or ISO copy.
- **Packs on the USB drive** (`usb:/riivolution`): if the game does not
  start, copy the pack to the SD card.

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
must support the GameCube controller. The `GameCube adapter` lines in
`boot.log` list the USB devices the Wii saw at launch.

### Reporting a problem

Open a GitHub issue with:

- `sd:/riftwii/session.log` (the menu) and `sd:/riftwii/boot.log` (the
  last launch), and `sd:/riftwii/crash.txt` if RiftWii crashed;
- your Wii model, System Menu version and which cIOS you have;
- what you did, and what the screen showed (a photo helps).

## For developers

- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md): how a launch works and
  which file owns what.
- [`docs/DEVELOPING.md`](DEVELOPING.md): building (CMake host tests,
  `make -f Makefile.wii` for the Wii app), testing in Dolphin, releasing.
- [`docs/README.md`](README.md): every other document.
