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
**RiftWii {{VERSION}}.** Wired Switch Pro Controllers, the Wii Remote in Harry Potter and the Goblet of Fire, Just Dance from RVZ again, and no more silent drops to the Homebrew Channel when the menu runs out of memory. Every RiftWii offers this update by itself (Settings > Check for a new version).

![RiftWii restarts after running out of memory and asks for a report]({{SCREENSHOTS}}2611-out-of-memory.png)

## What changed

- **Wired Switch Pro Controller.** Plug a Nintendo Switch Pro Controller in with a USB cable and it plays as a GameCube controller, with nothing to put on the SD card: in the menu, in GameCube games and in Wii games that take a GameCube controller. A, B, X and Y are themselves, ZL and ZR are L and R, R is Z and + is Start. Leave the sticks alone for a moment as it starts; holding X, Y and + for three seconds sets their middle again. No rumble or player lights yet, and the Switch 2 Pro Controller isn't covered. Thanks to grantendo, who tried it on a Wii U.
- **The menu no longer kicks you back to the Homebrew Channel without a word when it runs out of memory.** With a very big library it could, with no crash file and nothing in the log. Now it notes what happened, restarts RiftWii, says so on Home and asks whether to send a report.
- **Covers download where you're looking.** The page on screen (or the game picked on the shelf) gets its covers first, then the ones around it, instead of the list from the top down. The shelf's boxes download near what's on screen, as you get to them, rather than the whole library in the background. If GameTDB asks RiftWii to slow down, it waits and tries again instead of giving up on covers for the session.
- **Harry Potter and the Goblet of Fire (GameCube): the Wii Remote works.** RiftWii couldn't patch the program the game loads, and the game never treated the Wii Remote as plugged in. Both are fixed. Checked in Dolphin.
- **Just Dance from an RVZ** runs again (Just Dance 4 played a song in Dolphin), where it crashed before its first picture.
- **GameCube mods that add their own code**, such as Super Mario Sunshine's 16:9 and 60 fps patch, start again instead of crashing at once.
- **Call of Duty: Finest Hour (GameCube)** should no longer stop on RiftWii's crash screen: a floating-point error the game handles itself now goes back to the game. Not tried with the game yet.
- **The Wii Menu's font, sounds and music** should load again when RiftWii is started from its channel, where 2610-810 fell back to its own font (seen on a vWii). Checked in Dolphin, not on a console yet.
- **A Wii with a wrong clock** (back to 2000 after a flat clock battery, or never set) should be able to update and download covers again.
- **Missing mod files:** a whole missing folder is listed as one folder, and files a mod would only add to the game (not replace) are no longer asked about.

## What to test

1. **A wired Switch Pro Controller** in the menu and a game or two (Settings > Controllers > Check USB controllers shows it).
2. **Harry Potter and the Goblet of Fire** with only a Wii Remote: do the menus and the game take its presses?
3. **Just Dance** from an RVZ, and **Super Mario Sunshine with its 16:9/60 fps mod**.
4. **A big library:** browse Home and the shelf for a while. If RiftWii ever restarts with "RiftWii ran out of memory", please send the report it offers.
5. **The Wii Menu font** on Settings > Menu font, started from the RiftWii channel.

## Known issues

- **Everything or Nothing's THX and MGM logo movies** show in the wrong colours.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **USB gamepads:** `MultiIn` 2 to 4 and pads that pack their sticks in their own way aren't supported yet (the Switch Pro is built in).
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
