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
**RiftWii {{VERSION}}.** CTGP-R now takes its cup layouts from your own CTGP-R install, loads its code only when you start CTGP-R, and accepts installs that updated themselves again. CTGP-R's way back to RiftWii now works from any folder, and Sonic Unleashed sees the GameCube adapter on a Wii U. Every RiftWii offers this update by itself (Settings > Check for a new version).

![What's new in this version]({{SCREENSHOTS}}2611-162-whats-new.png)

## What changed

### CTGP-R

- **CTGP-R installs that updated themselves are accepted again.** 2611-151 refused them. CTGP-R's own updates now start, and a track update needs no RiftWii update. It was tried on a real Wii (PAL, from USB through d2x), with an updated CTGP-R. An SD card or disc launch, NTSC-U and NTSC-J have not been tried on a real Wii yet.
- **The cup layouts come from your own CTGP-R install.** The default, the alphabetical and your own `cuplayout.cup` layouts are read from your CTGP-R files on the SD card, so RiftWii no longer carries a list of cups. On a real Wii the default and alphabetical layouts raced and a Time Trial ghost saved; your own `cuplayout.cup` has not been tried on a real Wii yet.
- **CTGP-R's code is only loaded when you start CTGP-R.** It is now a separate add-on (`ctgpr.rwp`) in the zip and the update pack. Everyone else gets a RiftWii that is about 110 KB smaller and 111,744 bytes of the Wii's first memory block back. If the add-on is missing, RiftWii says so.
- **CTGP-R's Mario Kart Channel button brings back the RiftWii that started it**, from its own folder. Before, a RiftWii outside `sd:/apps/riftwii` came back not knowing where it lived, and the next CTGP-R start said a part of RiftWii was missing. On a real Wii the button came back to RiftWii and CTGP-R started again from it, on an earlier version of this change. The final version, the Rankings button and CTGP-R's crash handler have not been tried on a real Wii yet.
- **Leaving CTGP-R:** Reset restarts CTGP-R, as Reset does in a game. The way out is CTGP-R's own channel button, which now brings back RiftWii, or the HOME Menu's Wii Menu.

### Wii games and controllers

- **Sonic Unleashed sees the GameCube adapter on a Wii U.** Some Wii games ask which GameCube ports have a controller before they read them, and on a Wii U the answer was "nothing", so the game ignored the adapter and kept asking for a GameCube controller. RiftWii now answers the way a GameCube game gets it. Tried on a Wii U with the adapter, and in Dolphin. Most Wii games don't ask, so they are unchanged.
- **Super Smash Bros. Brawl with a pack that only swaps files can load a match.** It used to stop at "Can't Alloc Heap(MenuInstance)". RiftWii's code now sits in the second memory block for Brawl. A Training match loads and the GameCube controller plays, and Brawl's Album opens, on a Wii U and in Dolphin. Reset with a pack, cheats together with a pack, replays and the stage builder, USB, Subspace Emissary and packs with memory patches have not been tried on a real Wii yet.
- **A Wii Remote no longer gets stuck setting up its Classic Controller or Nunchuk** (seen in Eternal Darkness). After its retries it now plays on its own. Tried in Dolphin only, so it is not tried on a real Wii yet.
- The Wii U GameCube adapter note now says the adapter may only work in game from the lower back USB port, as a tester measured on every port.

### Settings and games

- **Settings > Drives > Game folders:** add and remove the folders RiftWii looks for games in, with a folder picker (up to 8 folders; `wbfs`, `games` and a drive's top folder are always searched). It saves to the same `game_folders` line in `settings.txt`. Tried in Dolphin with the SD card; a USB drive and a real Wii have not been tried yet.
- **GameCube named memory cards:** on a GameCube game's page, Memory card > A named card gives a card per player, or one card for several games. New card names one with the keyboard. It is backed up before each start like a game's own card. Tried in Dolphin (Luigi's Mansion starts on a named card); saving in a game to a named card and a real Wii have not been tried yet.
- **GameCube mods shipped as an xdelta patch** that only mention compression are read now, where before they never showed up as a game. A patch that really is compressed is still refused, and the log says how to make one RiftWii can read (`xdelta3 -S none`).
- A folder, song or theme with a line break in its name can no longer turn part of the name into a setting. Not tried on a Wii or in Dolphin.

![Game folders in Settings > Drives]({{SCREENSHOTS}}2611-162-game-folders.png)

### Network

- **Names are looked up through the DNS servers saved in the Wii's settings** when the Wii's own lookup fails (covers, updates, network packs, the network test). Nothing changes when your router hands out the DNS servers. Not tried on a real Wii or in Dolphin; it needs someone with typed-in DNS servers.

![A named memory card on a GameCube game]({{SCREENSHOTS}}2611-162-named-cards.png)

## What to test

1. **CTGP-R** from USB, from the SD card and from the disc, with an install that has updated itself. Try the default and the alphabetical cups, and your own `cuplayout.cup` if you have one.
2. **CTGP-R's Mario Kart Channel button**, with RiftWii in a folder other than `sd:/apps/riftwii`: does it bring RiftWii back, and does CTGP-R start again from it? Also the Rankings button, if you can find it.
3. **Sonic Unleashed with the GameCube adapter on a Wii U** (Options > Controller Settings > Nintendo GameCube Controller).
4. **Super Smash Bros. Brawl with a file-swap pack**: a match, Reset, and cheats if you use them.
5. **Eternal Darkness or another GameCube game** with a Wii Remote and Classic Controller or Nunchuk.
6. **Settings > Drives > Game folders** with a USB drive.
7. **A named memory card** in a GameCube game: save in the game, then start it again.
8. **Typed-in DNS servers** in the Wii's Internet settings: do covers and the update check work?

## Known issues

- **CTGP-R's Rankings button and crash handler** haven't been tried on a real Wii yet.
- **On one tester's console, a GameCube game's crash screen didn't go back to RiftWii.** It does in Dolphin. A problem report from that screen helps.
- **The emulated NAND on USB hasn't been tried with d2x on a real Wii yet.** If a game won't start with it, the problem report says why.
- **Phantasy Star Online Episode I & II Plus** doesn't start yet without patches.
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colors.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
- **xdelta patches that really compress a part** are still refused.
