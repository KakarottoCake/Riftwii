# RiftWii guide

Everything about installing and using RiftWii. The [README](../README.md) has the short version.

## What you need

- A Wii (or a Wii U in Wii mode) with the Homebrew Channel.
- An SD card, FAT32, for RiftWii itself and your mod packs.
- Your games, any of:
  - the game disc (works on any Wii, nothing else needed);
  - `.wbfs` images (split `.wbf1`, `.wbf2`, ... too) or `.iso` images on
    the SD card or a USB drive. These need a **d2x cIOS, v11 beta3 or
    newer** ([wii.hacks.guide/cios](https://wii.hacks.guide/cios)),
    installed in slot 249, 250 or 251: RiftWii does not start them on an
    older d2x or on a cIOS that is not d2x. A USB drive may be FAT32 or NTFS, or a drive
    formatted as WBFS by a WBFS manager or USB loader (its games are
    listed straight from the drive). The SD card and the USB drive both
    need 512-byte sectors. A Wii U's own storage drive (formatted by the
    Wii U) can't be read: RiftWii recognises it, says so once, and leaves
    it alone, so it can stay plugged in;
  - Dolphin's compressed `.rvz` images, from the same places (below).

## Installing

1. Download the newest `riftwii-vYYMM-N.zip` from the
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
**Favourites** once you mark a game as one (the star on its banner, or
**Favourite** on its page). **Minus** and
**Plus** turn to the previous and next page of games (the arrows at the
sides and the D-pad at a page's edge do too). Hold **B** (L on a
GameCube controller) and press **Right** or **Left** to jump to the first
game of the next or the previous letter, as in WiiFlow (Down and Up turn a
page); B pressed alone jumps to the next letter. The magnifier button above the settings button (Z on a GameCube controller, ZL
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
Settings switches to 3D covers, the carousel, the shelf, channels or
name tiles.

On the **shelf**, your games stand as boxes, spine out. The one you're
on slides out and turns to show its front; Left and Right move along the
shelf (hold one to speed up), Minus and Plus jump 12 games, and A opens
the game. Up or Down turns the box round to show its back. With the
pointer, point at a spine (it lifts), press A to pull that box out, and
A again to open it; hold A and move the pointer sideways to drag the
shelf, and let go while moving to send it gliding. Behind the shelf
is a blurred copy of the game's cover, and under its name are where the
game is, its region, how often you've played it and whether it has mods.
Each box's spine and front come from GameTDB's full
cover scan, fetched after the covers while Home is open and kept in
`sd:/riftwii/boxes`. A game GameTDB has no scan for gets a white spine
with its name and its cover on the front.

The **carousel** uses the same boxes, fanned out on an arc with every
front showing: the one you're on stands forward in the middle, and the
others turn away and drop back the further out they are. It moves and
opens games as the shelf does.

With **3D covers**, GameTDB's pictures of the cases (each one turned a
little) stand close together round a ring with no end: past the last game
comes the first again. The one you're on stands large in front and the
ring's far side shows small behind it. Left and Right turn the ring (hold
one to speed up), and the pointer picks and drags as on the shelf. The
pictures come from GameTDB while Home is open and are kept in
`sd:/riftwii/covers3d`; a game without one shows its cover meanwhile.

With **Channels**, each tile plays the game's own animated icon, the one
the Wii Menu shows for a disc channel, and the tile you're on shows the
game's name. Pick a game and its full banner plays, as the Wii Menu opens
a channel: **Start** (A on it, or Start on a GameCube controller) plays
the game with the mods and settings already chosen for it, **Settings**
(or 2) opens the game's page to change them (B there comes back to the
banner), the star in the top right corner (or 1) makes it a favourite,
the arrows (or + and -, or Left and Right) go to the game after or
before, and B or HOME return to Home. Each game's banner is read from its image the first time its page
of tiles is shown (a moment per game) and kept in `sd:/riftwii/banners`.
For ISO and WBFS games RiftWii decrypts it with your Wii's own key, read
from the console while it runs; nothing like it ships with RiftWii or is
written to the card. A game without a banner shows its name tile.

### A game's page

Games you start show on the Wii Message Board with how long you
played, as discs from the Wii Menu do (`message_board = off` in
`sd:/riftwii/settings.txt` turns that off).

Pick a game to open its page: its box on the left, its name in the band
at the top with a line of facts (its ID, year, genre, how many can play,
whether it plays online, how often you played it), and large tiles. The
D-pad moves between the tiles, A opens the one that is lit, and the line
under them says what it does. The prompts at the bottom right name the
buttons that work on the screen (A, B, +); with the pointer, they can be
pressed too.

- **Play** (the large tile, or Plus) starts the game; it says how many
  mods are on.

- **Mods** opens the packs made for this game. Each pack has an On/Off
  switch; once it is on, its settings show under it, with arrows on
  either side of the value (Minus steps back). A pack whose XML is broken
  shows the error under it. Settings that several packs share show once,
  as in Riivolution.
- **About** shows what GameTDB says about it: when it came
  out, its genre, who made it, its age rating, how many can play (and
  online, with the server it would use), the controllers it needs and
  those it also takes, each marked *Connected* or *Not connected* when
  the Wii can tell, and its story. The details come from GameTDB's
  database of every Wii game, `sd:/riftwii/wiitdb-<language>.zip` (about
  2 MB, in the menu's language with English where there is no
  translation): the first visit offers to download it, and a `wiitdb.xml`
  another loader left on the card is used when it is there. The first page opened reads
  it for all your games at once (a few seconds); after that they open at
  once, from `sd:/riftwii/gameinfo-<language>.txt`.
- **Saves**: *On the Wii* saves as usual. *SD, from Wii save* keeps this
  game's saves on the SD card, starting from a copy of the Wii's save
  (`sd:/riftwii/saves/<ID>/clone`); *SD, fresh start* starts a new one
  (`sd:/riftwii/saves/<ID>/fresh`). While a pack that brings its own
  saves is on, the row reads *Kept by the pack*. For a game from the SD
  card or a USB drive, d2x itself keeps the save on the card (its NAND
  emulation, as in USB Loader GX); a disc game's is kept by RiftWii. The
  save is in the folder's `title/<type>/<ID in hex>/data`.
- **Cheats** opens the game's cheat list. When the Wii is online, the
  first visit downloads the latest cheats from the GeckoCodes archive
  (**Download** gets them again later; cheats you added yourself and
  values you filled in stay). Tick the ones you want; **Use cheats**
  turns them all on or off. The list is a plain text file,
  `sd:/riftwii/cheats/<ID>.txt`, in the format other loaders use, so you
  can add your own on a computer. Codes with values to fill in (`XXXX`)
  show **Set values**: A asks for each value on a keypad (0-9, A-F; +
  is OK), with the cheat's notes above it, and turns the cheat on.
  **Its values** under it changes them later.
- **Favourite** (under the box) puts the game in Home's Favourites view.
- **Options** opens the game's own settings, a list of rows; the tile
  says how many differ from Settings:
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
  whatever the Wii's TV setting says (*Game's own* leaves it; *Default*
  follows Settings > Aspect ratio). Not every game reads the setting in a
  way that can be changed.
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

**Play** (or Plus) boots the game with what you chose; with nothing on,
the game starts as it is. While it starts, its progress prints on
screen. Your choices are saved for each game.

In every list, hold A and move the Wii Remote to drag it (a quick flick
keeps it going); the D-pad works too. A longer list has up and down
arrows on its right edge: point at one and press A to move a row, or hold
A to keep going. Each arrow goes away at its end of the list.

### Settings

The gear at the bottom right (or **2**) opens Settings: nine tiles, each
showing what it is set to now. *Picture* and *Games* are the defaults for
every game; then *Controllers*, *Home*, *Menu* (language, theme, font,
sounds, music), *Online*, *Drives* (finding games), *Help* (reports,
tutorial, the channel, reset, credits) and *Exit*. Each opens a short
list; the note under it explains the row you are on, and B goes back to
the tiles.

- **Language**: English, Español, 日本語, Português, Italiano, Français,
  한국어 (Korean) or *Wii* to follow the console. Korean needs **Menu font** set
  to *Wii Menu* on a Korean Wii, whose font has Hangul (RiftWii's own font
  has none); otherwise the menu stays in English and says so. A
  translation can be corrected by putting a copy of `wii/lang/<lang>.po` at
  `sd:/riftwii/lang/<lang>.po`.
- **Picture width**, **Deflicker**, **Black borders**, **Video mode**,
  **Aspect ratio**, **Game language**, **Game cIOS**, **Online server**:
  the defaults for every game.
- **Home tiles**: covers, 3D covers, carousel, shelf, channels or names.
- **Games from**: *All games*, *SD card*, *USB drive*, *GameCube* or
  *Wii channels*: which games Home lists, every place's or one's only.
- **Play history**: *Off* stops counting the games you start (Recently
  played, Home order and a game's page use the counts) and hides them.
- **Home order**: *A to Z* (a leading "The" is skipped), *Last played* or
  *Most played*. The last two put the games you played from RiftWii
  first, the rest after them A to Z.
- **Disc Channel**: the Disc drive's tile on Home, for playing from a
  disc. A new SD card asks whether you want it after the tour.
- **Widescreen menu**: on a 16:9 TV the menu is drawn narrower, so covers
  and pictures keep their shape instead of looking stretched. It starts
  at *4:3*; *16:9* turns it on, and *Automatic* follows the Wii's own TV
  setting (Wii Settings > Screen > Widescreen).
- **Screen size**: 100% down to 80%. If your TV cuts off the edges of the
  menu (overscan, common with HDMI adapters), lower it until the whole menu
  shows. Setting the TV's picture size to "Just scan", "Direct" or "Fit"
  does the same from the TV's side.
- **Theme**: steps through *Default* (RiftWii's own light look, no files
  needed) and every folder in `sd:/riftwii/themes/` that has a `theme.ini`,
  shown by the theme's `name` (or the folder name if it has none).
  Leaving Settings, RiftWii offers to restart its menu to show it (once,
  for the theme and the font together). The choice is
  saved as `theme = <folder>` in `sd:/riftwii/settings.txt`
  (`theme = default` for the default). The zip comes with three:
  *Midnight* (dark), *Bookshelf* (a light wooden bookcase: your covers
  stand on its shelves, with paper panels) and *Indigo* (indigo and violet
  glass on a fine dark grid, gold for what is lit). More in
  [THEMES.md](THEMES.md).
- **Clock**: how Home's clock writes the time: *12-hour* (with AM and
  PM), *24-hour*, or *Automatic*, as the menu's language writes it. Saved
  as `clock = auto`, `12` or `24`.
- **Menu font**: *RiftWii* (its own) or *Wii Menu*, the font the Wii Menu
  writes with, read from your Wii's own memory (nothing is downloaded, and
  it covers Japanese too). Leaving Settings, RiftWii offers to restart its menu to switch.
  If the Wii's font can't be read, RiftWii uses its own and says so on
  Home. Saved as `menu_font = riftwii` or `menu_font = wii`. The first
  read copies the font to `sd:/riftwii/fontcache/`, and later starts read
  that copy (quicker, and it needs no hardware access or cIOS). Delete
  the folder to read it from the Wii again.
- **Wii Menu button**: *Back to RiftWii* makes the Wii Menu button of a
  game's HOME Menu start RiftWii again (it needs the RiftWii channel);
  *Wii Menu* leaves it as it was.
- **Menu sounds** (Normal, Quiet, Off) and **Menu music**: the music is
  `music.ogg` (Ogg Vorbis, up to 6 MB) from `sd:/riftwii/`, or else the one
  the release puts in `sd:/apps/riftwii/`, looped while the menu is open.
- **Launch music** (Keep playing, Fade out; right after Menu
  music): *Keep playing* lets the menu's music play on under the launch
  screen's blue bar and log and fade out before the game takes over (it
  pauses while the game's IOS is loaded, and comes back after). *Fade out*
  fades it out when you start a game and keeps it off for the whole launch.
  `launch_music = on|off` in `settings.txt`.
- **In-game screenshots** (experimental, off at first): see Screenshots
  below. The picture is written straight from the screen, using almost
  none of the game's memory (games need it: keeping a copy of the
  picture took 0.8 MB, and Newer Super Mario Bros. Wii and HAL's Kirby
  games stopped on a black screen), so a picture taken during fast
  motion may shear slightly. Some games and mods may still not work with
  it: turn it off for those.
- **Download names and cheats**, and **Get the latest game names**.
- **GameCube adapter** (experimental), and **Check the GameCube adapter** (below).
- **GameCube rumble**: *Off* stops GameCube controllers rumbling in games,
  in the adapter and in the Wii's own ports.
- **Wii Remote rumble**: *Off* stops the Wii Remotes rumbling in every game
  (a game's own page has **Rumble** for that game alone). In a GameCube
  game a Wii Remote rumbles when the game rumbles the port it plays on, and
  stops when the game stops it, leaves or crashes.
- **Menu IOS**: IOS 58, or a d2x cIOS slot (248 to 252). Pick the slot that has
  fakemote to use USB DS3/DS4 pads as Wii Remotes.
- **Find network packs (RiiFS)** and **Copy network packs again** (below).
- **Look for games again**, **Check for a new version** (on GitHub; with downloads on, RiftWii also
  looks at every start and asks before it updates), and **Leave RiftWii**,
  which opens the HOME Menu (below).
- **Send a problem report**: see Reporting a problem below.
- **What's new** shows this version's main changes again (RiftWii shows
  them once after an update).
- **USB drive help**: what a USB drive needs, step by step, with a code
  for the guide. RiftWii also offers it when a USB drive can't be read.
- **Tutorial** shows the short tour of the basics again: a new SD card
  starts with it, once.
- **Test switches are on** shows up only when `sd:/riftwii/settings.txt`
  has a `debug_off =` line, which turns parts of RiftWii off for
  testing. Clear it unless the RiftWii developers asked you to keep it.
- **Credits and license**: RiftWii's license (the GNU GPL, version 3 or
  later, in full), where its source is, and who its parts come from.

### HOME Menu

HOME on Home (or **Leave RiftWii** in Settings) opens the HOME Menu, as
on the Wii: **Homebrew Channel**, **Wii Menu**, **Priiloader** (its menu;
the Wii Menu if Priiloader is not installed) or **Power off**. **Close**,
B or HOME goes back, so a HOME pressed by mistake costs nothing. Without
a pointer, the D-pad moves between the buttons, starting on Close. The
bar at the bottom shows each Wii Remote's batteries.

### WiiWare and Virtual Console games

The WiiWare and Virtual Console games installed on your Wii are on Home
with your other games, badged **WII**, each with its own icon and banner
in the Channels view. Those USB Loader GX keeps in an emulated NAND on
the SD card (its channel folder, `sd:/nands/01` unless you changed it)
are there too, badged **SD**. **Settings > Games from > Wii channels**
shows only these.

A channel game has a page like any game: **Play**, **Cheats**,
**Options** (the picture, language and the other game settings),
**Favourite** and **About**. RiftWii starts it itself, on the same
launch screen as a disc, so the cheats, the settings, screenshots, the
crash recorder, the GameCube adapter, the Wii U Pro Controller and the
Wii Menu button's way back to RiftWii work as they do for discs. This
needs a d2x cIOS, as SD and USB games do; when it cannot start a game
from the Wii itself, the Wii starts it as the Wii Menu would, without
those extras. A game in an emulated NAND keeps its saves in that folder.

To add games from `.wad` files, put them in a folder named `wad` (or `wads`) at the
top of the SD card and pick **Settings > Drives > Install WAD files**.
They go into that emulated NAND (the Wii itself is not changed) and show
up on Home. RiftWii decrypts them with your own Wii's key, read when you
install; it carries no key itself.

A game whose data was removed from the Wii to make room can't start:
RiftWii says so when you pick it.

### GameCube games

RiftWii plays GameCube games itself, in Wii mode. Each game goes in a
folder of its own in `sd:/games` on the SD card, or `usb:/games` on a USB
drive, named as you like, for example
`sd:/games/Super Mario Sunshine [GMSE01]/game.iso`. An image can be an
`.iso`, a `.gcm` or a Dolphin `.rvz` (checked as RVZ games are, below).
A GameCube image left loose in `sd:/games`, or put in `sd:/wbfs`, is not
listed as a GameCube game.

Games on the SD card need no cIOS. A game on the USB drive runs under a
d2x cIOS (249, 250 or 251, as USB Wii games do), which reads the drive
while it plays. Its memory cards and the rest stay on the SD card, so a
USB game still needs the SD card in. Its packs can be on either drive, as
a Wii game's can; a game on the SD card with a pack on the USB drive
runs under d2x too. On an NTFS drive a pack's tiny files (under about
700 bytes, which NTFS keeps inside its file table) can't be read during
play: such a pack says so at Start, and plays from the SD card or a
FAT32 drive. The drive needs 512-byte
sectors (FAT32 or NTFS, as for Wii games). A hard drive that sleeps when
idle is read once every 30 seconds while the game is quiet, so it does
not stop spinning and turn a later load into a disc error. GameCube games
on the USB drive are new and have not been tested on a Wii yet. `.ciso` images are not supported: turn them
back into an ISO or an RVZ with Dolphin's **Convert File...** first.

GameCube games are on Home with your other games, badged **GC**.
**Settings > Games from > GameCube** shows only these; the other
choices, and Home's view button (all games, games with mods, Recently
played, Favourites), can hide them as they hide any game.

A GameCube game's page has its own options in place of a Wii game's
picture and online settings: **Memory card** (*This game's own* or
*Shared*), **480p picture** (not for European games), **Sharper
picture**, **Picture width**, **Game language** and, for European
games, **Game speed** (60 Hz instead of 50).

**Memory cards** are files on the SD card, in `sd:/riftwii/gc/cards/`:
one for each game (`GMSE01.raw`), or with **Memory card** set to
*Shared* one for every game that shares it (`shared.raw`, and
`shared-jp.raw` for Japanese games). Switching copies no saves: move
them on the game's **Saves** page. With **Back up saves** on in
Settings (it is at first), the card is copied to
`sd:/riftwii/gc/backups/` before the game starts, when it has changed;
the newest five copies are kept, and **Restore an older save** on the
Saves page puts one back.

**Leaving a game:** hold for two seconds

- HOME on a Wii Remote with a Nunchuk, a Classic Controller or a Wii U
  Pro Controller;
- HOME and − together on a Wii Remote alone (its HOME is the game's Y);
- L, R, Z and Start together on a GameCube controller.

RiftWii waits for a save in progress before it goes back to the menu.
The console's **Reset** button restarts the game, as on a GameCube, in
the games where that is known to work (Super Mario Sunshine, USA, and
Super Mario Eclipse); in the others it goes back to RiftWii, as holding
HOME does.

**Controllers:** GameCube controllers in the Wii's own ports, and Wii
Remotes (sideways, with a Nunchuk or with a Classic Controller) and Wii
U Pro Controllers, which play as GameCube controllers: see [Wii Remote
controls in GameCube games](#wii-remote-controls-in-gamecube-games) for
the buttons and how to change them. A remote takes the port its light
shows, or the next free one when a GameCube controller is plugged into
that port. Controllers in the [GameCube controller adapter for
Wii U](#gamecube-controller-adapter-for-wii-u) play too, on the same
rule: each takes its adapter port's number, or the next free one, before
the Wii Remotes take what is left. On a Wii U, a Wii Mini and a Family
Edition Wii, which have no GameCube ports, the adapter is the only way
to play with GameCube controllers.

**Mods:** Riivolution packs for a GameCube game go in
`sd:/riivolution/` as for Wii games, and are picked on the game's page.
RiftWii serves their files to the game without changing the image.
Packs with memory, savegame or shift patches don't start yet, and say
so. A mod shipped as a patch (`.xdelta` or `.vcdiff`) goes in
`sd:/riftwii/patches/`: it is listed as the game it makes when its
original is in `sd:/games` or `usb:/games`, and the first start makes that game as
`sd:/games/<its ID>/game.iso` (the card needs room for it), an ordinary
game from then on.

### RVZ games

Dolphin's compressed `.rvz` images play straight from `games` on the SD
card or the USB drive, without turning them back into an ISO, and packs
work on them as on any game. RiftWii checks each RVZ when you pick it:

- Zstandard (any level) or no compression, with chunks of 32 to 128 KiB,
  plays. Most RVZs are made this way.
- Chunks of 256 or 512 KiB play after a warning (press Start twice).
- Larger chunks, bzip2, LZMA and LZMA2 and WIA files are refused, with
  the reason. So is a GameCube RVZ among the Wii games: it goes in a
  folder of its own ([GameCube games](#gamecube-games), below).

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

### RiiTag

[RiiTag](https://riitag.t0g3pii.de) shows the game you are playing on
your tag (USB Loader GX calls it Wiinnertag). Make an account there and
copy your key, then either:

- put `riitag_key = <your key>` in `sd:/riftwii/settings.txt`, or
- use a `Wiinnertag.xml` you already have for USB Loader GX: RiftWii reads
  `apps/usbloader_gx/Wiinnertag.xml` as it is, on the SD card or the USB
  drive, and also `riftwii/Wiinnertag.xml` on either (same format, one
  `<Tag URL="..." Key="..."/>` per server, `{ID6}` and `{KEY}` in the
  URL).

When you press Start, RiftWii tells each server the game's ID, while the
Wii is still online (not with Settings > Online off). `session.log` says
`RiiTag: ... updated`, or what the server answered. The key never goes in
the logs or problem reports.

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
- Press Start with a code mod on and RiftWii asks how to start it:
  - **With RiftWii's extras**: the crash recorder, Return to RiftWii
    from the HOME Menu, the game page's video and other settings, and
    the GameCube controller adapter for Wii U (in the console's second
    memory, as these builds use all of the game's main memory).
    Project+ runs this way.
  - **Without extras**: as the mod's own loader starts it: its codes,
    `gameconfig.txt` and the GameCube controller adapter when one is
    plugged in, nothing else (HOME > Wii Menu goes to the Wii Menu).
    Try this when a mod stays on a black screen.
  A launch without the menu (autorun, another loader) starts with the
  extras, or without them with `code_build_extras = off` in
  `sd:/riftwii/settings.txt`.
- Either way, a code build runs under IOS58 (the Homebrew Channel's) or
  the cIOS RiftWii's menu runs on, and the game is told it has its own
  IOS, as the launcher Project+ and its mods ship with (MinimaLauncher)
  does. Brawl's own IOS36 is what the disc asks for.
- PMEX Remix is very heavy for a real Wii: it can lag (on the character
  select screen, for one) and its own start screen recommends Dolphin.
  That is the build, not RiftWii.
- `boot.log` lists the codes, where they went and what `gameconfig.txt`
  changed, if something goes wrong.

#### Pictures for your mods

Point at a mod on the **Mods** page and its picture shows below the list.
To give a mod a picture, put a PNG next to it, named after it:

| The mod | Its picture |
| --- | --- |
| `riivolution/Newer.xml` | `riivolution/Newer.png` |
| `riftwii/pm.raw` (or `pm.raw.001`...) | `riftwii/pm.png` |
| A code build in `Project+` | `Project+/cover.png` |

Any size works (up to 1024 pixels a side and 2 MB); it is shrunk to fit.
Box art from GameTDB or anywhere else is fine. A mod without a picture
shows none.

#### Code builds on a virtual SD card (.raw images)

A build can also live inside a card image instead of on the SD card
itself: one file, like `sd.raw`, holding a whole FAT32 SD card with the
build's folder inside, like the `sd.raw` Dolphin and Project+'s netplay
builds use. The game then gets the image as its SD card. This helps when
the build can't read your real card (an SDXC card, a card formatted
with big clusters), or when you'd rather keep the build on the USB drive.

1. Put the image in the `riftwii` folder on the SD card or the USB drive.
   Any name ending in `.raw` works, so you can keep several builds side
   by side: `riftwii/pm.raw`, `riftwii/rex.raw`, `riftwii/sd.raw`. (When
   both drives have one with the same name, the SD card's is used.) An
   image from Dolphin works as it is: its build folder (`Project+`,
   `rex_`) goes at the top of the image, as on a real card.
2. Open the game's page, then **Mods**. Each build inside shows up with
   its image's name, like **Project+ (in pm.raw)**. Turn one on and press
   Start. The game gets one SD card, so turning on a build in one image
   turns off the builds in the others (and the ones on the SD card).

RiftWii can make the image for you, from a build already on the SD card,
with no PC: on the game's **Mods** page, pick **Make an SD image...**
under the build. It copies the build's folder, the other folders its
codes load files from (like `projectm` for a build in `sd:/codes`),
`gameconfig.txt` or `gc.txt` from the top of the card, and the game's
folder in `private/wii/app` (custom stages) into `riftwii/<build>.raw`,
then turns the build in the image on. An image over 4 GB is saved in
parts of 4000 MB (`.raw.001`, `.raw.002`...). A few things about it:

- The SD card needs room for a second copy of the build plus some free
  space for the game (5% of the build, between 128 and 512 MB). The
  popup says how much before it starts.
- It takes a while: about a minute for every 150-200 MB on a typical
  card. **Stop** gives up and deletes what was written.
- An SD card bigger than 32 GB (SDXC) needs the image: Brawl only reads
  cards up to 32 GB (SDHC), so a build straight on an SDXC card never
  loads, and RiftWii says so when you press Start. The image is a small
  card inside a file, which Brawl reads fine from the big one.
- Once the image works, you can delete the build's own folders from the
  SD card if you want the room back (keep `riftwii`).
- New and experimental: tested in Dolphin with Project M 3.6 (a 1 GB
  image that then started the game), not yet on a real Wii.

A few things to know:

- Keep the image on the same drive as the game. An image on the USB
  drive is read through d2x while you play, which only happens when the
  game is on the USB drive too; Start says so otherwise. With the game on
  the SD card or a disc, keep the image on the SD card.
- The image should be in one piece or a few: copy it to a freshly
  formatted drive if Start says it is in too many pieces.
- FAT32 drives hold files up to 4 GB. A bigger image (REX's is about
  12 GB) can be split: in 7-Zip, right-click the image, **7-Zip > Split
  file...**, and pick a size of `4000M`. That makes `rex.raw.001`,
  `rex.raw.002` and so on; put them all in `riftwii`, and RiftWii reads
  them as one image, listed as `rex.raw`. Keep the parts together on one
  drive and don't rename them.
- What the game writes (replays, custom stages, settings) goes into the
  image.
- Builds inside an image can't be turned on together with builds on the
  SD card or in another image, or with Riivolution packs, yet.
- Tested in Dolphin with Project+ 3.2 (3 GB image) and Project M 3.6
  (1 GB image) on the SD card. Players run Project M and REX this way
  on real Wiis, with the game and the image on the same drive.

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

**Check each cIOS** on that page restarts RiftWii and asks IOS 58 and
every installed d2x cIOS whether it sees the adapter where it is plugged
in (on a Wii U it asks whether that is a front or a back port). Home
shows the answer and `sd:/riftwii/usbcheck.txt` has the details, which
a problem report carries. Games from the SD card or a USB drive run on
a d2x cIOS, so this shows whether the adapter can work in them.

**GameCube adapter** in Settings is **Automatic** at first: the adapter
is used when it is plugged in as the game starts, and the game is left
alone when it is not. **On** always sets it up, so it can be plugged in
during the game; **Off** never does. `boot.log` says what was found.

It needs a menu IOS with USB HID: IOS 58 (the default) or a d2x cIOS;
the game then keeps that IOS. On a Wii U it needs IOS 58 or a d2x cIOS
on base 58 (usually 251), which RiftWii picks for games on the SD card or
a USB drive while the adapter is plugged in. On a Wii it stays off for
games read from the USB drive (and RVZ games or packs read from it),
where it broke the game's disc reads. **On** tries it there anyway (an
experiment: if the game then fails to read its disc, go back to
**Automatic**). RVZ games are the exception: the adapter stays off for
every RVZ game, even with **On**, until the conflict is solved
([issue #4](https://github.com/KakarottoCake/Riftwii/issues/4)). Games without GameCube controller support
ignore it, and so do mods that bring their own controller code (mkwcat's
NSMBW project) or read the controller hardware directly (Gecko codes
that add GameCube controls to NSMBW).

GameCube games follow the same setting. They keep the menu's IOS (IOS
58 unless you changed it), so on a Wii U the adapter works there with
**Automatic**, and their reads from the SD card do not go through d2x.
RVZ GameCube games get the adapter too. **GameCube rumble** in Settings
also reaches the adapter's controllers.

> **Warning for Wii U owners: On can freeze the console.** With **On**,
> a game from the SD card or a USB drive sometimes freezes at 97% while
> it starts: the d2x cIOS never answers RiftWii's request for the
> adapter, and the whole cIOS stops with it. The same launch can work
> one time and freeze the next, and RiftWii cannot tell beforehand. Hold
> the power button to turn the console off when it happens. RiftWii asks
> twice before it turns **On** on a Wii U. Leave it on **Automatic**
> unless you are testing the adapter, and send a problem report when it
> freezes.

### Wii Remote controls in GameCube games

In a GameCube game a Wii Remote plays as a GameCube controller. Without
files of your own the buttons are these:

| GameCube | Wii Remote alone (held sideways) | With a Nunchuk | Classic Controller |
| --- | --- | --- | --- |
| A, B | 2, 1 | A, B | a, b |
| X, Y | A, HOME | 1, 2 | x, y |
| Z | - | - | ZL or ZR |
| L, R | (none), B | C, Z | L, R |
| Start | + | + | + |
| Control stick | D-pad | Nunchuk stick | left stick |
| C-stick | (none) | D-pad | right stick |
| D-pad | D-pad | (none) | D-pad |

You can change them in text files on the SD card:

- `sd:/riftwii/gc/controls/default.txt` for every GameCube game;
- `sd:/riftwii/gc/controls/<game ID>.txt` for one game (its six-letter
  ID, `GLMP01.txt` for Luigi's Mansion), read after `default.txt`.

A file has a heading for each way of holding the controller, then one
line per button you want changed: the button, `=`, and what it presses.
Buttons you don't name keep what they had. `#` starts a comment.

```
# sd:/riftwii/gc/controls/GLMP01.txt
[remote]          # a Wii Remote alone, held sideways
B = L             # the trigger as L
- = R Z           # - presses R and Z together
shake = B         # shake the remote to press B

[nunchuk]         # a Wii Remote with a Nunchuk
C = Y
Up = Up           # the D-pad's up as the D-pad's up, not the C-stick
HOME = none

[classic]         # a Classic Controller (or Pro)
ZL = L
```

A Classic Controller laid out as in Super Mario 3D All-Stars, with ZR
opening the map in Super Mario Sunshine:

```
# sd:/riftwii/gc/controls/GMSE01.txt
[classic]
ZR = Y            # ZR opens the map (the GameCube's Y button)
ZL = Z            # ZL stays a plain Z press, as it is without a file
```

ZL and ZR are buttons that are either pressed or not, and both press Z
unless a file says otherwise. The Classic Controller's analogue triggers
are L and R: the game reads how far you press them. They go with their
buttons: map `R` to something else (`R = Y`) and the trigger no longer
half-presses the GameCube's R either, so a game such as Super Mario
Sunshine doesn't spray when you press it.

- **Headings:** `[remote]`, `[nunchuk]`, `[classic]`.
- **Buttons** (left of `=`): `A`, `B`, `1`, `2`, `-` (or `Minus`), `+`
  (or `Plus`), `HOME`, `Up`, `Down`, `Left`, `Right`, `Shake`; with a
  Nunchuk also `C` and `Z`; on a Classic Controller `A`, `B`, `X`, `Y`,
  `L`, `R`, `ZL`, `ZR`, `-`, `+`, `HOME` and the D-pad (no `1`, `2` or
  `Shake`). The D-pad's directions are as you hold the controller: with
  the remote sideways, `Up` is the arrow that points up then.
- **What they press** (right of `=`, several separated by spaces):
  `A`, `B`, `X`, `Y`, `Z`, `L`, `R`, `Start`, the D-pad's `Up`, `Down`,
  `Left`, `Right`, the control stick pushed all the way (`StickUp`,
  `StickDown`, `StickLeft`, `StickRight`) and the C-stick
  (`CStickUp`, `CStickDown`, `CStickLeft`, `CStickRight`). `none`
  presses nothing. `Up = Up StickUp` presses both.
- **Shake** presses its button for a moment each time the Wii Remote is
  shaken, again and again while you keep shaking. It is off unless a file
  names it, as the remote then sends its motion all the time.
- The Nunchuk's stick and the Classic Controller's sticks always stay
  the GameCube's sticks. Its analogue L and R stay the GameCube's while
  the `L` and `R` buttons do.
- A Wii U Pro Controller (synced to the Wii like a Wii Remote) plays with
  the `[classic]` controls: its buttons are a Classic Controller Pro's.
  Its L, R, ZL and ZR are plain buttons (digital), so they press the
  GameCube's L, R and Z all the way: a half-press, like Sunshine's light
  R spray while walking, isn't possible on a Pro Controller. HOME on its
  own leaves the game, as on a Classic Controller.
- Upper or lower case doesn't matter. A name RiftWii doesn't know, or a
  button the controller doesn't have, is skipped, and the log
  (`sd:/riftwii/boot.log`, lines starting `GameCube controls:`) says which
  line and why.
- Leaving the game doesn't change: hold HOME (HOME and - on a Wii Remote
  alone) for two seconds, whatever HOME presses in the game.

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
  its choice's, and over a macro's; params inside a choice's
  `<patch id>` win over all of them, for that patch only.
- A memory patch with both `value` and `valuefile` uses the file. A
  `search` may write more or fewer bytes than its `original`. An
  uncached address (`0xC…`, `0xD…`) patches the same RAM as its `0x8…`,
  `0x9…` twin; an offset above 32 bits is dropped with a warning.
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
the menu: the screen fades out, then the Wii goes to standby or fully
off as its own setting says (yellow light with WiiConnect24 or standby
on, red without), as the Wii Menu does. So does the HOME Menu's power off.

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

## Versions and updates

Versions are numbered the way Dolphin numbers its own: the year and
month of the release line (2610 is October 2026), then the number of
changes made since (2610-123), so a higher number is always newer.
(Versions before October 2026 were numbered like 2.7.0 and 3.3.9-beta.)

There is one kind of release: every new version is for everyone, and
RiftWii offers the newest one. Earlier versions had a Stable and a Beta
choice in Settings; a version that had it set to either still updates to
the newest release.

An update from inside RiftWii also brings the themes that come with it
(Bookshelf, Indigo, Midnight) and the channel installer
(`apps/riftwii_channel`) up to date, the first time the new version starts
online. Themes you made or downloaded and your `music.ogg` are left alone;
changes you made to Bookshelf, Indigo or Midnight themselves are replaced,
so copy such a theme to a folder of its own name first.

An update is installed only when its download matches the SHA-256
GitHub gives for it. While the new RiftWii is being put in place, the
power button waits until it is: the screen stays on for a moment, then
the Wii turns off.

**Experimental** features are in releases too, but they are not
yet confirmed on real Wiis and may not work on yours. The menu marks
them (a note before the launch, or "Experimental" in the Settings note):

- **The GameCube controller adapter** in games (it works in the menu on a
  Wii U). It stays off for RVZ Wii games
  ([issue #4](https://github.com/KakarottoCake/Riftwii/issues/4)).
  In GameCube games it has only been tried in Dolphin, which has no
  adapter.
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

**The USB drive isn't found.** Settings > USB drive help has the
checklist. Try a drive with its own power supply.
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
