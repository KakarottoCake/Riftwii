RiftWii {{VERSION}}
================

RiftWii loads Riivolution-format mod packs into Wii games played from
the disc, a USB drive or the SD card, and can keep modded saves on the
SD card, apart from your Wii saves. It was written from scratch and
contains no Riivolution code. Thank you for trying it.

START HERE
----------
1. Copy everything inside this zip's sd-card folder to the top of your
   SD card (merge folders if asked).
2. Put your games in wbfs or games on the SD card or a USB drive:
   - Wii games (.wbfs, .iso): in wbfs, for example
     wbfs/Mario Kart Wii [RMCE01]/RMCE01.wbfs
   - Wii .iso and .rvz: in games, for example games/Mario Kart Wii.rvz
   - GameCube games: a folder of their own in games, for example
     games/Luigi's Mansion/game.iso
   Playing from the disc? Skip this step.
3. Copy each mod pack to the top of the SD card exactly as it came:
   its XML file ends up in sd:/riivolution, its folders usually next to
   it (for example sd:/nmg).
4. Start RiftWii from the Homebrew Channel. Press 1 on Home to see all
   your games, pick one, open Mods to switch a pack on, then Play.
5. Optional: start "RiftWii Channel Installer" from the Homebrew
   Channel and press A, to put RiftWii on the Wii Menu and come back to
   it when you quit a game.

The step-by-step guide, with a drawing of every folder:
https://github.com/KakarottoCake/Riftwii/blob/main/docs/GUIDE.md
What changed in each version: the release notes on
https://github.com/KakarottoCake/Riftwii/releases

You need: a Wii or a Wii U in Wii mode with the Homebrew Channel and
an SD card. Wii game images and GameCube games or packs on USB need a
d2x cIOS, v11 or newer (v11 beta3 or newer recommended), normally in
slot 249, 250 or 251. GameCube games and packs on SD need no cIOS.
The SD card must be FAT32; a
USB drive may be FAT32, NTFS or WBFS. Both need 512-byte sectors. RiftWii
reads game images and packs without changing them. A virtual SD image
on USB is writable: the game's replays, settings and other writes go
into that image.

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

GAMECUBE GAMES
--------------
Put each game in its own folder in sd:/games or usb:/games, for example
games/Luigi's Mansion/game.iso. ISO, GCM and RVZ work. For two-disc
games keep both images in the same folder; RiftWii changes discs when
the game asks. Saves use SD memory card images in riftwii/gc/cards.
The game's Saves page can copy saves from Dolphin or a real card in
the Wii. Settings > Games > Bring your Nintendont saves imports those
cards too. The real-card choice uses slot A on an
original Wii; Wii U has no memory card slots.
To leave, hold for two seconds: HOME with a Nunchuk, Classic Controller
or Wii U Pro Controller; HOME and - on a Wii Remote alone; or L, R, Z
and Start on a GameCube controller.

RVZ GAMES (EXPERIMENTAL)
------------------------
If an RVZ game does not start or play, use a WBFS or ISO copy.
.rvz files play straight from games, without turning them back into an
ISO, and mods work on them too. RiftWii checks each one when you pick
it: Zstandard or no compression with chunks of 32 to 128 KiB plays
(most RVZs are made this way); 256 or 512 KiB chunks play after a
warning (choose Play again); anything heavier is refused with the
reason. The SD card must be in the Wii even for an RVZ on the USB
drive. For Wii RVZs, RiftWii keeps a small launch file in
sd:/riftwii/rvz/.

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
   mods, all games, Homebrew, Recently played and Favourites. Recently
   played appears after playing a game, Favourites after starring one.
   B jumps to the next
   letter, A to Z; Minus and Plus turn the pages. The view you pick is remembered, and Home opens
   on the last game you played.
3. Pick a game. Its page shows how often you played it.
   - Mods opens the packs made for this game. Each pack has an On/Off
     switch; once it is on, its settings show under it. The arrow
     buttons either side of a setting step it (- steps back too).
   - Saves keeps the Wii saving as usual, or keeps this game's saves on
     the SD card (copied once from the Wii's save, or started fresh).
     SD, emulated NAND uses the save folder from USB Loader GX's
     settings, or /nands/01. The note shows the folder.
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
5. Play (+) boots. A bar and the log show the progress while the game
   starts. Your choices are remembered per game.
6. Settings (the gear, bottom right, or 2). The note under the list
   says what the row you are on does. It holds the language (English,
   Español, Japanese, Português, Italiano, Français, Korean, or follow
   the Wii), the defaults for every game, Home tiles (covers, 3D covers,
   carousel, shelf, channels or names), whether game
   names, covers and cheats are downloaded, the GameCube adapter, Menu
   IOS (IOS 58 or a d2x slot; pick the one with fakemote so a USB DS3
   or DS4 works as a Wii Remote), Menu sounds and Menu music, the Wii
   Menu button (Back to RiftWii or Wii Menu), network packs, Check for
   a new version, Look for games again and Leave RiftWii.
   Korean needs apps/riftwii/korean.ttf beside boot.dol, or the Wii
   Menu font on a Korean Wii.

Homebrew lists apps in sd:/apps; pick one and confirm Start. To save a
mod setup as its own tile, use Mods > Make a Home tile with these mods
on a game from SD or USB. Each tile keeps its own choices.
Hold B (L on GameCube) and press A on a game to move it in a tile view:
drag and release A with the pointer, or use the D-pad and press A to
put it down. Settings > Home > Home order > Your order keeps it.

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
Settings switches to 3D covers, carousel, shelf, channels or name tiles.
Settings > Menu > Get more themes previews and downloads community
themes to sd:/riftwii/themes. Downloads must be on.

GAMECUBE CONTROLLER ADAPTER FOR WII U (EXPERIMENTAL)
----------------------------------------------------
It works in the RiftWii menu and can work in games, including RVZ.
Support depends on the console, the game's USB use and the cIOS.
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
On a Wii U it needs IOS 58 or d2x based on IOS 58 for every USB port.
RiftWii prefers that d2x when it sees the adapter. Automatic leaves it
off if d2x reads USB on another base, or reads SD on base 56 or older.
On tries anyway, but can freeze a Wii U during launch: keep Automatic
unless testing. Hold the power button if it freezes. Check each cIOS
on the adapter's test page checks the installed slots and writes the
results to sd:/riftwii/usbcheck.txt.

NETWORK PACKS (RiiFS)
---------------------
Packs can also come from a PC running a RiiFS server, as with
Riivolution. Put an XML in sd:/riivolution with
<network protocol="riifs" address="192.168.1.20" port="1137"/>, or turn
on "Find network packs" in Settings. Those packs show with "@ address"
after their name. Before the game starts, RiftWii copies what it needs
to sd:/riftwii/riifs/ (files missing or different in size; "Copy network packs again"
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
A Wii U Pro Controller works as a Classic Controller. The Wii U
GamePad works in the menu.
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
usually needs no reinstall when RiftWii updates. If an older channel
needs updating, RiftWii asks you to run the installer again.
- Open it from RiftWii (Settings > RiftWii channel on the Wii Menu; the
  first time, RiftWii also asks) or from the Homebrew Channel.
- It needs a d2x cIOS in slot 249, 250 or 251. A to install or update,
  - (Y on a GameCube controller) to remove, HOME to go back.
- It works on a Wii and on a Wii U (vWii): the installer puts the
  right version on for the console it runs on.
- It writes to the Wii's system memory, so as with any channel, have
  BootMii or Priiloader in case something goes wrong. It logs to
  sd:/riftwii/channel.log.
- An in-app update brings the installer and built-in themes up to date
  when the new RiftWii first starts online. Themes you added and your
  music.ogg are kept; edits to built-in themes can be replaced.
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
  sd:/riftwii/crash.txt and starts again. It then offers to send a
  problem report.
- The GameCube adapter does nothing: open "Check the GameCube adapter"
  in Settings. The menu IOS must be IOS 58 or a d2x cIOS, and the game
  must support the GameCube controller. The "GameCube adapter" lines in
  sd:/riftwii/boot.log list the USB devices the Wii saw at launch.

REPORTING PROBLEMS
------------------
Start RiftWii again after a failed launch or crash and choose Send
when it offers a report. For a freeze or another problem, open
Settings > Help > Send a problem report. It uploads the logs and
settings to paste.rs (or dpaste.com if needed) and shows a link and QR
code. Send the link with what you did and what the screen showed:
https://discord.gg/gGsTdjjQaK
Anyone with the link can read the report. Nothing is sent unless you
choose Send. A copy is saved as sd:/riftwii/report.txt; send that file
if the upload fails.

KNOWN LIMITS
------------
- Wii game images on USB or SD need d2x. GameCube games and packs on
  SD work without it.
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
