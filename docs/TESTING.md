# Testing RiftWii

This is the full test plan for the next stable release. It covers
everything that changed since the last stable one, 2.7.0, and the basics
that must keep working. Please don't try to do all of it in one go: pick
the sections that match what you have (a disc, a USB drive, mods, a Wii U,
a GameCube adapter, another loader) and go through those.

Each step says what should happen. When something else happens, that's
a result worth sending. Steps marked **(Wii)** or **(Wii U)** need that
console; everything else is worth trying on both.

## Before you start

1. Put the release zip's `sd-card` folder on your SD card (and
   `usb-drive` on your USB drive if you use one), replacing the old
   files. Or, with Settings > Updates on Beta, let RiftWii update itself
   (that's a test too: section 2).
2. Games on the SD card or a USB drive need d2x cIOS v11 beta3 or newer
   in 248 to 252 (249 and 251 are the usual ones). Discs work without one.
3. Note your console: Wii or Wii U, region, and your cIOS slots. A
   problem report lists them for you.

## How to send results

Problem reports need no computer, and you don't need to take out your
SD card:

1. In RiftWii, go to **Settings > Send a problem report**.
2. Scan the QR code with your phone, or copy the link it shows.
3. Post the link with a line on what you did and what happened.

After a failed launch or a crash, RiftWii offers the report by itself at
the next start: press A. If the upload fails, the report is saved as
`sd:/riftwii/report.txt`.

For things that worked, a short "works" with the game, where it ran from
(disc, SD, USB, RVZ) and your console is just as useful.

## 1. Start-up and Home

- [ ] **Clean install.** Copy the zip's `sd-card` folder over your old
  files. Settings, play history, covers, mods and games stay.
- [ ] **First start on a fresh card.** Delete `sd:/riftwii/settings.txt`
  and `sd:/riftwii/tutorial_done.txt`. The tutorial shows once, and the
  Disc Channel question is asked once. Settings > Tutorial shows it again.
- [ ] **What's new** pops up once after an update, and Settings > What's
  new shows it again.
- [ ] **No SD card.** Start RiftWii without one: it says so, and Try again
  works once you put the card in.
- [ ] **Views.** Press 1 (Y on a Classic Controller) to cycle All, the
  drives and Favourites. Settings > Home tiles: Covers, Names, Channels and
  the Shelf. Each one shows your games, and the page turns (Minus and
  Plus) slide.
- [ ] **Shelf**: the box spines, box art in 16:9, and the disc drive's box
  with its disc whole (Wii).
- [ ] **Channels view**: the games' own banners and icons, with their
  banner sound when you open one; the arrows switch banners.
- [ ] **Letter jump.** B on Home (L on a GameCube controller) jumps to
  the next letter. Sorting puts "The Legend of Zelda" under L.
- [ ] **Search.** The search button (top right; Z on a GameCube
  controller, ZL on a Classic Controller): Home filters as you type, the
  box above the keyboard names the first matching games, and the count is
  right. Cancel brings the whole list back.
- [ ] **Home notices** (an update, a sent report) pop up once, not every
  time you come back from Settings.
- [ ] **Clock.** Settings > Clock: Automatic, 12-hour and 24-hour.
- [ ] **Menu font.** Settings > Menu font: Wii Menu (the console's own
  font, read from the NAND) and RiftWii's. Japanese and Korean menus use
  the Wii Menu's font.
- [ ] **Languages**: English, French, Spanish, Italian, Portuguese,
  Japanese and Korean menus have no English left over in the screens
  you use.
- [ ] **Aspect ratio** (Settings): 16:9 and 4:3 menus, on a Wii U's TV
  and a Wii's.
- [ ] **Themes** (Settings > Theme): Default, Midnight and Bookshelf, each
  with its own colours and music. Theme updates come with app updates.
- [ ] **Transitions**: opening a game, Settings and a channel zooms and
  slides smoothly, with no freeze.
- [ ] **Menu music and sounds** (Settings): on and off, Normal, Quiet
  and Off.
- [ ] **Pointer.** The Wii Remote's pointer is steady, also tilted. With a
  Classic Controller or a GameCube controller plugged in, pressing its A
  while the Remote points clicks where the Remote points.
- [ ] **Rumble**: the Wii Remote rumbles lightly when the pointer goes over a button.
- [ ] **Balance Board** paired: the menu doesn't crash or hang.
- [ ] **Dimming.** Leave the menu alone: it dims when the console's
  screen saver is on, and wakes up on any button.
- [ ] **Menu screenshots.** Hold 1 and press HOME on a Wii Remote. A PNG
  appears in `sd:/riftwii/screenshots`.
- [ ] **HOME Menu** in RiftWii: Homebrew Channel, Wii Menu, Priiloader and
  Power off.
- [ ] **Power button**: the console's and a Wii Remote's turn the Wii off.
- [ ] **Look for games again** (Settings) finds a game you just copied.
- [ ] **Two USB drives** (Wii U: a drive in each port, or port 2 alone):
  the menu and the game read the drive with the games.
- [ ] **Credits and license** (Settings) opens and scrolls.

## 2. Updates

- [ ] **Settings > Updates** switches between Stable and Beta.
- [ ] **Check for a new version.** On Beta it offers the newest beta; on
  Stable it doesn't offer anything older. The update shows its progress
  and RiftWii restarts into the new version.
- [ ] **Offline**: with no network, the start-up check gives up after a
  few seconds and the menu works.

## 3. Discs

- [ ] **A retail disc with nothing on.** It boots as from the Wii Menu.
- [ ] **Disc Channel tile** (Settings > Disc Channel): the tile shows the
  disc's banner and starts it.
- [ ] **A disc with a pack on the SD card** (Newer Super Mario Bros. Wii,
  a Mario Kart Wii mod, a Super Mario Galaxy 2 pack).
- [ ] **A disc with a pack on the USB drive** (Retro Rewind).
- [ ] **Burned discs** (early Wiis) and **a USB DVD drive**.

## 4. Games on the SD card or a USB drive

- [ ] **WBFS and ISO** on FAT32 and NTFS, in `wbfs/` and `games/`.
- [ ] **RVZ images** on the SD card and a USB drive: a few games of
  yours, nothing on. Resident Evil 4 from an RVZ (fixed in 2610-213).
- [ ] **Game cIOS on Automatic**: boot.log lists your d2x slots and says
  which one it picked. Also try a fixed slot.
- [ ] **d2x version check**: with a d2x older than v11 beta3, RiftWii says
  so in the menu (once per session for v11 beta1/beta2).
- [ ] **Saves on the SD card** (game page, Saves: SD): the game saves and
  loads its save from `sd:/riftwii/saves`. Do it for a game on the SD card
  and one on USB, on d2x 249 (base 56) and 251 (base 58). boot.log says
  whether d2x's emulation or RiftWii's own save code kept it. USB games
  with SD saves on d2x 249 start again (fixed in 2610-251).
- [ ] **New Super Mario Bros. Wii from the SD card on a Wii's d2x 249**
  (with and without Newer): it boots (fixed in 2610-208).
- [ ] **Leaving a game** with Reset: back to RiftWii, then start another.
- [ ] **A USB 3 drive on a Wii U** and **a drive bigger than 1 TB**: no
  stall before a USB game starts.

## 5. Mods

- [ ] **Packs on the SD card**: Newer, a Galaxy pack, a Mario Kart Wii
  pack (Retro Rewind, MKWii Deluxe) with their options.
- [ ] **Pack names**: the Mods page shows the name the pack's author gave
  it, and the file's name when you highlight it.
- [ ] **A mod with nothing picked**: Start offers to turn it off and start
  the game.
- [ ] **Riivolution's Ocarina XML** (a pack for every game) doesn't mark
  every game as modded.
- [ ] **Packs on the USB drive** with a game on USB, then with a disc.
- [ ] **Retro Rewind from USB** with a game on USB: the SD card stays
  usable (the cIOS is picked for it).
- [ ] **Saves**: NAND, Separate and Fresh start on a modded game.
- [ ] **Just Dance mods**, **Metroid: Other M Redux**, and other big
  packs (RiiMajor).
- [ ] **A missing pack folder**: RiftWii refuses with a message.

## 6. Code builds, SD images and cheats

- [ ] **Project+, PMEX Remix and REX** from the SD card, with Brawl from a
  disc, the SD card and USB.
- [ ] **SD images** (Brawl's Mods page, Make an SD image... under the build): the
  progress bar runs, the image works in game, making it again keeps the
  old one until the new one is whole. A build with thousands of files
  takes seconds to plan, not minutes.
- [ ] **A build inside an image** shows up as a code build and starts
  (Project+ from `pplus.raw`).
- [ ] **Cheats.** On a game page, Cheats downloads the list. Pick a few
  and play. Codes with values ask for them on a keypad; a code you
  merged from another file stays.
- [ ] **One game's cheats and settings don't carry over** to the next
  game you open.

## 7. Picture and language

- [ ] **Picture width, Deflicker, Black borders** on a few games.
  Mario Super Sluggers keeps its own picture whatever they say (fixed in
  2610-213).
- [ ] **Video mode**: NTSC, PAL 60, PAL 50, 480p and The console's.
- [ ] **Region video fix** on a game page, for a game from another region.
- [ ] **Game language**: another language the game has.

## 8. Online, the Message Board and RiiTag

- [ ] **Online server**: Wiimmfi and WiiLink in Mario Kart Wii, plain and
  with a pack. Games that never went online say "No online play".
- [ ] **Message Board**: after playing, the Wii Menu's Message Board lists
  the game.
- [ ] **RiiTag** (if you have one): the tag shows the game you started.

## 9. The RiftWii channel

- [ ] **Install** it with the RiftWii channel app, on a Wii and on a Wii
  U. It shows its 16:9 splash and starts RiftWii.
- [ ] **Return to RiftWii**: in a game, HOME > Wii Menu brings you back.
  Known issue (#62): some games freeze or go black there; list which ones.
  Settings > Wii Menu button: Wii Menu sends you to the real Wii Menu.

## 10. Other loaders

- [ ] **USB Loader GX** with the build that hands launches to RiftWii.
- [ ] **WiiFlow**: a plugin that starts RiftWii with a launch file
  (docs/HEADLESS.md, "A WiiFlow plugin"), for a USB game and for Brawl
  with `code_build=Project+`. With the game on USB, boot.log shows the
  drive found, also when the plugin had the drive mounted.
- [ ] **launch.txt**: the same lines in `sd:/riftwii/launch.txt`, then
  start RiftWii from the Homebrew Channel. The file is deleted.

## 11. GameCube controllers

- [ ] **Wii GameCube ports** in the menu and in games.
- [ ] **Wii U GameCube adapter** (Settings > GameCube adapter, Check the
  GameCube adapter, Check each cIOS): in the menu and in SD and USB games,
  front and back ports. On Automatic, a USB game on a Wii U now starts on
  the base-58 d2x (usually 251) with the adapter on: try a few games
  besides Brawl. Games the adapter breaks keep it off with a boot.log
  line.

## 12. Problem reports and crashes

- [ ] **Settings > Send a problem report**: the link opens and the QR code
  scans.
- [ ] **A crash**: the crash screen shows, and the next start offers the
  report, with `gamecrash.txt` for a game crash.
- [ ] **Offline**: the report is saved as `sd:/riftwii/report.txt`.

## 13. Games with their own fixes

From a USB drive or the SD card where you can:

- [ ] **Kirby's Return to Dream Land**: past the Wii Remote screen.
- [ ] **Super Smash Bros. Brawl**: load a stage.
- [ ] **New Super Mario Bros. Wii**: play a few minutes.
- [ ] **Rhythm Heaven Fever**: past its first screens.
- [ ] **Resident Evil 4**, also from an RVZ: GameCube controllers work.
- [ ] **Wii Fit Plus** with a Balance Board.
- [ ] **Metroid Prime Trilogy** with a cheat on, inside one of its games.

## Known issues

- Some games freeze or go black after HOME > Wii Menu instead of going
  back (#62): Animal Crossing: City Folk, Captain Rainbow, Epic Mickey,
  Super Mario Galaxy and Wii Fit Plus so far. Set Settings > Wii Menu
  button to Wii Menu meanwhile.
- The pink bar in the Kirby games with width 720 and borders removed
  (#13), and Black borders: Remove all in some games (#15).
- CTGP-R started from RiftWii (use the Homebrew Channel).
