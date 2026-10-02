# Testing RiftWii

This is the full test plan for the next stable version. It covers
everything that changed since the last stable one, 2.1.0. Please don't
try to do all of it in one go: pick the sections that match what you
have (a disc, a USB drive, mods, a Wii U, a GameCube adapter) and go
through those.

Each step says what should happen. When something else happens, that's
a result worth sending.

## Before you start

1. Put the release zip's `sd-card` folder on your SD card (and
   `usb-drive` on your USB drive if you use one), replacing the old
   files. Or, with Settings > Updates on Beta, let RiftWii update itself.
2. Have a d2x cIOS in 249, 250 or 251 for games on the SD card or a USB
   drive. Discs work without one.
3. Note your console: Wii or Wii U, region, and your cIOS slots. A
   problem report lists them for you.

## How to send results

Problem reports need no computer and you don't need to take out your
SD card:

1. In RiftWii, go to **Settings > Send a problem report**.
2. Scan the QR code with your phone, or copy the link it shows
   (paste.rs, or dpaste.com when paste.rs can't be reached).
3. Post the link with a line on what you did and what happened.

After a failed launch or a crash, RiftWii offers the report by itself
at the next start: press A. If the upload fails, the report is saved as
`sd:/riftwii/report.txt`.

For things that worked, a short "works" with the game and where it ran
from (disc, SD, USB) is just as useful.

## 1. Start-up and the menu

- [ ] **First start on a fresh card.** Delete `sd:/riftwii/settings.txt`
  and `sd:/riftwii/tutorial_done.txt`. The tutorial shows once.
  Settings > Tutorial shows it again.
- [ ] **No SD card.** Start RiftWii without one: it says so instead of
  hanging.
- [ ] **Home pages.** Minus and Plus turn the pages, with a slide. B
  jumps A to Z (L on a GameCube controller). The arrows and the game
  count are visible, also on a Wii U's TV.
- [ ] **Home tiles.** Settings > Home tiles: Covers and Names both work.
  A game page's Cover row downloads its box art.
- [ ] **Favourites.** Favourite a game on its page. Press 1 on Home
  until the Favourites view shows it.
- [ ] **Menu music and sounds.** Settings > Menu music on and off, Menu
  sounds Normal, Quiet and Off.
- [ ] **Dimming.** Leave the menu alone. It dims when the console's
  screen saver is on, and wakes up on any button.
- [ ] **Menu screenshots.** Hold 1 and press HOME on a Wii Remote, or
  hold L and R and press Down on a GameCube controller. The screen
  flashes and a PNG appears in `sd:/riftwii/screenshots`.
- [ ] **HOME Menu.** Press HOME and try each choice: Homebrew Channel,
  Wii Menu, Priiloader (its menu if you have it, else the Wii Menu) and
  Power off. Homebrew Channel should open the Homebrew Channel also when
  RiftWii was started from its channel on the Wii Menu (fixed in RC8).
- [ ] **Classic Controller in the menu.** The stick moves the pointer;
  the D-pad hides it and moves the highlight; pushing the stick brings
  the pointer back (RC8).
- [ ] **Power button.** The console's button and a Wii Remote's both
  fade out and turn the Wii fully off (red light).
- [ ] **Look for games again** (Settings) finds a game you just copied.
- [ ] **Extra game folders.** Put `game_folders = /Wii Games` in
  `sd:/riftwii/settings.txt` and a game in that folder: it shows up.
- [ ] **Credits and licence** (Settings) opens and scrolls.

## 2. Updates

- [ ] **Settings > Updates** switches between Stable and Beta.
- [ ] **Check for a new version.** On Beta it should say you have the
  newest. On Stable it should not offer an older version.

## 3. Discs

- [ ] **A retail disc with nothing on.** It boots like it does from the
  Wii Menu.
- [ ] **A disc with a pack on the SD card** (Newer Super Mario Bros.
  Wii, Mario Kart Wii mods, Super Mario Galaxy 2 packs, and so on).
- [ ] **A disc with a pack on the USB drive** (Retro Rewind, for
  instance), also as the first launch after turning the console on.
- [ ] **Burned discs** (early Wiis whose drive reads DVD-R only). It
  warns before the launch, then boots.
- [ ] **A USB DVD drive** on port 0 with a burned Wii disc.

## 4. Games on the SD card or a USB drive

- [ ] **WBFS and ISO, on FAT32 and NTFS drives.** Both in `wbfs/` (an
  `.iso` in `wbfs/` works too, as in USB Loader GX's layout) and in
  `games/`.
- [ ] **Game cIOS on Automatic.** boot.log should list your d2x slots
  and their base IOS ("cIOS:" lines) and say which one it picked. Also
  try a fixed slot (249, 250, 251) for comparison.
- [ ] **Games that reload IOS** keep running. boot.log should say "d2x
  keeps IOS... across the program's own IOS reloads" for every image
  launch. This is new in 2.7.0 RC7: tell us if any game that worked on
  RC6 doesn't now.
- [ ] **Leaving a game.** Back to RiftWii and to the Wii Menu after a
  USB game, then start another game.
- [ ] **RVZ images** (experimental): one game, nothing on.
- [ ] **A USB 3 hard drive on a Wii U.** It shouldn't stop at 3%.
- [ ] **A USB drive bigger than 1 TB.** No 20 second wait with "did not
  start through d2x" lines in boot.log before a USB game starts (RC8).

## 5. Mods

- [ ] **Packs on the SD card**: Newer, a Galaxy pack, a Mario Kart Wii
  pack (Retro Rewind, MKWii Deluxe), with their options.
- [ ] **Packs on the USB drive** with a game on USB, then with a disc.
- [ ] **Saves.** On a game page, Saves: NAND, Separate and Fresh start.
  The game should see the save you picked.
- [ ] **Just Dance 2014 to 2020 mods** (Just Dance Mega, for instance).
- [ ] **Very big packs** (Metroid: Other M Redux, with its large
  movies). Other M Redux used to stop on a black screen right after the
  launch; since RC8 it should reach the title screen, the opening movie
  and the first room.
- [ ] **A missing pack folder.** RiftWii should refuse with a message,
  not boot half a mod.
- [ ] **Memory-heavy packs** (RiiMajor and other big ones). If one
  fails, the report says whether memory ran out.

## 6. Code builds and cheats

- [ ] **Project+, PMEX Remix and REX** from the SD card, with their
  `gc.txt`. A cheat file in `usb:/codes` doesn't get in the way.
- [ ] **The virtual SD card** (a build's `sd.raw` on the SD card or a
  USB drive): the build's own SD files load in game.
- [ ] **Cheats.** On a game page, Cheats downloads the list, pick a
  few, play. Codes with values to fill in say "Edit first".
- [ ] **Cheats in Metroid Prime Trilogy** (new in RC7): turn one on,
  pick one of the three games in the trilogy's menu, and check the
  cheat still works inside that game.

## 7. Picture and language

- [ ] **Picture width, Deflicker, Black borders** on a few games.
  Known issue: the Kirby games get a pink bar with width 720 and
  borders removed (issue 13).
- [ ] **Video mode**: NTSC, PAL 60 Hz, PAL 50 Hz, 480p (needs a
  component cable) and The console's.
- [ ] **480p fix.** Wii Music and Mario Kart Wii in 480p.
- [ ] **Region video fix** (new in RC7, on a game page): only for a US
  or Japanese game that shows no picture on a console from another
  region. Turn it on for such a game and say whether it helps.
- [ ] **Game language**: pick another language the game has.

## 8. Online, returning and the Message Board

- [ ] **Online server**: Wiimmfi or WiiLink in Mario Kart Wii, plain
  and with a pack.
- [ ] **Return to RiftWii.** In a game, HOME > Wii Menu should bring
  you back to RiftWii, also in Mario Kart Wii packs and CTGP-R 1.02.
  Settings > Wii Menu button: Wii Menu sends you to the real Wii Menu.
- [ ] **Message Board.** After playing, the Wii Menu's Message Board
  lists the game.

## 9. The RiftWii channel

- [ ] **Install** it with the RiftWii channel app in the Homebrew
  Channel, on a Wii and on a Wii U.
- [ ] **Start RiftWii from the Wii Menu** through the channel, and
  return to it from a game.

## 10. Other loaders

- [ ] **Starting games through RiftWii from another loader**: a loader
  with a Friivolution path pointed at RiftWii, or the USB Loader GX
  build that hands launches to RiftWii (docs/HEADLESS.md).

## 11. Experimental

- [ ] **GameCube controller adapter for Wii U** (Settings > GameCube
  adapter, Check the GameCube adapter): in the menu and in games. If the
  adapter is found but shows no controllers, send a report: boot.log now
  has a "first report, port status" line (RC8). On a Wii U, **On** can
  freeze games from the SD card or USB at 97%: RiftWii asks twice before
  it turns On. Keep **Automatic** unless you are testing it. From
  Automatic, left goes straight to Off without the warnings; right goes
  to On (fixed after RC9).
- [ ] **In-game screenshots** (Settings > In-game screenshots): take a
  few in two or three games, restart RiftWii, look in
  `sd:/riftwii/screenshots`. Say which games worked.
- [ ] **CTGP-R 1.03** from a USB or SD game. The supported way is still
  the Homebrew Channel. If you try it from RiftWii, send the report:
  boot.log should end with "Homebrew app: IOS..., hardware access on"
  (new in RC7). Since RC9 RiftWii also adds `disable_ios_exploit = yes`
  to `sd:/ctgpr/config.ini` (boot.log: "CTGP: config.ini set to skip
  CTGP's own IOS exploit"); check that online races still work.

## 12. Problem reports

- [ ] **Settings > Send a problem report**: the link opens and the QR
  code scans.
- [ ] **A game crash.** If something crashes for you, start RiftWii
  again and send the report it offers. It should include
  `gamecrash.txt`.
- [ ] **Offline.** With no network, the report is saved as
  `sd:/riftwii/report.txt` and the menu stays usable.
- [ ] **When paste.rs can't be reached** (some networks block it), the
  report goes to dpaste.com instead and the link shows as usual (RC8).

## 13. Games with their own fixes

From a USB drive or the SD card where you can:

- [ ] **Kirby's Return to Dream Land**: past the Wii Remote screen,
  with screenshots on and off, with and without a pack.
- [ ] **Super Smash Bros. Brawl**: load a stage in any mode.
- [ ] **New Super Mario Bros. Wii**: play a few minutes.
- [ ] **Rhythm Heaven Fever**: past its first screens.
- [ ] **Resident Evil 4**: GameCube controllers work.
- [ ] **Excite Truck** from the SD card.
- [ ] **Prince of Persia: The Forgotten Sands**, Driver: San Francisco,
  The Adventures of Tintin, We Dare: they boot (RiftWii leaves their
  code alone).

## Known issues

- The pink bar in the Kirby games with width 720 and borders removed
  (issue 13).
- CTGP-R 1.03 started from RiftWii (use the Homebrew Channel).
- Super Mario Galaxy can go black when it returns to the RiftWii
  channel.
