<!--
The GitHub release notes for the next release. The release workflow
(.github/workflows/release.yml) fills in the {{...}} names and drops these
comments; docs/DEVELOPING.md (Releasing) says how to run it.

  {{VERSION}}      2610-500            {{TAG}}        v2610-500
  {{DOL_SHA256}}   the DOL's SHA-256   {{DOL_BYTES}}  the DOL's size
  {{SCREENSHOTS}}  this tag's docs/screenshots/ on raw.githubusercontent.com,
                   so ![...]({{SCREENSHOTS}}2610-500-menu.png) shows a
                   screenshot committed before the release

Only what a player notices goes in: leave out CI and workflows, review
bots (CodeRabbit), tools/, tests, docs and refactors (docs/RELEASING.md).

Rewrite the intro, What changed, What to test and Known issues for every
release, then delete the line below: the workflow refuses notes that still
have it, or that haven't changed since the last release's tag.
-->
**RiftWii {{VERSION}}.** Your games are on Home the moment RiftWii starts, a game starts behind its box instead of a wall of text, and there's a new emulated NAND manager, your own covers and favorites for homebrew, and a Nintendont controller.ini import. Most of it comes straight from testers' requests. Every RiftWii offers this update by itself (Settings > Check for a new version).

![A game starting behind its box]({{SCREENSHOTS}}2611-200-launch-box.png)

## What changed

### Home

- **Your games show at once.** Home remembers the games it found last time and shows them the moment RiftWii starts, while the drives are read again in the background. Before, every start was a fresh scan. The scan itself reads less, "All games" is much quicker, and "Games with mods" is now just "Mods".
- **The Covers view shows the edges of the pages either side**, like the Channels view, when Settings > Home > Pages at the sides is on. Point at one and press A to turn to it.
- **The Disc drive tile** now also shows in the Wii and GameCube views.
- **A new SD card starts the widescreen menu on Automatic**, so it follows the Wii's own TV setting. Cards that already have settings keep what they had.

![The Covers view with the pages either side]({{SCREENSHOTS}}2611-200-covers-sides.png)

### Starting a game

- **A game starts behind its box.** The log is hidden behind the game's box and a progress bar. Press any button to see the log, and it also appears by itself if something goes wrong. Settings > Menu > Launch screen switches back to the log.
- **GameCube launches move the progress bar step by step** instead of sitting at 3% and jumping to the game.

### Homebrew

- **Your own covers, 3D covers and boxes for homebrew.** Name the picture after the app's name in its meta.xml (for example `ScummVM.png`) and put it in `riftwii/covers`, `riftwii/covers3d` or `riftwii/boxes`, the same way as for games. The app's folder name works too.
- **Homebrew can be a favorite** and shows in Home's Favorites view.
- **Homebrew covers come from GameTDB** for apps it knows.

![Your own cover for a homebrew app]({{SCREENSHOTS}}2611-200-homebrew-cover.png)

### Emulated NAND

- **Settings > Games > Manage emulated NAND.** For any emulated NAND RiftWii finds:
  - delete a channel you installed into it, with its ticket and save;
  - delete a game's emulated save;
  - copy a game's save, or your Miis, between it and the Wii;
  - remove its copy of the Wii settings.

  Anything it replaces is backed up on the SD card first, and writing to the Wii's own memory asks twice. System software (IOS, the Wii Menu) is never touched.

![The emulated NAND manager]({{SCREENSHOTS}}2611-200-emulated-nand.png)

### Controllers

- **Settings > Controllers > Import from Nintendont** copies the controller.ini files you used with Nintendont into RiftWii, from the root or `nintendont` folder of the SD card or USB drive, or `controllers/VID_PID.ini`. Each file is checked first, and an existing layout is only replaced if you say yes. Restart RiftWii to use them.
- **GameCube "Real card in slot B"** is checked before the game starts, like slot A. If there's no readable card, you're offered the game's SD card instead of a launch that stops with an error.

![Import from Nintendont]({{SCREENSHOTS}}2611-200-nintendont-import.png)

### Covers from SteamGridDB

- **Type your SteamGridDB key in Settings > Online.** The key is hidden while you type and only its last four characters are shown after. Clear it with the row below.
- **Pictures are no longer downloaded again at every start.** A saved picture is never asked for again, and "not found" answers are remembered for a week, so a restart no longer uses up your SteamGridDB allowance.

### Other fixes

- **Game page:** click the disc to see it up close and spin it.
- **CTGP-R updates install again** from a scrubbed game image.
- **Saves are safer.** A game's choices, Home's tiles, controller calibrations and the Riivolution config are written beside the old copy and swapped in, so a power cut while saving no longer loses them.
- **Parental controls also cover games started from another loader** or from `launch.txt`. A restricted game is refused there, since nobody is at the menu to type the PIN.
- **Test files left on the SD card are ignored** by release builds, and covers download over HTTPS.

## What to test

1. **Start RiftWii twice:** are your games on Home at once the second time?
2. **Start a game:** do you see its box, and does a button show the log?
3. **Settings > Games > Manage emulated NAND** on a real Wii, especially copying a save or the Miis to and from the Wii. That part couldn't be tried in Dolphin.
4. **Settings > Controllers > Import from Nintendont** with your own controller.ini, then a game with that controller.
5. **Your own homebrew covers**, and a homebrew favorite.
6. **SteamGridDB:** type your key, start RiftWii twice, and check your allowance didn't drop the second time.

## Known issues

- **Copying saves or Miis into the Wii's own memory** (Manage emulated NAND) hasn't been tried on a real Wii yet.
- **GameCube 16:9** hasn't been tried on a real Wii since its fix.
- **CTGP-R online:** a whole race and a Time Trial upload haven't been tried from RiftWii yet.
- **Super Smash Bros. Brawl's Reset** still loses the GameCube adapter on an SD card over 2 GB.
- **On one tester's console, a GameCube game's crash screen didn't go back to RiftWii.** It does in Dolphin. A problem report from that screen helps.
- **Phantasy Star Online Episode I & II Plus** doesn't start yet without patches.
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colors.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
- **xdelta patches that really compress a part** are still refused.
