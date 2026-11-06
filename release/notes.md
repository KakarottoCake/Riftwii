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
**RiftWii {{VERSION}}.** Keep every game's saves in one emulated NAND, now also on a USB drive you can share between two Wiis. Also: hide, rename or delete a game from its page, a QR code to report how a game runs, GameCube games that play straight from Nintendont's or Dolphin's memory card, an optional "start my last game" countdown, smoother loading dots and a fix for Wii Menu sounds that wouldn't load. Every RiftWii offers this update by itself (Settings > Check for a new version).

![Settings > Games: Wii saves, Emulated NAND and NAND keeps]({{SCREENSHOTS}}2611-emulated-nand-settings.png)

## What changed

- **One emulated NAND for every game.** Settings > Games has three new rows. **Wii saves** picks *On the Wii* or *Emulated NAND* for every game. **Emulated NAND** says where it is: the folder USB Loader GX's settings name, `sd:/nands/01`, or `usb:/nands/01` on the USB drive. **NAND keeps** is either just the saves, or the saves plus the Wii's settings and Miis (like USB Loader GX's "Full" mode; the folder needs a whole NAND for that). A game's **Saves** has a new first choice, **Default**, which follows Settings, and still lets you pick something else for that game.
- **The emulated NAND can be on the USB drive.** One drive shared between two Wiis finds the same saves on both. It needs a FAT32 partition that is one of the drive's first four partitions (d2x can't use a GPT drive), and a game started from the SD card or the USB drive. A USB folder in USB Loader GX's settings works now too instead of being refused.
- **Manage game** on a game's page: **hide** it from Home (Settings > Home shows hidden games again), **rename** it, or **delete** its image, covers and cheats (its saves only if you say so).
- **Report how a game runs.** Press 1 on a game's page for a QR code that opens the compatibility list's form with the game already filled in.
- **GameCube games can play from Nintendont's or Dolphin's memory card** (a new Memory card choice, when such a card is there), and Better-Nintendont's cards in `apps/gc_devo` are found too. Covers and cheats that USB Loader GX or WiiFlow already downloaded are used when RiftWii has none.
- **Start the last game right away** (Settings > Home, off unless you turn it on): a five-second countdown that any button cancels. Hold B while RiftWii starts to skip it.
- **The disc slot light** pulses while a game loads or a disc is copied, like the Wii Menu. Settings > Games turns it off.
- **Wii Menu sounds and music:** RiftWii now opens the Wii's file permission check once, before reading, instead of after each refusal. One tester's Wii refused every file the old way and fell back to RiftWii's own sounds, and lost about 7 seconds at start-up.
- **Loading dots** are smooth circles now, and they stay where they belong on a 16:9 menu.
- **Covers use less memory**, and a cover that doesn't fit is skipped instead of restarting RiftWii.
- **N64 Virtual Console games in an emulated NAND** (Ocarina of Time, Mario Golf) start again.

![A game's page: Saves and Manage game]({{SCREENSHOTS}}2611-game-page-manage.png)

![Save location with Default]({{SCREENSHOTS}}2611-save-location-default.png)

![Press 1: report how it runs]({{SCREENSHOTS}}2611-report-how-it-runs.png)

![Start the last game right away]({{SCREENSHOTS}}2611-start-last-game.png)

## What to test

1. **Emulated NAND on USB:** set Settings > Games > Wii saves to *Emulated NAND* and Emulated NAND to *USB drive* (or point USB Loader GX at `usb1:/nands/01/`), play a game from USB or SD and save. Does the save show up in USB Loader GX, and the other way round? Try *Saves, settings and Miis* too if your folder holds a whole NAND.
2. **Wii Menu sounds** (Settings > Menu > Wii Menu sounds and music) on a Wii where they didn't load before.
3. **Manage game:** hide, rename and delete a test game.
4. **A GameCube game on Nintendont's card**, and the start-last-game countdown.
5. **The loading dots** on a 16:9 TV.

## Known issues

- **The emulated NAND on USB hasn't been tried with d2x on a real Wii yet.** If a game won't start with it, the problem report says why.
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colours.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
