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
**RiftWii {{VERSION}}.** A fix for big game libraries: with hundreds of games, RiftWii could run out of memory and restart, most often while it downloaded an update or the game details, so an update never finished. Everything from 2611-236 (GameTDB entries for mod tiles and more) is in it too. Every RiftWii offers this update by itself (Settings > Check for a new version).

If you have a big library and RiftWii keeps restarting when it updates, copy `boot.dol` from this release's zip to `sd:/apps/riftwii/` by hand once, or turn off Wii Menu sounds in Settings before you update.

## What changed

- **Downloads go straight to the SD card.** The update (RiftWii and its plugins) and GameTDB's game details are written to the card as they arrive, instead of being held whole in memory first. An update now needs a few MB of free space on the card, and almost no memory.
- **Covers take turns in big libraries.** RiftWii no longer keeps room for every game's cover at once: it leaves memory for the shelf's full covers and the rest of the menu, and covers you scroll back to are read again from the card. A library that fits works as before.

## What to test

1. **A big library** (a few hundred games): scroll Home and the shelf with full covers, then update, or open a game's About page so its details download. Does RiftWii keep going?

## Known issues

- **The way back from a WAD game** hasn't been tried on a real Wii or vWii yet, including this release's vWii fix.
- **The Rift Store** hasn't been tried on a real Wii yet: its downloads, a full SD card, and Stop while files are being put in place.
- **Big libraries** were checked in Dolphin only (an update and a 400-game list with memory held back), not yet on a real Wii with a big drive.
- **GameTDB entries for mod tiles** were checked in Dolphin only. The first start after updating reads the game details once more in the background, so mod covers can be found for every game on your drives.
- **This release's two fixes** were checked in Dolphin only: a USA game on a real Japanese Wii, and a real Wii Remote waking up in the menu, are still to try.
- **WiiWare and Virtual Console from another region** don't get the language fallback yet.
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
