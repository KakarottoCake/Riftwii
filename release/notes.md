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
**RiftWii {{VERSION}}.** Get more themes works again, and A only presses what the pointer is on. Every RiftWii offers this update by itself (Settings > Check for a new version).

![Get more themes, with Dashboard just installed]({{SCREENSHOTS}}2611-252-themes.png)

![What's new]({{SCREENSHOTS}}2611-252-whats-new.png)

## What changed

- **Get more themes works again.** RiftWii's website moved from riftwii.github.io to riftwii.com in mid-November, and the old address now sends everything on to the new one. For safety the theme browser doesn't follow a server sending it somewhere else, so the list never loaded, on every version since the move. RiftWii now gets themes from riftwii.com itself.
- **Wave's preview shows in the theme browser** (already live for this version, since it's on the website): it was bigger than RiftWii takes.
- **A only presses what the pointer is on.** In the Rift Store, A with the pointer on empty space opened the lit item, and a GameCube controller's A on a button at the bottom (Search, for one) went to the page underneath instead. Now, with the pointer on screen, A presses what it points at and nothing else; with the pointer hidden, A presses the lit item as before.

## What to test

1. **Settings > Menu > Get more themes**: does the list load, and does a theme install?
2. **The Rift Store with a GameCube controller**: point at Search and press A. Does the keyboard open?
3. **A on empty space** in the Rift Store: nothing should happen.

## Known issues

- **The way back from a WAD game or a game with emulated-NAND saves** hasn't been tried on a real Wii or vWii yet, including 2611-249's fix.
- **The Rift Store** hasn't been tried on a real Wii yet: its downloads, a full SD card, and Stop while files are being put in place.
- **Big libraries** were checked in Dolphin only (an update and a 400-game list with memory held back), not yet on a real Wii with a big drive.
- **GameTDB entries for mod tiles** were checked in Dolphin only. The first start after updating reads the game details once more in the background, so mod covers can be found for every game on your drives.
- **2611-236's two fixes** were checked in Dolphin only: a USA game on a real Japanese Wii, and a real Wii Remote waking up in the menu, are still to try.
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
