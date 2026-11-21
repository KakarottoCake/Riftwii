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
**RiftWii {{VERSION}}.** WiiWare and Virtual Console games kept in an emulated NAND on a hard drive (or in WiiFlow's folder) can now be copied to the SD card, where RiftWii plays them, and GameCube games no longer turn red on PAL 60. Every RiftWii offers this update by itself (Settings > Check for a new version).

![Copy to sd:/nands/01 in the emulated NAND manager]({{SCREENSHOTS}}2611-208-copy-to-sd.png)

## What changed

- **Copy a WiiWare or Virtual Console game to the SD card.** In Settings > Games > Manage emulated NAND, pick a game in an emulated NAND on the USB drive (USB Loader GX's) or in another folder on the SD card (WiiFlow's `nands/default`), then **Copy to sd:/nands/01**. The game, its save, its ticket and the shared files it needs are copied, and the original stays where it was. A game already in the SD card's folder can be copied to the USB drive the same way. RiftWii plays these games only from the SD card, so this is how an emulated NAND made by another loader can be used. An emulated NAND at the very top of a drive (ModMii's SNEEK builds) isn't listed.
- **GameCube games on PAL 60 no longer turn red.** Settings > Picture > Video mode's PAL 60 and PAL 50 now reach GameCube games too, so a US or Japanese GameCube game goes out as PAL 60 the way the TV expects. 480p and 240p stay as they were. This couldn't be checked on a real Wii and a PAL TV yet.
- **Mario Kart Wii Deluxe starts again.** Its XML names a folder its download doesn't have, and RiftWii stopped every launch over it. A missing folder now only stops a launch when files are missing too, as when a copy stopped part way.
- **The missing-files message is easier to read**: larger, darker letters, and each pack's list starts with "Missing for <pack>", so the pack's own XML no longer looks like one more missing file.
- **The Disc Channel tile looks like the Wii Menu's** and takes the theme's tile color, and every Settings page lines up on the same edges.
- **The built-in themes (Bookshelf and Midnight) and the theme kit are CC0**, free to change and share. The kit's starting theme.ini now lists every setting.

## What to test

1. **Settings > Games > Manage emulated NAND** with USB Loader GX's or WiiFlow's emulated NAND: copy a WiiWare or Virtual Console game to the SD card, then find it on Home and play it.
2. **A US or Japanese GameCube game with Video mode on PAL 60**, on a PAL TV: are the colors right?
3. **Mario Kart Wii Deluxe**, or any pack that used to say files were missing.

## Known issues

- **Copying a game to the SD card** (Manage emulated NAND) has only been tried in Dolphin, and only with a game that has no shared files.
- **GameCube games on PAL 60** haven't been tried on a real Wii and a PAL TV.
- **Cars 2 doesn't start for one tester.** It's being looked into; a problem report helps.
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
