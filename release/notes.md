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
**RiftWii {{VERSION}}.** Themes can draw the back of a game's box and frame every cover, the menu no longer freezes with big themes, games with emulated-NAND saves go back to RiftWii, and a GameCube controller's pointer goes away when it's unplugged. Every RiftWii offers this update by itself (Settings > Check for a new version).

![The Plaza theme on Home]({{SCREENSHOTS}}2611-249-plaza.png)

![A box turned to its back on the shelf, drawn by the Plaza theme]({{SCREENSHOTS}}2611-249-box-back.png)

![What's new]({{SCREENSHOTS}}2611-249-whats-new.png)

## What changed

- **Themes can draw the back of a game's box.** On the shelf, Up turns the box around. A theme can now give that back its own picture (`blank_back`), and the game's screenshots go on it as on a real case. Without one, the back is drawn as before.
- **Themes can frame every cover on Home**, not only the lit one (`cover_frames = yes` in the theme).
- **Bigger themes install from the theme browser.** It takes themes of up to 192 files, so themes with a looping backdrop fit. Three new community themes use all of this: Dashboard, Wave and Plaza (Settings > Themes > Get more themes). An older RiftWii shows them with its own box back.
- **The menu no longer freezes on "Reading the SD card...".** A memory check could wait for the SD card while holding a lock the card's own reading needed, so both waited forever. It showed up with a theme with large music, but could have happened anywhere.
- **Games with saves in an emulated NAND go back to RiftWii.** On a vWii, HOME > Wii Menu went to the Wii Menu. RiftWii lost track of its channel once d2x's NAND emulation was on, and the game itself then looked for the channel in the emulated NAND and restarted the console. Both are fixed. Checked in our Dolphin build that emulates d2x, not yet on a real Wii or vWii.
- **A GameCube controller's pointer goes away** half a second after it's unplugged, from the Wii's port or the adapter, and the next controller's pointer starts in the middle of the screen. Before, swapping ports could leave a pointer on screen for good.
- **The Rift Store's list** (already live for every RiftWii, since it's online): every app has an icon now, each app's link goes to its own page, the descriptions are rewritten, and apps whose download is gone are off the list.

## What to test

1. **A game with saves in an emulated NAND** (the game page's Saves: SD, emulated NAND), from USB or SD: HOME > Wii Menu. Do you come back to RiftWii?
2. **A new theme**: Settings > Themes > Get more themes, install Plaza, Wave or Dashboard. Look at Home and the shelf, and press Up on a box to see its back.
3. **A GameCube controller**: unplug it while its pointer is on screen, then plug one into another port.

## Known issues

- **The way back from a WAD game or a game with emulated-NAND saves** hasn't been tried on a real Wii or vWii yet, including this release's fix.
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
