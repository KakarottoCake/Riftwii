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
**RiftWii {{VERSION}}.** Settings is easier to read and search, and a game's Mods and Saves pages can download mods and saves. Every RiftWii offers this update by itself (Settings > Check for a new version).

It replaces 2611-268, which was taken down the same day: on a real Wii it refused to start every game ("RiftWii's memory was damaged"). The change behind that is out of this release.

![A Settings page: each setting's value, a line on what it does, and when it takes effect]({{SCREENSHOTS}}2611-268-settings.png)

## What changed

### Settings
- **Every setting shows its value and says what it does.** Under each one is a short line in plain words, and a tag says when a change takes effect: *Next game* or *After a restart* (no tag: at once).
- **A on a setting with several choices opens a list** of them, with the one in use marked. The arrows still step through them.
- **On/off settings are switches, and things that do something are buttons** (Import, Test, Rescan...).
- **Search every setting** with the new search button beside the version (or press 1, or Y on a Classic or GameCube controller). Accented letters count as plain ones, so *ecran* finds *écran*.
- **Show advanced settings**, at the end of a page, hides what most players never need (cIOS, the copy of the Wii's memory, debugging, network packs). Search always finds them.
- **A game's own settings (Options on its page) work the same way.** Its cIOS, region fixes, loading speed and memory row sit behind the same switch, unless you changed one.
- Long setting names no longer get cut off beside short values (Drives' *Games on the SD card*).

![Search finds a setting in any section]({{SCREENSHOTS}}2611-268-settings-search.png)

![A game's own settings]({{SCREENSHOTS}}2611-268-game-options.png)

### Mods and saves
- **Get more mods** on a Wii game's Mods page downloads Riivolution mods from GameBanana, and RiftWii's own copies of Luma's Workshop mods, with icons and full names. A mod already on the card asks before it's replaced.
- **Get a save** on a game's Saves page downloads a save from GameBanana: a Wii save goes into the game's SD or emulated NAND save folder, a GameCube save onto its RiftWii memory card, each with a backup of what it replaces.

![Get more mods on a game's Mods page]({{SCREENSHOTS}}2611-268-get-more-mods.png)

![Get a save on a game's Saves page]({{SCREENSHOTS}}2611-268-get-a-save.png)

### Miis and the emulated NAND
- **Manage emulated NAND > Miis** shows the Wii's Miis and the emulated NAND's side by side, A to Z. Copy one either way (it asks first), take one out of the emulated NAND, or Copy all.
- **Install WAD files** moved into Manage emulated NAND, and its lists are A to Z.

### Games
- **F-Zero GX (USA)** gets past its save page: it restarts itself after making the save, and RiftWii now lets that restart run.
- **The Zelda: Ocarina of Time / Master Quest disc** starts the game you pick on its menu, and bonus discs with many demos (Mario Kart: Double Dash!! bonus disc, demo discs) can start all of them.
- **LEGO Batman and both LEGO Indiana Jones games** get the GameCube adapter, screenshots, crash recording and cheats back.
- After an IOS reload, RiftWii waits until the console's second memory answers before going on.

### Menu
- **Home's Layout button also sets Home's order.**
- **Japanese text wraps at better places**, and chooser names shrink to fit.
- Buttons that open another page end in ">", the hints along the bottom are no longer pressable, theme names show whole, and the Rift Store lists RiftWii itself.
- The controllers' in-game code now comes as an add-on (the pads plugin), loaded only when a game needs it. Keep the plugins folder when you update; the update does this for you.

## What to test

1. **If you had 2611-268:** does a game start again? Try a Wii game from disc, USB and SD, and a GameCube game.
2. **Settings**: change a few settings, open a choice's list with A, try Search and Show advanced settings, in 4:3 and 16:9.
3. **Get more mods** and **Get a save** on a real Wii: does the list load, and does a download install?
4. **Manage emulated NAND > Miis**: copy a Mii each way.
5. **F-Zero GX (USA)**: make a save, then play. **The Zelda OoT / Master Quest disc**: pick each game on its menu.

## Known issues

- **The Zelda OoT / Master Quest disc**: picking a game went black in one Dolphin setup today, on this build and on the one before. It worked when the fix was made. A problem report from a real Wii helps.
- **Get more mods, Get a save and Miis** were tried in Dolphin only.
- **The way back from a WAD game or a game with emulated-NAND saves** hasn't been tried on a real Wii or vWii yet.
- **The Rift Store** hasn't been tried on a real Wii yet: its downloads, a full SD card, and Stop while files are being put in place.
- **WiiWare and Virtual Console from another region** don't get the language fallback yet.
- **The GameCube crash screen doesn't go back to RiftWii** on A or HOME for some players; the console has to be turned off. It does in Dolphin. A photo of the screen helps.
- **SpongeBob SquarePants: The Movie (PAL)** crashes at random for one tester, and **Cars 2** doesn't start for another. Both are being looked into; a problem report helps.
- **GameCube games on PAL 60**, and **GameCube 16:9** since its fix, haven't been tried on a real Wii.
- **Copying saves or Miis into the Wii's own memory** (Manage emulated NAND) hasn't been tried on a real Wii yet.
- **CTGP-R online:** a whole race and a Time Trial upload haven't been tried from RiftWii yet.
- **Super Smash Bros. Brawl's Reset** still loses the GameCube adapter on an SD card over 2 GB.
- **Phantasy Star Online Episode I & II Plus** doesn't start yet without patches.
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colors.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
- **xdelta patches that really compress a part** are still refused.
- **Japanese and Korean players** can search Settings only by the English words each setting carries: the search keyboard has a to z.
