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
**RiftWii {{VERSION}}.** Pick any of GameTDB's covers for a game, WiiSX and Wii64 start, and a fix for 007 NightFire's Wii Remote. Every RiftWii offers this update by itself (Settings > Check for a new version).

![Choose cover: every cover GameTDB has for the game]({{SCREENSHOTS}}2610-choose-cover.png)

## What changed

- **Choose cover.** A game's settings have a new **Choose cover** row, below GameTDB cover. It shows every cover GameTDB has for the game (up to four kinds, from every region), and the one you pick is kept on the SD card, so it stays offline too. A mod's tile can get its own cover the same way. **Back to automatic** undoes it.
- **WiiSX and Wii64 start.** RiftWii used to refuse them with "does not fit next to RiftWii", because they need the memory RiftWii itself sits in. They now start from the Homebrew list and from the Emulators view, and quitting them comes back to RiftWii (checked on a Wii U).
- **007 NightFire (GameCube): the Wii Remote keeps working in missions.** NightFire's missions only read their controllers when a GameCube controller answers, so with none plugged in the Wii Remote and HOME stopped at the first mission. RiftWii now keeps them reading. Checked in Dolphin only: please try it on a Wii.
- **007 Everything or Nothing from a USB drive** now switches to its main game the way it already did from the SD card. Not tried on a Wii yet.
- **Wii Remote rumble in GameCube games** is sent at a pace the Wii's Bluetooth chip can keep up with, so a game that buzzes the motor very fast can't swamp it.
- **Games start after Copy a game disc.** After copying a disc, some games (Super Mario Galaxy 2 among them) failed to start in the same session with "the apploader asked for a load outside RAM". They start now.
- **The RiftWii channel installer from the Homebrew Channel** no longer stops with "NAND access was not restored" while it looks for your cIOS (confirmed by a tester on a Wii).
- **The picture picker's messages** (No pictures found., Could not load pictures.) no longer hide behind the Back to automatic button.

## What to test

1. **007 NightFire** with a Wii Remote and no GameCube controller: play into the first mission. Do the Remote and HOME keep working?
2. **007 Everything or Nothing from a USB drive:** it should go back to RiftWii for a moment, then start its main game.
3. **Choose cover** on a few games and a mod tile, then Back to automatic.
4. **WiiSX or Wii64** from the Homebrew list, and quitting back to RiftWii.
5. **Copy a game disc**, then start the copy straight away.

## Known issues

- **Everything or Nothing's THX and MGM logo movies** show in the wrong colours (dark red and green instead of gold and silver). The game itself and its other scenes look right.
- **The DualShock 3 over USB** hasn't been confirmed to work: RiftWii sets it up, but no button presses have been seen from one yet.
- **USB gamepads:** `MultiIn` 2 to 4 and pads that pack their sticks in their own way aren't supported yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
