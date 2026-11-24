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
**RiftWii {{VERSION}}.** The Rift Store: homebrew apps to browse, install, update and remove from RiftWii itself, with your saves kept. Home's bar, the game page and Settings are tidier, and WAD games go back to RiftWii on a Wii U. Every RiftWii offers this update by itself (Settings > Check for a new version).

![The Rift Store]({{SCREENSHOTS}}2611-229-store.png)
![An app's page in the Rift Store]({{SCREENSHOTS}}2611-229-store-app.png)
![What's new in this version]({{SCREENSHOTS}}2611-229-whats-new.png)

## What changed

### The Rift Store

- **The Rift Store button** on Home's bar (the bag, left of Search) opens it, and so does Settings > Online > Rift Store. It lists about 290 homebrew apps by category: emulators, games, media players, loaders, system tools and more. Each app has its icon, a description, its size, date, license, the consoles it works on, and a QR code to its own page. **1** searches, **2** shows the apps on your SD card and their updates.
- **Install, Update and Remove** from the app's page. A download is checked against the file the list approved before anything is written, and an update puts all the new files in or none of them. If the SD card is too full, the store says how much room to make first.
- **Your saves and settings stay.** An update replaces the app itself, but keeps any file the app or you changed since it was installed. Remove takes the app away, then asks whether to delete its saves and settings too. Keep them is the button that's lit, and A, B or HOME keep them.
- **What each app works with:** the Wii Remote, Nunchuk, Classic Controller and GameCube controller pictures on its page. The **Without a Wii Remote** button on the store's front (−) shows only apps you can play with a Classic or GameCube controller.
- **On a Wii U,** apps listed for the Wii only say "Wii only", and the store asks before installing one.
- **Daily builds** of Snes9x RX, WiiStation and Nintendont are in the store, so you get their newest fixes.
- The store loads only while it's open and takes no memory from the rest of RiftWii. Themes can replace its pictures (docs/THEMES.md).

### Home

- **Search and Layout are on the bar** in every layout, next to Show and the gear, instead of moving to the top corners.
- **The gear's name shows on the status line,** like the other buttons' names.
- **B jumps to the next letter** and now shows that letter for a moment.
- **Games on the SD card open sooner** while RiftWii is still reading the drives.
- **The tiny labels (WII, GC, SD, USB, MODS) are thicker** and readable on every theme.
- **Names sort without case or accents** everywhere: Home's A to Z, the folder pickers, the theme list and the GameCube card names.

### Game page and Settings

- **At 16:9, the game's page, Manage game and About use the whole width.** A subtitle that's too long scrolls instead of being cut, the help text has two full lines, and buttons that open another screen show an arrow.
- **About** has a smaller cover, the controllers in the middle, and a story box that never runs into the bottom bar.
- **Settings' buttons just say Picture, Games, Home...** and its help has the (i) like every other page.
- **Install WAD files is under Settings > Games,** next to Manage emulated NAND.
- **Manage emulated NAND groups its list** (game saves, channels, system channels, hidden titles), shows game names, says where each title is, and won't delete system titles.

### WAD games on a Wii U

- **HOME > Wii Menu in a WiiWare or Virtual Console game started from a WAD** froze a tester's Wii U on a black screen. RiftWii now also copies the IOS its channel starts on into the game's emulated NAND. Not tried on that Wii U yet.

## What to test

1. **The Rift Store:** install an app, start it from Home's Homebrew view, then remove it. Does the store ask about its saves?
2. **An update in the store** for an app whose settings you changed: are they kept?
3. **The Without a Wii Remote filter** and an app's controller pictures: do they match what you know of the app?
4. **On a Wii U:** a WiiWare or VC game from a WAD, then HOME > Wii Menu. Back in RiftWii?
5. **Home's bar** in your usual layout, and B's letter jump.

## Known issues

- **The way back from a WAD game** hasn't been tried on a real Wii or vWii yet, including this release's vWii fix.
- **The Rift Store** hasn't been tried on a real Wii yet: its downloads, a full SD card, and Stop while files are being put in place.
- **The GameCube crash screen doesn't go back to RiftWii** on A or HOME for some players; the console has to be turned off. It does in Dolphin. A photo of the screen helps.
- **SpongeBob SquarePants: The Movie (PAL)** crashes at random for one tester. It's being looked into; it hasn't happened in Dolphin.
- **Copying a game to the SD card** (Manage emulated NAND) has only been tried in Dolphin, and only with a game that has no shared files.
- **GameCube games on PAL 60** haven't been tried on a real Wii and a PAL TV.
- **Cars 2 doesn't start for one tester.** It's being looked into; a problem report helps.
- **Copying saves or Miis into the Wii's own memory** (Manage emulated NAND) hasn't been tried on a real Wii yet.
- **GameCube 16:9** hasn't been tried on a real Wii since its fix.
- **CTGP-R online:** a whole race and a Time Trial upload haven't been tried from RiftWii yet.
- **Super Smash Bros. Brawl's Reset** still loses the GameCube adapter on an SD card over 2 GB.
- **Phantasy Star Online Episode I & II Plus** doesn't start yet without patches.
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colors.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
- **xdelta patches that really compress a part** are still refused.
