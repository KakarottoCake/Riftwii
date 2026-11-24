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
**RiftWii {{VERSION}}.** A tester's idea: a mod's own Home tile can now use its GameTDB entry (like Newer Super Mario Bros. Wii, SMNP03) for its name, cover and About page. Also two fixes from testers: USA games no longer crash on a Japanese Wii, and the Wii Remote's pointer works again after the Remote goes to sleep and wakes up. Everything from 2611-233 (the Rift Store and more) is in it too. Every RiftWii offers this update by itself (Settings > Check for a new version).

![What's new in this version]({{SCREENSHOTS}}2611-236-whats-new.png)

## What changed

- **Mod tiles can use a GameTDB entry.** Some mods are on GameTDB as games of their own (Newer Super Mario Bros. Wii, Retro Rewind, Project+). When you make a Home tile for a mod (Mods > Make a Home tile with these mods), pick its entry from a list, or type its six-character ID. The tile on Home then gets that entry's name, cover, box and 3D cover, and its About page shows the mod's own details. On an existing tile, it's under Options > GameTDB entry. The tile still starts the same game with the same mods. A name you typed yourself is never changed, and Automatic matches by the tile's name as before.

![Picking a mod tile's GameTDB entry]({{SCREENSHOTS}}2611-236-gametdb-entry.png)

- **USA games on a Japanese Wii** (and other games from another region) no longer crash at start, as Super Mario Galaxy 2 did with a green screen. When Game language is left on the Wii's own language and the game's region doesn't have that language, the game now gets its region's usual one instead (English for a USA game). A language you pick on the game's page is still used as it is.
- **The Wii Remote's pointer** works again after the Remote times out and you wake it up. Before, it disappeared or drifted (with a Nunchuk plugged in) until RiftWii was restarted.

## What to test

1. **Make a Home tile for a mod** that has a GameTDB entry (Newer, Retro Rewind, CTGP...) and pick the entry: does Home show its name and cover, and its About page its details?
2. **A game from another region**, for example a USA game on a Japanese Wii: does it start, and in English?
3. **Leave the Wii Remote alone in the menu** until it turns off, then press A: does the pointer go where you aim?

## Known issues

- **The way back from a WAD game** hasn't been tried on a real Wii or vWii yet, including this release's vWii fix.
- **The Rift Store** hasn't been tried on a real Wii yet: its downloads, a full SD card, and Stop while files are being put in place.
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
