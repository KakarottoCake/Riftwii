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
**RiftWii {{VERSION}}.** Real GameCube memory cards work on a Wii, WAD files can sit in your own game folders and WiiWare games started from them go back to RiftWii, and Project+ no longer goes black with the GameCube adapter. Every RiftWii offers this update by itself (Settings > Check for a new version).

![What's new in this version]({{SCREENSHOTS}}2611-219-whats-new.png)

## What changed

- **Real GameCube memory cards work on a Wii.** The menu's sound kept the card in slot A or B from being unlocked, so games couldn't see it, and a card that never answered could freeze the menu. Both are fixed.
- **WAD files are found in your Game folders.** Until now only `wiiware/` on the SD card and the USB drive was read. Folders added in Settings > Drives > Game folders now count too (and one level of folders inside them), so WADs can sit next to your other games, for example in `sd:/roms/wii - wiiware`.
- **WiiWare and Virtual Console games started from a WAD go back to RiftWii.** HOME > Wii Menu in such a game restarted a Wii U to its own menu, because the RiftWii channel wasn't in the emulated NAND the game runs in. RiftWii now copies its channel there before the game starts. Not tried on a real Wii or vWii yet.
- **WADs work when USB Loader GX keeps its channels on the USB drive.** They were refused with "The WAD may be damaged". RiftWii plays channels only from the SD card, so it now prepares them in `sd:/nands/01`, and Settings > Install WAD files works the same way.
- **Project+ no longer goes black with the GameCube adapter** when its virtual SD image is spread over thousands of pieces on the card. The list of pieces took too much of the game's memory; it now stays on the SD card and is read as needed.
- **240p keeps PAL 60's colors.** A console set to PAL 60 on an RGB SCART cable showed games in 240p red. Wii games on a console set to PAL, and GameCube games when Video mode asks for PAL 60 or PAL 50, now keep PAL 60 in 240p. 480p stays as it was.
- **LEGO Star Wars: The Complete Saga** has the GameCube adapter, screenshots, crash recording and cheats back, L, R, Z and Start held leaves it, and Reset keeps the controllers.
- **For pack makers:** a pack made only of memory patches can give a Wii game GameCube controller support. RiftWii now finds a `PADRead` the pack writes into memory, as it does in a patched `main.dol`.

## What to test

1. **A real GameCube memory card** in slot A or B with a GameCube game: does the game see it and save?
2. **A WAD in a folder from Settings > Drives > Game folders**: does it show on Home and start?
3. **A WiiWare or VC game from a WAD**, then HOME > Wii Menu: does it go back to RiftWii?
4. **Project+ with the GameCube adapter plugged in.**
5. **240p on a console set to PAL 60** with an RGB SCART cable: are the colors right?

## Known issues

- **The way back from a WAD game** hasn't been tried on a real Wii or vWii yet.
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
