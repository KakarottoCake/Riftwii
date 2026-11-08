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
**RiftWii {{VERSION}}.** Games get back much of the memory RiftWii used inside them, mods load faster, and parents can keep kids out of the Homebrew Channel. Every RiftWii offers this update by itself (Settings > Check for a new version).

![What's new in this version]({{SCREENSHOTS}}2611-128-whats-new.png)

## What changed

- **More memory for games.** Everything RiftWii keeps running inside a game was rewritten to take less room, with the help of grantendo and JackW. A game from a USB drive with the GameCube adapter now gives up about 20 KB instead of 62 KB, a Wii game from an RVZ about 75 KB instead of 149 KB, and a disc game with a 1,000-file mod about 125 KB instead of 138 KB. Mods and code builds that ran out of memory before may work now.
- **Mods load faster.** Pack files on the SD card or a USB drive are read straight into the game, with about a quarter of the trips to the card. Saves are written in bigger pieces. Mods that run from a card image (`sd.raw`, like Project+ and REX) read a little ahead, so their many small reads become fewer trips.
- **RVZ games** use RiftWii's own decompressor: much smaller, just as fast.
- **Parental controls can lock the HOME Menu.** Two new rows in Settings > Games > Parental controls: **HOME Menu buttons** shows the Homebrew Channel and Priiloader buttons, asks for the PIN first, or hides them so the HOME Menu has only Wii Menu and Power off; **Settings** can ask for the PIN before Settings opens. Both only work once a PIN is set. Not tried on a real Wii yet.
- **Updates restart into the new version.** From the next update on, the popup after an update says Restart and starts the new RiftWii right away, instead of leaving for the Homebrew Channel.
- **Mod tiles get their own 3D cover and box** from GameTDB, not only the flat cover (thanks grantendo).
- **Code mods (Project+, REX and others):** the Mods page has an **Extras** row (On / Off / Ask every launch) and a **Crash logs** row, off at first. The Start question sits in the middle of the screen. A code build only uses the USB controllers plugged in before you press Play, which leaves the game more memory.
- **GameCube adapter:** no rumble while its gray USB plug is out (that plug powers the motors), and Settings > Controllers > Check USB controllers says when it is. With two adapters, unplugging and plugging back one no longer drops the other.
- **Wii U Pro Controller:** Mario Kart Wii's Classic Controller cheats work without opening the HOME Menu first. Not tried on a real Wii yet.

![The HOME Menu with the Homebrew Channel and Priiloader hidden]({{SCREENSHOTS}}2611-128-home-menu-parental.png)
![The new HOME Menu buttons row in Parental controls]({{SCREENSHOTS}}2611-128-parental-home-buttons.png)

## What to test

1. **Big mods** from the SD card and from a USB drive (Mario Kart packs, Newer, Retro Rewind, Galaxy packs): do they load as fast or faster? Any freezes or missing files?
2. **Saves** in a mod with its own save folder: save, turn the Wii off, start again. Is the save there?
3. **Code builds** (Project+, REX, Project M), especially from `sd.raw`, with Extras and Crash logs on and off.
4. **Parental controls:** set a PIN, then try each HOME Menu buttons choice and the Settings lock.
5. **Mario Kart Wii with a Wii U Pro Controller** and Classic Controller cheats: do they work right away?
6. **GameCube adapter:** pull the gray plug while playing. Does rumble stop and do the controllers keep working?

## Known issues

- **The emulated NAND on USB hasn't been tried with d2x on a real Wii yet.** If a game won't start with it, the problem report says why.
- **Phantasy Star Online Episode I & II Plus** doesn't start yet.
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colours.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
