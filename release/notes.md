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
**RiftWii {{VERSION}}.** CTGP-R now starts through its own channel from a USB drive or an SD card, without asking for the disc, and RiftWii's own CTGP-R launch keeps your Mario Kart save. The Shelf scrolls more smoothly, and any tile can have your own box and 3D cover. Every RiftWii offers this update by itself (Settings > Check for a new version).

## What changed

### CTGP-R

- **CTGP-R starts through its own channel, from a disc, a USB drive or the SD card.** This is now the default. From a game on USB or the SD card, the channel used to restart itself at start-up and then ask for the disc ("Please insert the Mario Kart Wii game disc"). RiftWii now starts it the way the Homebrew Channel does, so it keeps the game and goes on to its own menu. Tried on a real Wii from USB (PAL, through d2x): the channel's menu, Mario Kart Wii and custom races. From the SD card and from a disc it has not been tried on a real Wii yet.
- **CTGP-R goes online again on this path.** Online play and Time Trial uploads are CTGP-R's own here, and RiftWii no longer turns them off. On a real Wii it reached CTGP-R's room selection; a whole online race and a Time Trial upload have not been tried yet.
- **No "Before you play" note before CTGP-R's channel.** The offline-only note now shows only for RiftWii's own CTGP-R launch.
- **RiftWii's own CTGP-R launch is now something you opt into**, with an empty file `ctgpnative.txt` next to RiftWii's `boot.dol`. It is offline only, as before.
- **RiftWii's own CTGP-R launch keeps your Mario Kart save.** It stopped at "Continue without saving / Return to the Wii Menu" when the SD card had no `riivolution/save` folder. CTGP-R keeps its save there, so RiftWii now copies the Wii's save in once, and never overwrites a save already on the card. Tried in Dolphin (a save copied in, a save already there, no save at all). It has not been tried on a real Wii yet.

### Menu

- **Smoother Shelf scrolling.** The Shelf no longer stops for a moment each time a new box comes into view, and the boxes round the game you're on load first. The grey border round each case is gone. Tried in Dolphin only; the smoothness needs a check on a real Wii.
- **Your own box and 3D cover for any tile**, whatever it's called: `sd:/riftwii/boxes/<name>.png` (a whole case, laid out like GameTDB's full covers) and `sd:/riftwii/covers3d/<name>.png` (like GameTDB's 3D covers), named like your own covers. Tried in Dolphin; not on a real Wii yet.

### Controllers

- **Reset keeps the GameCube adapter and USB controllers in Wii games without mods.** Before, the game came back from Reset without them until you started it again from RiftWii. Tried on a Wii U with NARUTO: Clash of Ninja Revolution 3. Super Smash Bros. Brawl on an SD card over 2 GB keeps its own Reset, which still loses the adapter; `boot.log` says which case it was.

## What to test

1. **CTGP-R from USB and from the SD card:** does it go straight to CTGP-R's menu, with no "insert disc"?
2. **CTGP-R from a disc.**
3. **CTGP-R online:** a whole race in a Worldwide room, and a Time Trial ghost upload.
4. **RiftWii's own CTGP-R launch** (`ctgpnative.txt`) on a card without `sd:/riivolution/save`: does your save load?
5. **The Shelf** on a real Wii: is scrolling smooth?
6. **Your own box or 3D cover** for a mod tile.
7. **Reset** in a Wii game without mods, with the GameCube adapter or a USB controller.

## Known issues

- **CTGP-R online:** a whole race and a Time Trial upload haven't been tried from RiftWii yet.
- **CTGP-R's Rankings button and crash handler** haven't been tried on a real Wii yet.
- **Super Smash Bros. Brawl's Reset** still loses the GameCube adapter on an SD card over 2 GB.
- **On one tester's console, a GameCube game's crash screen didn't go back to RiftWii.** It does in Dolphin. A problem report from that screen helps.
- **The emulated NAND on USB hasn't been tried with d2x on a real Wii yet.** If a game won't start with it, the problem report says why.
- **Phantasy Star Online Episode I & II Plus** doesn't start yet without patches.
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colors.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
- **xdelta patches that really compress a part** are still refused.
