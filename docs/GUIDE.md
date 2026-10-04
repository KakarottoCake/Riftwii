# RiftWii guide

Everything about installing and using RiftWii. The [README](../README.md) has the short version.

## What you need

- A Wii (or a Wii U in Wii mode) with the Homebrew Channel.
- An SD card, FAT32, for RiftWii itself and your mod packs.
- Your games, any of:
  - the game disc (works on any Wii, nothing else needed);
  - `.wbfs` images (split `.wbf1`, `.wbf2`, ... too) or `.iso` images on
    the SD card or a USB drive. These need a **d2x cIOS** installed in
    slot 249, 250 or 251. A USB drive may be FAT32 or NTFS, or a drive
    formatted as WBFS by a WBFS manager or USB loader (its games are
    listed straight from the drive). The SD card and the USB drive both
    need 512-byte sectors;
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
   A drive formatted as WBFS has no folders: RiftWii lists every
   game on it, and only reads it.
5. Start RiftWii from the Homebrew Channel. The first time, a short
   tour shows the basics (Settings > Tutorial shows it again).

## Using RiftWii

### Home

RiftWii reads the SD card and the USB drive and shows your games as
tiles, with the disc drive first. At first it shows only games that
have mod packs (they carry a **MODS** tag). The round button at the
bottom left (or **1**) switches between games with mods, all games and,
once you have played something, **Recently played**, then
**Favourites** once you mark a game as one on its page. **Minus** and
**Plus** turn to the previous and next page of games (the arrows at the
sides and the D-pad at a page's edge do too). **B** (L on a GameCube
controller) jumps to the next game starting with another letter, A to
Z. The magnifier button above the settings button (Z on a GameCube controller, ZL
on a Classic Controller) opens a keyboard to search every game by name
or game ID; point and press A on the keys, or use the D-pad, and Minus
deletes a letter. A search looks through every game, whatever the view;
press 1 (the view button), or search with nothing typed, to go back to
the view.
To look for new games on the drives, use Settings > Look for games
again (or X on a GameCube controller). RiftWii remembers the view and opens on the last game
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
    the sides; only the TV picture changes, never what the game draws.
    *Remove all (experimental)* also stretches it over the bars at the
    top and bottom. That one changes the game's own frame sizes, and
    some games then show a shifted or broken picture, flash or crash
    (the Kirby games' pink bar; Resident Evil 4 was reported): use
    *Remove* for those. After a game has run once, the page tells you
    which borders it left.
- **Video mode** makes the game use another TV signal: *NTSC (480i)*,
  *PAL 60 Hz*, *PAL 50 Hz*, *480p* (needs a component cable) or *The
  console's* setting. *Game's own* leaves it to the game.
- **Region video fix** (off unless you turn it on) is for a US or
  Japanese game that shows no picture on a console from another region:
  the game is told the video hardware matches its region.
- **Aspect ratio** makes the game use *4:3* or widescreen *16:9*
  whatever the Wii's TV setting says (*Game's own* leaves it). Not every
  game reads the setting in a way that can be changed.
- **Rumble** and **Wii Remote speaker**: turn either off for this game.
- **Region strings fix** (off unless you turn it on) is for a game from
  another region: where the game looks up the console's country names, it
  finds its own region's.
- **Game language** tells the game the console is set to another
  language. Pick one the game has: a game missing it may stop (Super
  Mario Galaxy 2 from the US has no German, and freezes).
- **cIOS** (SD and USB games) picks the d2x slot the game runs under,
  248 to 252. *Automatic* uses the Menu IOS slot. Else, as USB Loader GX
  does, it first tries the d2x cIOS whose base is the IOS the game asks
  for (Super Smash Bros. Brawl gets base 56, which its mods need; a game
  on the SD card gets a base from 56 to 60), then 249, 250 and 251.
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
- **Theme**: steps through *Default* (RiftWii's own light look, no files
  needed) and every folder in `sd:/riftwii/themes/` that has a `theme.ini`,
  shown by the theme's `name` (or the folder name if it has none).
  RiftWii offers to restart its menu to show the new theme. The choice is
  saved as `theme = <folder>` in `sd:/riftwii/settings.txt`
  (`theme = default` for the default). More in [THEMES.md](THEMES.md).
- **Wii Menu button**: *Back to RiftWii* makes the Wii Menu button of a
  game's HOME Menu start RiftWii again (it needs the RiftWii channel);
  *Wii Menu* leaves it as it was.
- **Menu sounds** (Normal, Quiet, Off) and **Menu music**: the music is
  `music.ogg` (Ogg Vorbis, up to 6 MB) from `sd:/riftwii/`, or else the one
  the release puts in `sd:/apps/riftwii/`, looped while the menu is open.
- **In-game screenshots** (experimental, off at first): see Screenshots
  below. The picture is written straight from the screen, using almost
  none of the game's memory (games need it: keeping a copy of the
  picture took 0.8 MB, and Newer Super Mario Bros. Wii and HAL's Kirby
  games stopped on a black screen), so a picture taken during fast
  motion may shear slightly. Some games and mods may still not work with
  it: turn it off for those.
- **Download names and cheats**, and **Get the latest game names**.
- **GameCube adapter** (experimental), and **Check the GameCube adapter** (below).
- **Menu IOS**: IOS 58, or a d2x cIOS slot (248 to 252). Pick the slot that has
  fakemote to use USB DS3/DS4 pads as Wii Remotes.
- **Find network packs (RiiFS)** and **Copy network packs again** (below).
- **Look for games again**, **Updates** (Stable or Beta, below),
  **Check for a new version** (on GitHub; with downloads on, RiftWii also
  looks at every start and asks before it updates), and **Leave RiftWii**,
  which opens the HOME Menu (below).
- **Send a problem report**: see Reporting a problem below.
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

CTGP Revolution 1.03 does not work from RiftWii until further notice.
Its pack replaces `main.dol` with the CTGP-R Channel
(`/apps/ctgpr/boot.dol`), which RiftWii starts the way the Homebrew
Channel does, but it stops on a black screen from a disc and a green
one from USB. Start CTGP from the Homebrew Channel instead; the game
page says so while its pack is on.

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
- Code builds are **experimental**. Tested in Dolphin with Project+ 3.2,
  REX and PMEX Remix, and on a Wii with PMEX Remix from a USB drive (it
  reached a match). Older builds that go in `sd:/codes` with
  `gameconfig.txt` at the top of the card (Project M, Brawl Minus,
  Legacy XP's setup for USB loaders) use the same files.
- These builds use all of the game's memory, so RiftWii leaves its own
  extras out of them: no crash recorder, no in-game screenshots and no
  GameCube controller adapter for Wii U (controllers in the Wii's own
  GameCube ports work as usual). `boot.log` says so for each.
- PMEX Remix is very heavy for a real Wii: it can lag (on the character
  select screen, for one) and its own start screen recommends Dolphin.
  That is the build, not RiftWii.
- `boot.log` lists the codes, where they went and what `gameconfig.txt`
  changed, if something goes wrong.

#### Code builds on a virtual SD card (sd.raw)

A build can also live inside a card image instead of on the SD card
itself: one file, `sd.raw`, holding a whole FAT32 SD card with the
build's folder inside, like the `sd.raw` Dolphin and Project+'s netplay
builds use. The game then gets the image as its SD card. This helps when
the build can't read your real card (an SDXC card, a card formatted
with big clusters), or when you'd rather keep the build on the USB drive.

1. Put the image at `riftwii/sd.raw` on the SD card, or on the USB drive
   (the SD card's is used when both have one). An image from Dolphin
   works as it is: its build folder (`Project+`, `rex_`) goes at the top
   of the image, as on a real card.
2. Open the game's page, then **Mods**. The build inside shows up as
   **Project+ (in sd.raw)**. Turn it on and press Start.

A few things to know:

- The image on the USB drive works with the game on the USB drive too
  (it's read through d2x while you play). With the game on a disc,
  keep the image on the SD card.
- The image should be in one piece or a few: copy it to a freshly
  formatted drive if Start says it is in too many pieces. FAT32 drives
  hold files up to 4 GB, so use an image of 4 GB or less there.
- What the game writes (replays, custom stages, settings) goes into the
  image.
- Builds inside `sd.raw` can't be turned on together with builds on the
  SD card, or with Riivolution packs, yet.
- Tested in Dolphin with Project+ 3.2 in a 3 GB image on the SD card.
  The USB drive is untested so far.

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

> **Warning for Wii U owners: On can freeze the console.** With **On**,
> a game from the SD card or a USB drive sometimes freezes at 97% while
> it starts: the d2x cIOS never answers RiftWii's request for the
> adapter, and the whole cIOS stops with it. The same launch can work
> one time and freeze the next, and RiftWii cannot tell beforehand. Hold
> the power button to turn the console off when it happens. RiftWii asks
> twice before it turns **On** on a Wii U. Leave it on **Automatic**
> unless you are testing the adapter, and send a problem report when it
> freezes.

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

### Screenshots

Hold **1** on a Wii Remote and press **HOME**, or hold **L** and **R** on
a GameCube controller and press **Down**.

- In the menu the screen flashes and the picture goes to
  `sd:/riftwii/screenshots/riftwii-0001.png` (then 0002, and so on). In
  the menu, 1, L and R do their usual job when you let go of them.
- In a game (with Settings > In-game screenshots on) the picture is kept
  on the Wii's internal memory while you play, without pausing the game,
  and goes to `sd:/riftwii/screenshots/<game ID>-0001.png` the next time
  RiftWii starts. The HOME press that finishes the combo never reaches
  the game, so its HOME Menu does not open. A game keeps at most 32
  pictures per session. The Wii Remote combo works with the Wii Remote
  alone, not with buttons on a Classic Controller. The GameCube combo
  works in games that support the GameCube controller.

## Fixes for particular games

RiftWii makes the same fixes USB Loader GX makes for some games, at every
launch:

- **Kirby's Return to Dream Land** checks its own code (MetaFortress) and
  stops on a white screen when anything changed it. Its checks are patched
  out, so screenshots, packs and games on USB work.
- **Prince of Persia: The Forgotten Sands, Driver: San Francisco, The
  Adventures of Tintin and We Dare** check their code too. RiftWii leaves
  them alone: no in-game screenshots, GameCube adapter or crash recording,
  and the video width, deflicker and borders stay as the game has them.
- **Resident Evil 4** clears the top of the Wii's main memory at its
  title screen, where RiftWii keeps the code for in-game screenshots,
  the GameCube adapter and crash recording. It gets none of them, and
  *Return to RiftWii* goes to the Wii Menu instead.
- **New Super Mario Bros. Wii** (its disc check), **Resident Evil 4**
  (GameCube controllers), **Excite Truck** and Kirby from the SD card.
- Every game: the older error #002 check, and the wrong 480p setting some
  games send the video encoder.
- Every game from the SD card or a USB drive: when the game reloads IOS
  itself, it gets the same d2x cIOS again, so the game image stays
  (boot.log: "d2x keeps IOS... across the program's own IOS reloads").
- Cheats in games that start another executable, such as Metroid Prime
  Trilogy's three games: the new one is hooked too, so the cheats keep
  working after the switch.

Boot.log lists them under "Game fixes".

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
the top of the SD card or USB drive, or on a drive formatted as WBFS. The status line at the bottom of
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

**A game boots without mods but goes black with them.** When a pack
reads from the SD card while you play (its files there, or its saves),
RiftWii usually keeps its own IOS running and tells the game it has the
one it asked for. A few games don't accept that (Just Dance 2014 and
other games on IOS57 are switched to their own IOS automatically). For
another game, add `sd_launch_ios = game` to `sd:/riftwii/settings.txt`:
every game then starts on its own IOS. (`sd_launch_ios = menu` keeps
RiftWii's IOS for every game.) Please report the game if it helps.

**RiftWii crashed.** It shows what happened, saves it to
`sd:/riftwii/crash.txt` and starts again (A, RESET, or after a minute).
Back on Home it offers to send a report (below).

**The game crashed.** When a game started from RiftWii crashes, what the
Wii was doing is saved on the way down. Start RiftWii again: it saves
that as `sd:/riftwii/gamecrash.txt` and offers to send a report. A game
that only freezes (no crash) leaves nothing: send a report from
Settings and say what happened.

**The game shows its own error ("An error has occurred") while it
loads.** Send the logs below with your report.

**The GameCube adapter does nothing.** Open **Check the GameCube
adapter**: it says whether the adapter is found and shows what each
port presses. The menu IOS must be IOS 58 or a d2x cIOS, and the game
must support the GameCube controller. The `GameCube adapter` lines in
`boot.log` list the USB devices the Wii saw at launch.

### Reporting a problem

After RiftWii or a game crashes, or a launch fails, RiftWii asks at the next start whether
to send a report. For anything else (a black screen, a game that
freezes, a mod that does not load), use **Settings > Send a problem
report** after starting RiftWii again.

A report holds what it takes to see what went wrong: the logs of this
run and the last one (`session.log`, `session-previous.log`), the last
launch's `boot.log`, `crash.txt` and `gamecrash.txt`, `settings.txt`, the last game's
choices and the XMLs of the packs it had on, the list of files in
`sd:/riivolution` and `sd:/riftwii`, and which console, System Menu,
IOS and cIOS slots, and controllers this is. It is sent to
[paste.rs](https://paste.rs), or to [dpaste.com](https://dpaste.com)
when paste.rs can't be reached, and RiftWii shows its link and a QR code
of it: send that link (on the Discord, or in a GitHub issue) with what
you did and what the screen showed. Anyone who has the link can read
the report, and nothing is sent unless you choose Send.

The report is also saved as `sd:/riftwii/report.txt`. If it could not be
sent (no internet, or neither site answered), send that file instead.

## For developers

- [`docs/MAP.md`](MAP.md): every file, and where to start for each
  feature.
- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md): how a launch works and
  which file owns what.
- [`docs/DEVELOPING.md`](DEVELOPING.md): building (CMake host tests,
  `make -f Makefile.wii` for the Wii app), testing in Dolphin, releasing.
- [`docs/THEMES.md`](THEMES.md): making themes, colour keys, picture
  sizes and music.
- [`docs/README.md`](README.md): every other document.
