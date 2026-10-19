RiftWii {{VERSION}}
================

RiftWii loads Riivolution-format mod packs into Wii games played from
the disc, a USB drive or the SD card, and can keep modded saves on the
SD card, apart from your Wii saves. It was written from scratch and
contains no Riivolution code. Thank you for trying it.

New in 2.0: online play through Wiimmfi, WiiLink WFC and other servers;
Dolphin's compressed .rvz images; cover art on Home; mod packs on the
USB drive; favourites and an A to Z jump; video mode, game language and
cIOS per game; the GameCube controller adapter for Wii U; a loading bar;
choices shared with Riivolution; and a check for new versions.

New in 2.1: a Stable and a Beta version of RiftWii (one kind of
release since 2610-432); mod packs on the USB drive work with d2x, with a disc too
(RiftWii starts your d2x cIOS for the launch); a USB LAN adapter can
stay plugged in beside the USB drive; RiftWii asks before it updates
itself; failure screens say where the logs are; the GameCube adapter
is set up as Nintendont does it; New Super Mario Bros. Wii no longer
stops after a few minutes when played from USB, SD or an RVZ; a
RiftWii channel for the Wii Menu, on a Wii or a Wii U; the menu fully
translated; and fixes throughout 2.0.1 to 2.0.10.

New in 3.2 (beta):
- The shelf: Settings > Home tiles > Shelf stands your games as boxes,
  spine out. The one you're on slides out and turns to show its front
  and back. The box scans come from GameTDB while Home is open.
- Channels: Settings > Home tiles > Channels plays each game's own
  animated icon, as the Wii Menu does. Pick a game and its banner plays
  before its page. Banners are read from your game images once and kept
  in sd:/riftwii/banners.
- Bookshelf, a new sample theme (a wooden bookcase: your covers stand on
  its shelves). Pick it in Settings > Theme.
- Settings > Widescreen menu: on a 16:9 TV covers and pictures keep
  their shape. Automatic follows the Wii's own TV setting.
- Settings > Screen size: if your TV cuts off the menu's edges, make the
  menu smaller (100% down to 80%).
- Home is quicker: covers, boxes and icons are found by game.
- 3.2.1: 3.2.0 went to a black screen on consoles; RiftWii is smaller
  again. The channel screen shows the whole banner, and Widescreen menu
  starts at 4:3.
- 3.2.2: with Widescreen menu on 16:9, the pointer reaches the TV's
  sides again (and the buttons there).
- 3.2.3: the channel screen's banner has no thin lines across it, and
  its Back and Continue buttons show at every screen size.
- 3.3.0: RiftWii makes a code build's SD card image for you (Mods page,
  Make an SD image...), so the game can be on the SD card too. Game
  banners look as the Wii Menu shows them (missing titles, 16:9), and
  the launch screen's text stays in its box on a 16:9 TV.
- 3.3.1: Start no longer hangs on a Project+ image made by RiftWii.
- 3.3.2: Settings and the game page in groups, and many small menu
  fixes. Menu font > Wii Menu works on a console. RVZ games on a USB
  drive wait for a drive that is slow to start. A code build on an SD
  card bigger than 32 GB says to make an SD image.
- 3.3.3: Menu font > Wii Menu no longer hangs some consoles at start.
  The channel can use the hardware like the Homebrew Channel (install
  it again from Settings). Project+ and other code builds' crashes
  are recorded for a problem report.
- 3.3.4: USB drives in the Wii's second port, and the right drive
  when two are plugged in. Menu font > Wii Menu uses the Wii Menu's
  own bitmap font. Hold B and press Left/Right to jump by letter.
  Wiinnertag.xml on the USB drive. Spine names read top to bottom.
  The power button follows the Wii's standby setting. 16:9 fixes.
- 3.3.5: Smooth transitions everywhere: games open out of their
  tile, pages slide, popups pop, boxes fly off the shelf. Banners
  are cut to the screen (flashing on consoles). Theme polish.
- 3.3.6: The Wii Menu's look: the bar that dips under a digital
  clock, the HOME Menu, glossy buttons. Banners play their sound
  and the arrows go from game to game. Bookshelf gets an iOS 6
  HOME Menu and shelves for the channels. An old RiftWii channel
  is offered for reinstalling.
- 3.3.7: A new SD card asks whether the Disc Channel should be on
  Home (Settings > Disc Channel). The banner screen's arrows switch
  games instead of starting one. A steadier pointer on 16:9. In-app
  updates bring the themes RiftWii comes with up to date too.
- 3.3.8: The pointer no longer shakes more towards the screen's
  sides: the Wii Remote's tilt is steadied.
- 3.3.9: The pointer reworked again (3.3.8 made the sides worse).
  "The" is not sorted on (The Legend of Zelda under L). The
  Wii Menu's font without hardware access: the menu runs on a d2x
  cIOS for the session to read it.
- 2610-126: New version numbers, as Dolphin's: the year and month
  of the last stable release (2610: 2.7.0), then the changes since.
  The banner screen's Start plays the game, its gear opens the
  game's page. D-pad page turns, the shelf's sides, the pointer at
  the screen's edges and the Wii Menu's font after a restart fixed.
- 2610-128: The banner screen's Settings is a pill beside Start.
  Settings says when a settings.txt test switch turns off the way
  back to RiftWii. More checks of RiftWii's memory while a game
  starts.
- 2610-138: A star on the banner screen makes a game a
  favourite. Settings: Home order (A to Z, Last played, Most
  played), USB drive help, What's new. The Wii Menu's font is
  copied to the SD card; without hardware access the cIOS is
  only used to make that copy, so USB drives work from the
  channel again. In-app updates bring the channel installer.
- 2610-140: GameCube adapter: Check each cIOS asks IOS 58
  and every installed d2x cIOS whether it sees the adapter where
  it is plugged in, for the Wii U's front ports in games.
- 2610-142: GameCube adapter on a Wii U: SD games start on a
  d2x cIOS on base 58, the only one that reaches the front USB
  ports, and the adapter stays on under it.
- 2610-146: GameCube adapter: third-party adapters that wait for
  the menu's set-protocol request get it, so their controllers
  show. On a Wii U whose cIOS bases can't be read, SD games
  with the adapter use slot 251.
- 2610-151: Testers' suggestions: + and - change games on a
  banner, B from its Settings comes back to it; Settings > Games
  from, Play history, GameCube rumble; one restart for theme and
  font; sharper Channels icons. GameCube adapter in games on a
  Wii U: RiftWii's own USB handle, opened before the menu's.
- 2610-161: Cheats with X values are set on the Wii (a hex
  keypad), and getting the latest cheats keeps your own. Settings >
  Wii Remote rumble; a progress bar for updates; smoother banner
  + and -; the channel's start screen fills a 16:9 TV (channel
  10); a way out of a stuck menu. Wii U GameCube adapter: next try.
- 2610-164: GameCube adapter in games on a Wii U: RiftWii asked
  the USB driver's version in an order that a Wii U's IOS took as
  another request, and then refused everything. It asks as the
  menu's own USB code does now.
- 2610-168: GameCube adapter on a Wii U: RiftWii's own USB
  requests keep their buffers in MEM2, as USB needs; full adapter
  diagnostics at each launch and in Check each cIOS.
- 2610-178: GameCube adapter on a Wii U in SD and RVZ games
  (confirmed), and a fix for disc games; Korean menu language;
  a quicker start without Internet; Cheats never stall; Settings
  > Aspect ratio as a default for every game.
- 2610-184: no crash with a Balance Board on as RiftWii
  starts; the shelf view scrolls quickly and keeps its box art on
  a widescreen menu; the crash screen saves its report whole.
- 2610-189: games from the SD card or a USB drive need d2x v11
  (beta3 is the latest; beta1 and beta2 get a reminder to update);
  saves on the SD card wait longer for a slow card.
- 2610-193: saves on the SD card for games from the SD card or
  a USB drive are kept by d2x itself, as in USB Loader GX; a USB
  game with mods or saves on the SD card gets a cIOS that reaches
  the card.
- 2610-204: French menu, Settings > Clock (12-hour or
  24-hour), Korean finished; games from the SD card on a d2x
  base 56 no longer fail to open the game (DI reply 128).
- 2610-208: games from the SD card on a Wii's d2x on base 56
  (slot 249) start again; the GameCube adapter check at launch
  broke them (open partition failed: DI reply 128).
- 2610-213: Resident Evil 4 from an RVZ plays (no more black
  screen); a Classic Controller's A no longer moves the pointer;
  Mario Super Sluggers keeps its own picture width and borders.
- 2610-251: search names the games as you type; mod packs show
  their own names; USB games with SD saves start on d2x 249 again;
  safer SD images; many crash, freeze and hardening fixes.
- 2610-255: a launch from another loader (WiiFlow, USB Loader GX)
  waits for the USB drive instead of giving up at once.
- 2610-290: you can now disable the clock. The shelf: reflections,
  a backdrop from the cover, momentum, boxes that turn round; the
  disc drive's own box; the shelf matches the Wii look.
- 2610-325: STABLE. Everything since 2.7.0 (the 2610 betas and
  RC1 to RC3) plus the RiftWii channel 11 (fewer black screens
  on the way back from a game), the GameCube adapter with SD games
  on a Wii U, safer saves on SD and USB, and full translations.
- 2610-329: STABLE hotfix. RiftWii channel 12: channel 11 never
  started on a Wii U (vWii). Home no longer shows a second clock.
- 2610-332: STABLE hotfix. RiftWii channel 13: channels 11 and 12
  never started on a Wii U (vWii). The older LEGO games from RVZ
  (The Complete Saga, Indiana Jones 1 and 2, Batman) may start.
- 2610-334: STABLE hotfix. Madden NFL 08 and other games whose
  main.dol only starts another executable (EA's 2007 games, Red
  Steel, Mortal Kombat) start the real one, as USB Loader GX does.
- 2610-335: STABLE hotfix. The start file is loaded after the
  apploader ran (2610-334 stopped Madden NFL 08 at 70%).
- 2610-336: STABLE hotfix. A game's video follows the disc's own
  region (a USA game with an X in its ID got PAL 50).
- 2610-337: STABLE hotfix. Madden NFL 08 starts: the part of its
  executable over RiftWii's own memory is written last.
- 2610-338: STABLE hotfix. The Smurfs: Dance Party is treated as a
  MetaFortress game; the game is shown its IOS's real revision.
- 2610-339: STABLE hotfix. MetaFortress games (The Smurfs: Dance
  Party) get no Back to RiftWii patch; the IOS field as GX sets it.
- 2610-340: STABLE hotfix. Home on a 16:9 menu shows its view and
  page names; Play history off and Wii Remote rumble off hold
  everywhere.
- 2610-343: games can use the SD card while your mods are on it.
  Shelf boxes shaped like Wii cases, a made-up back cover when
  none can be downloaded, + and - one box at a time; Settings >
  Reset all settings.
- 2610-346: STABLE. Games can use the SD card while your mods are
  on it; shelf boxes shaped like Wii cases; Settings > Reset all
  settings; LEGO Batman from an RVZ and a Mario Kart Wii pack fixed.
- 2610-350: STABLE hotfix. Mario Kart Wii with a hax pack starts
  again; HOME Menu sounds; the game page shows the memory RiftWii's
  extras take; Black borders: Remove all spares Kirby, Mario Party 8
  and Resident Evil 4.
- 2610-355: games start faster (no GameCube adapter wait when
  none is plugged in, fewer log writes); a mod's own banner names
  it on the Wii Message Board.
- 2610-360: themes can bring their own sound effects; Ghostbusters
  plays on past the hotel; SD cards over 2 GB are kept safe from
  older games (no SD card for them while packs are on it).
- 2610-368: the GameCube adapter in Project+; memory search patches
  change every match; NewerSMBW's replay recorder; Settings buttons
  translated; USB scan logs partitions.
- 2610-371: a Wii Remote log to learn why the Wii U Pro Controller
  does nothing in games (it does not make it work yet).
- 2610-372: Back to RiftWii works again for players with a Wii U
  Pro Controller paired.
- 2610-373: a test setting for the Wii U Pro Controller in games
  (wpad_probe = valid in settings.txt).
- 2610-391: a new look: big tiles for each game's page and for
  Settings, button prompts, 3D covers and a carousel on Home, each
  game's About from GameTDB, channels like the Wii Menu's, and
  everything kept inside a TV's edges.
- 2610-395: each game's About on one page (details, controllers,
  story), bigger covers and names on a widescreen TV, and the Wii U
  Pro Controller test now says in the log whether it changed the game.
- 2610-406: a first try at the Wii U Pro Controller in games (as a
  Classic Controller), sharp channel icons framed like the Wii Menu's,
  new controller and button pictures and smoother round buttons.
- 2610-407: RiftWii no longer stops when it starts after a game that
  wrote the controller log (Wii U Pro Controller owners).
- 2610-408: the Wii U Pro Controller is no longer sent the Wii Remote
  requests that made it hang up in games (a second try).
- 2610-412: more memory left to games (Newer Super Mario Bros. Wii
  from an RVZ crashed), and a note before an RVZ launch of a game
  that needs it all.
- 2610-418: code mods ask how to start (with or without RiftWii's
  extras) and run on IOS58 as their own launcher does; USB keyboard
  games (City Folk) start on an IOS with a keyboard driver.
- 2610-421: the game page's Mods tile is no longer cut off, and
  boot.log records the memory the game gets (for REX on a Wii U).
- 2610-432: one kind of release (no more Stable or Beta); popups
  look like the Wii Menu's; Channels' empty places match the icons;
  a Wii U's own USB drive is left alone; Retro Rewind starts with
  its separate save on.
- 2610-454: GameCube games in Wii mode (a trial: put a disc image
  in sd:/games/<its ID>/game.iso); WiiWare and Virtual Console
  on Home and WAD installs; more Home tiles for one game, each
  with its own mods; your own Home order; the Indigo theme;
  saves shared with USB Loader GX's emulated NAND.
- 2610-461: WiiWare WADs whose tickets carry a stray key
  number install; WADs in sd:/wads are found too; the code
  mod Start popup's text no longer runs under its choices.
- 2610-463: WiiWare games whose program RiftWii's loader
  could not find (an ELF, or a DOL it read too strictly)
  start through it, with cheats.
- 2610-464: WiiWare kept whole in an emulated NAND starts
  from there when the Wii's copy has its data removed;
  Bit.Trip Void, Error #002 and a restart's lost access fixed.
- 2610-489: GameCube games from a USB drive, the Wii U's
  GameCube adapter in GameCube games, GameCube mods built
  with libogc (Sonic Riders TE), WiiWare and VC with the
  launch screen, problem reports again, half the download.
- {{VERSION}}: Kirby Air Ride's sound and Melee's music; Home
  shows SD games before USB ones; REX with SD saves; "Title [ID]"
  mod folders; safer cheat and update downloads.
- 2610-280 (RC3): the GameCube adapter set to On no longer stops
  SD games from booting; no Wii Remote rumble for screenshots (it
  crashed Need for Speed: ProStreet). Cars 2 warning.
- 2610-275 (RC2): in-game screenshots written whole, with Wii Remote
  rumble; the GameCube adapter with USB games on a Wii U; Project+
  and PMEX return to RiftWii; clearer missing-mod-folder help; shelf
  case colours; banner sound and pointer fixes.
- 2610-259 (RC1): release candidate for the next stable release;
  WiiFlow plugins start games through RiftWii with a launch file;
  a launch from another loader frees a USB drive it left open.

New in 3.1 (beta):
- Pictures for your mods: point at a mod on the Mods page and its
  picture pops up. Put a PNG next to the mod, named after it
  (riivolution/Newer.png for Newer.xml, riftwii/pm.png for pm.raw,
  cover.png in a code build's folder). Without one, the game's cover
  shows, with the file name to add.
- Credits: every project RiftWii follows is named in the files that
  follow it, checked against the code (docs/ATTRIBUTION_AUDIT.md).

New in 3.0.1 (beta):
- Several virtual SD cards: every .raw file in the riftwii folder (pm.raw,
  rex.raw, sd.raw...) is one. Pick the build on the game's Mods page;
  turning one on turns the others off.
- Big images can be split for FAT32 drives: 7-Zip > Split file at 4000M
  makes rex.raw.001, rex.raw.002...; RiftWii reads them as rex.raw.
- Start explains when an image is on the USB drive but the game isn't
  (keep them on the same drive).

New in 3.0 (beta):
- Search: the small magnifier in Home's top right corner (or ZL/Z)
  finds games by name or game ID.
- Home shows which view is on, and the round buttons show their names
  when you point at them.
- Covers download in the background: pages turn and scroll smoothly
  while they come in.
- RiiTag: your tag shows the game you play. Put riitag_key = <your key>
  in sd:/riftwii/settings.txt, or keep USB Loader GX's Wiinnertag.xml.
- Settings > Menu font: write the menu in the Wii Menu's own font, read
  from your Wii (it covers Japanese too).
- Project M 3.6 in a virtual SD card (sd.raw) no longer freezes after
  its startup screen.

New in 2.8 (beta):
- Menu themes: Settings > Theme. A theme is a folder in
  sd:/riftwii/themes/ with its colours, pictures and music; Midnight, a
  dark theme, comes with this zip. To make your own, get the theme kit
  (linked from docs/THEMES.md on GitHub).
- On each game page: Aspect ratio (4:3 or 16:9), Rumble and Wii Remote
  speaker (turn them off), and a Region strings fix for imported games.
- A mod pack whose folder was copied into the riivolution folder by
  mistake now works anyway.
- Hidden files a Mac leaves on the SD card (names starting with ._) are
  skipped, and RiftWii tells you how to clean them up.
- Clearer messages when a code mod does not fit, and a pack's own codes
  file no longer shows up as a separate mod (Wii Party Combined Mods).
- Fewer I/O errors on SD cards with a damaged file system.

New in 2.7 (everything since 2.1.0):
- Code builds: Brawl mods made of Gecko codes (Project+, REX, PMEX
  Remix) run from RiftWii, also from a virtual SD card image at
  riftwii/sd.raw on the SD card or the USB drive (experimental).
- USB drives formatted as WBFS, drives with several partitions, drives
  over 1 TB, your own game folders (game_folders in settings.txt) and
  .iso games in wbfs folders.
- Riivolution packs on the USB drive, burned discs and USB DVD drives
  (all experimental).
- Just Dance 2014 to 2020 mods on a real Wii.
- A HOME Menu (Homebrew Channel, Wii Menu, Priiloader, power off),
  screenshots (in games too, experimental), problem reports with a link
  and a QR code, and a record of crashes inside games.
- Menu music and softer sounds, a first-run tutorial, the power button,
  dimming with the Wii's burn-in reduction, Minus and Plus to turn pages,
  a faster start, and an update check that no longer holds the menu.
- Games show on the Wii Message Board; other loaders can start games
  through RiftWii; a Region video fix on each game page.
- USB Loader GX's fixes for particular games, its automatic cIOS
  choice, and d2x keeps the game when it reloads IOS.
- Many games fixed: Mario Kart Wii, Wii Sports, Retro Rewind, Brawl
  from USB, Kirby's Return to Dream Land, Rhythm Heaven Fever, Rii
  Major, Metroid: Other M Redux, Resident Evil 4 and more.
- Black borders: Remove takes away the bars at the sides only; Remove
  all also the top and bottom, which breaks some games (experimental).
- CTGP Revolution 1.03 started from RiftWii is experimental: start CTGP
  from the Homebrew Channel.

You need: a Wii with the Homebrew Channel, an SD card, and for USB or
SD games a d2x cIOS installed (slots 248 to 252; by default the Menu
IOS slot is used, else 249, 250 and 251 in that order). The SD card must be FAT32; a
USB drive may be FAT32, NTFS or WBFS. Both need 512-byte sectors. RiftWii
never writes to the USB drive.

SETUP: TWO DRAGS
----------------
1. Copy everything inside the sd-card folder to the root of your
   SD card. This installs RiftWii itself (with its Homebrew Channel
   icon and description).
2. Copy everything inside the usb-drive folder to the root of your
   USB drive, if you use one. Then add your games (see below).

WHERE GAMES GO
--------------
- USB drive: Wii backup files (.wbfs, plus every .wbf1, .wbf2, ...
  piece when a game is split) in wbfs, either directly or one folder
  below it, for example: wbfs/Game [RMCE01]/RMCE01.wbfs. Raw disc
  copies (.iso) work there too (wbfs/Game [RMCE01]/RMCE01.iso, as USB
  Loader GX keeps them) or in games, and Dolphin's compressed .rvz
  files go in games.
  A name carrying the game ID, like that example, lets big drives list
  in seconds.
- SD card: the same layout works in sd:/wbfs and sd:/games.
- Games somewhere else: add the folders to sd:/riftwii/settings.txt,
  split by ";", for example: game_folders = /Wii Games; usb:/iso/wii
  (a plain /path is looked for on both drives).
- A drive with several partitions is read from the one holding wbfs,
  games or your game_folders.
- Or play from the physical disc: the Disc drive tile, first on the
  home screen.
- Images on NTFS must not be compressed (Windows file compression) or
  sparse.
- Plug in only one USB drive: d2x can use only one.

RVZ GAMES (EXPERIMENTAL)
------------------------
If an RVZ game does not start or play, use a WBFS or ISO copy.
.rvz files play straight from games, without turning them back into an
ISO, and mods work on them too. RiftWii checks each one when you pick
it: Zstandard or no compression with chunks of 32 to 128 KiB plays
(most RVZs are made this way); 256 or 512 KiB chunks play after a
warning (press Start twice); anything heavier is refused with the
reason. The SD card must be in the Wii even for an RVZ on the USB
drive: RiftWii keeps a small index of each game in sd:/riftwii/rvz/.

WHERE MODS GO
-------------
- Mod packs go in sd:/riivolution, exactly as for Riivolution.
- Packs can also go in usb:/riivolution on a FAT32 or NTFS USB drive
  (experimental); they show with "@ USB". If one fails, copy it to the
  SD card.
- Code builds (Project+, REX) go on the SD card only: the build's folder
  at the top of the SD card, and the game on a USB drive or the disc,
  not the SD card.
- A pack's folders go where its XML says, usually at the top of the SD
  card next to the riivolution folder (sd:/nmg for Neo Mario Galaxy).
  RiftWii won't start a pack none of whose files are there, and says
  where to copy them.
- Copy each pack's folders completely: a missing folder or file is
  named in sd:/riftwii/boot.log, with what the card holds instead.

USING IT
--------
1. Start RiftWii from the Homebrew Channel. It reads the SD card and
   the USB drive and shows the games that have mods as tiles, with
   their covers.
2. The round button at the bottom left (or 1) steps through games with
   mods, all games, Recently played and Favourites. B jumps to the next
   letter, A to Z; Minus and Plus turn the pages. The view you pick is remembered, and Home opens
   on the last game you played.
3. Pick a game. Its page shows how often you played it.
   - Mods opens the packs made for this game. Each pack has an On/Off
     switch; once it is on, its settings show under it. The arrow
     buttons either side of a setting step it (- steps back too).
   - Saves keeps the Wii saving as usual, or keeps this game's saves on
     the SD card (copied once from the Wii's save, or started fresh).
   - Cheats opens the game's cheat codes. When the Wii is online they
     are downloaded the first time. Tick the ones you want. They live
     in sd:/riftwii/cheats/<ID>.txt, which you can edit on a computer.
   - Picture width, Deflicker and Black borders change how the game
     draws its picture. 720 fills the TV from side to side; Deflicker
     Off gives the sharpest picture. Black borders: Remove takes away
     the bars at the sides; Remove all also the top and bottom
     (experimental: some games break with it).
   - Video mode picks the TV signal (NTSC, PAL 60, PAL 50, 480p).
   - Game language tells the game the console is set to another
     language; pick one the game has.
   - cIOS (SD and USB games) picks the d2x slot, 248 to 252.
   - Online server lets the game play online again (below).
   Default follows Settings, where the same rows apply to every game.
4. In every list, hold A and move the Wii Remote to drag the list up
   or down. The D-pad works too.
5. Start (+) boots. A bar and the log show the progress while the game
   starts. Your choices are remembered per game.
6. Settings (the gear, bottom right, or 2). The note under the list
   says what the row you are on does. It holds the language (English,
   Espanol, Japanese, Portugues, Italiano, or follow the Wii), the
   defaults for every game, Home tiles (covers or names), whether game
   names, covers and cheats are downloaded, the GameCube adapter, Menu
   IOS (IOS 58 or a d2x slot; pick the one with fakemote so a USB DS3
   or DS4 works as a Wii Remote), Menu sounds and Menu music, the Wii
   Menu button (Back to RiftWii or Wii Menu), network packs, Check for
   a new version, Rescan and Exit.

Games started from RiftWii show on the Wii Message Board with the time
played (message_board = off in settings.txt turns this off). Menu music
plays sd:/riftwii/music.ogg if you put one there, else the tune in
sd:/apps/riftwii ("Insect Factory" by Zane Little Music, CC0).

Your choices for a game are also written to Riivolution's own
sd:/riivolution/config files, so both loaders turn on the same mods.

ONLINE PLAY
-----------
Nintendo's Wi-Fi Connection closed in 2014. "Online server" points a
game at a replacement: Wiimmfi (Mario Kart Wii gets Wiimmfi's own
patch), WiiLink WFC (online communications credit to WiiLink WFC),
AltWFC, or your own server (wfc_domain = <domain> in
sd:/riftwii/settings.txt). Nothing changes while mod packs are on:
Mario Kart Wii distributions bring their own online setup.

COVERS
------
When the Wii is online, Home fetches each game's cover from GameTDB
(about a second each, the page on screen first) and keeps it in
sd:/riftwii/covers. A game without a cover shows its name. Home tiles in
Settings switches to name tiles.

GAMECUBE CONTROLLER ADAPTER FOR WII U (EXPERIMENTAL)
----------------------------------------------------
It works in the RiftWii menu; in games it is not yet confirmed on real
consoles. It stays off for RVZ games for now (GitHub issue #4).
The Nintendo adapter (WUP-028) or a copy that works like it (a Mayflash
with its switch on Wii U):
1. Plug the adapter's black USB plug into the Wii before you start the
   game. The grey plug only adds power for rumble.
2. "Check the GameCube adapter" in Settings shows what each port
   presses, so you can see it works before starting a game.
"GameCube adapter" in Settings is Automatic at first: the adapter is
used when it is plugged in as the game starts, and the game is left
alone when it is not. On always sets it up, so it can be plugged in
during the game; Off never does.
In games that support the GameCube controller (Mario Kart Wii, Super
Smash Bros. Brawl and others) the adapter's controllers fill the ports
that have no controller plugged in, rumble included. It needs the menu
IOS to be IOS 58 (the default) or a d2x cIOS. Games without GameCube
controller support ignore it, and so do mods that bring their own
controller code (such as mkwcat's NSMBW project) or Gecko codes that
read the controller hardware directly.
On a Wii U, games from a USB drive or the SD card run under d2x, which
reaches only the rear USB ports: plug the adapter into a rear port.

NETWORK PACKS (RiiFS)
---------------------
Packs can also come from a PC running a RiiFS server, as with
Riivolution. Put an XML in sd:/riivolution with
<network protocol="riifs" address="192.168.1.20" port="1137"/>, or turn
on "Find network packs" in Settings. Those packs show with "@ address"
after their name. Before the game starts, RiftWii copies what it needs
to sd:/riftwii/riifs/ (only changed files; "Copy network packs again"
copies everything), then plays from the SD card.

GAME NAMES
----------
When the Wii is online, RiftWii downloads GameTDB's list of game names
in the menu's language (at most once a week), so games show as "Super
Mario Galaxy 2" rather than the disc's "SUPER MARIO GALAXY MORE".
Offline, a folder named "Title [ID]" is used, then the disc's own name.

CONTROLS
--------
Wii Remote (pointer or D-pad), Classic Controller or GameCube pad.
Button names match on all of them. The GameCube control stick and the
Classic Controller's left stick move a pointer like a Wii Remote's;
the D-pad moves the highlight. With no pointer on screen, only the
highlighted tile or row answers to A. HOME opens the HOME Menu: leave to
the Homebrew Channel, the Wii Menu or Priiloader, or turn the Wii off.
Close (or B, or HOME again) goes back.

RIFTWII CHANNEL ON THE WII MENU
-------------------------------
The zip also holds the RiftWii Channel Installer (sd-card/apps/
riftwii_channel). It puts a RiftWii channel on the Wii Menu, so RiftWii
starts without the Homebrew Channel. The channel holds no copy of
RiftWii: it starts apps/riftwii/boot.dol from the SD card (or the USB
drive), so RiftWii's own updates keep working and the channel never
needs installing again.
- Open it from RiftWii (Settings > RiftWii channel on the Wii Menu; the
  first time, RiftWii also asks) or from the Homebrew Channel.
- It needs a d2x cIOS in slot 249, 250 or 251. A to install or update,
  - (Y on a GameCube controller) to remove, HOME to go back.
- It works on a Wii and on a Wii U (vWii): the installer puts the
  right version on for the console it runs on.
- It writes to the Wii's system memory, so as with any channel, have
  BootMii or Priiloader in case something goes wrong. It logs to
  sd:/riftwii/channel.log.
- Updating RiftWii from inside RiftWii replaces only RiftWii; copy
  apps/riftwii_channel from this zip to get the installer.
- With the channel installed, the Wii Menu button in a game's HOME Menu
  starts RiftWii again (Settings > Wii Menu button switches it back).

UPDATES
-------
There is one kind of RiftWii release, and every RiftWii updates to
the newest one (Settings > Check for a new version). Burned discs
are at your own risk: they wear the Wii's drive more than
Nintendo's discs.
Experimental features (packs on the USB drive, RVZ games, the
GameCube adapter in games, in-game screenshots, code builds, the
virtual SD card) are not yet confirmed on every Wii and may not
work on yours.

TROUBLESHOOTING
---------------
- My games don't show up: Home first shows only games with mod packs.
  Press 1 for all games. The line at the bottom of Home says what went
  wrong with a drive.
- "No d2x cIOS in 249-251": SD and USB games need a d2x cIOS. The disc
  drive works without one.
- The USB drive isn't found: try a drive with its own power supply. If
  Menu IOS is set to a cIOS, it must be based on IOS 58 for USB drives
  in the menu; otherwise set Menu IOS back to IOS 58.
- A pack says "Broken": its XML has an error, shown under the pack.
  Fix the file on a computer.
- A mod doesn't start: RiftWii never starts a game half patched. The
  screen names the pack, option and file that stopped it. Press A to go
  back to RiftWii.
- RiftWii crashed: it shows what happened, saves it to
  sd:/riftwii/crash.txt and starts again. Please send that file.
- The GameCube adapter does nothing: open "Check the GameCube adapter"
  in Settings. The menu IOS must be IOS 58 or a d2x cIOS, and the game
  must support the GameCube controller. The "GameCube adapter" lines in
  sd:/riftwii/boot.log list the USB devices the Wii saw at launch.

REPORTING PROBLEMS
------------------
Please report: Wii model, system menu version, which d2x slot was
used, game ID, what you did, what the screen showed, and
sd:/riftwii/boot.log (or session.log if it hung before the game
started). If a game stopped with its own disc error ("An error has
occurred"), include the last step shown in those logs.

KNOWN LIMITS
------------
- USB and SD game boot need d2x. Plain IOS58 boot is not supported.
- Games stored past the first 2 TB of a drive cannot be read by d2x
  and are skipped.
- Code builds read their files from the SD card or from a virtual SD
  card image (riftwii/sd.raw).
- RVZ games are tested in Dolphin, which has no d2x: RVZ games on the
  USB drive still need checking on a real Wii.

LICENSE, SOURCE AND CREDITS
---------------------------
RiftWii is free software under the GNU General Public License, version 3
or later, and comes with NO WARRANTY. LICENSE.txt is the license,
SOURCE.txt says where this release's source code is, and NOTICE.md lists
every third-party component, where it came from and its license.

RiftWii contains or follows work from USB Loader GX (online server,
Wiimmfi, return-to-channel, language and video patches, and the Gecko
code handler), Gecko OS, Brainslug, wup-028-bslug, Nintendont, WiiDRC,
libwiigui, FreeTypeGX, oggplayer, WiiLink WFC, d2x cIOS, devkitPro and
libogc, pugixml, Zstandard, BearSSL, FreeType, zlib, Tremor and libogg, with Dolphin, wiibrew and the Riivolution patch format wiki as
documentation. Thank you to all of their authors. In RiftWii itself:
Settings > Credits and license.

DISCLAIMER
----------
I don't condone piracy. RiftWii doesn't come with any games and won't
download them for you. Play your own games: discs you own, or backups
you made from them yourself. RiftWii exists for preservation, and to
move Wii development forward: out of the stone age of needing an aging
disc in an aging drive every time you want to play.
