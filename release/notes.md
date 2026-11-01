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
**RiftWii {{VERSION}}.** Games without a cover get one made up with their name, parental controls with a PIN, copying a game disc to a `.wbfs` file, themes with a moving background, and channel updates that need no Wii Remote. Every RiftWii offers this update by itself (Settings > Check for a new version).

![What's new in this version]({{SCREENSHOTS}}2610-whatsnew-793.png)

![Home's Shelf view: a made-up cover in front, paper spines with the games' names]({{SCREENSHOTS}}2610-made-up-covers.png)

![Settings > Games > Parental controls]({{SCREENSHOTS}}2610-parental-controls.png)

## What changed

- **Games without a cover get a made-up one**: the game's name on a blank Wii-style case, in every Home view, instead of an empty card. On the Shelf view they get a paper spine with the name down it, in the game's colour. A theme can draw its own blank case (`blank_cover.png`).
- **Parental controls** (Settings > Games > Parental controls). Set a PIN, pick the highest age rating allowed, and choose whether games rated above it are locked (the PIN starts them) or hidden from Home. Games with no known rating can be allowed or treated as too old. Forgot the PIN? Deleting the `parental_pin` line in `sd:/riftwii/settings.txt` turns the controls off.
- **Copy a game disc** (Settings > Drives > Copy a game disc). The Wii disc in the drive is copied to a `.wbfs` file on a FAT32 USB drive or the SD card, with every partition, so the game then plays from Home without the disc. It needs a d2x cIOS; copies over 4 GB are saved in pieces. When it's done, RiftWii says whether the game is on Home now, or which setting hides it and what to change.
- **Themes can loop a moving background**: a short animation behind every screen, from `backdrop_00.png` up, with `[backdrop] frames` and `fps` in `theme.ini` (docs/THEMES.md). The new **Vaporwave** theme on the themes site uses it.
- **Updating the channel needs no Wii Remote.** Update now on the update prompt installs straight away and comes back to RiftWii, so a player with only a GameCube controller (on a Wii U) isn't stuck in the installer.
- **Covers:** a case's colour now matches the game's full ID, a cover no longer sits on a grey card, and a WiiWare or Virtual Console game whose data was removed shows its cover. A mod's own Home tile gets the mod's own cover from GameTDB.
- **Code mods remember how they start** (with or without RiftWii's extras), and a **Pro Controller** recognized late works for Mario Kart Wii's Classic Controller cheats without pressing HOME first.
- **For developers:** Settings > Help > USB Gecko log sends RiftWii's log to a USB Gecko in slot B, and the WiiRD debugger can start games with Gecko OS's debugger handler (docs/DEVELOPING.md).

## What to test

1. **Games without a cover**: how they look in each Home view, the Shelf view especially.
2. **Parental controls**: set a PIN, then try a locked game and the Hide mode.
3. **Copy a game disc**, if you have a d2x cIOS: copy one to USB or SD, then play it without the disc.
4. **Updating the channel** from the update prompt, if your channel is old.

## Known issues

- **Copy a game disc** hasn't been tried on a real Wii yet: Dolphin can't read a whole disc.
- **EA's 007 GameCube games:** NightFire loads, but the Wii Remote stops working when a mission starts; Agent Under Fire and Everything or Nothing still don't load. A problem report from each helps.
- **USB gamepads:** `MultiIn` 2 to 4 and pads that pack their sticks in their own way aren't supported yet; a pad (other than a wired Xbox 360 pad) needs a config.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
