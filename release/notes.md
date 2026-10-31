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
**RiftWii {{VERSION}}.** USB gamepads now work inside games, wired Xbox 360 pads need no config, a cleaner menu font, covers you choose, and fixes for the 4-Wii-Remote freeze and several games. Every RiftWii offers this update by itself (Settings > Check for a new version).

![What's new in this version]({{SCREENSHOTS}}2610-whatsnew-781.png)

![A game's settings: the GameTDB cover row]({{SCREENSHOTS}}2610-gametdb-cover.png)

## What changed

- **No more freeze with a 4th Wii Remote** (#138). With four Remotes setting up at once on a real console, a slow answer could get asked for twice, and the second answer was written to the wrong place in memory. RiftWii now never asks twice for an answer that is still coming, and drops any answer that doesn't belong to the question being waited on. Checked in Dolphin with 4 Remotes; not tried on a real console yet.
- **USB gamepads work inside games**, not only in the menu. Starting a game resets the Wii's USB and the pad took a moment to come back; the game's controller code now notices. A PS4 pad takes one slot instead of four, PS3 pads get the extra wake-up some of them need, and every step of a pad's setup shows up in problem reports. Plug the pad in before starting a game.
- **Wired Xbox 360 controllers** (Microsoft's and third-party ones) work as GameCube controllers with no config file, in the menu and in games, while Settings > Controllers > USB input devices is on. LB or RB is Z, the triggers are L and R, and Guide (or Back + Start) is the way back to RiftWii.
- **A cleaner menu font**, M PLUS 1 SemiBold: lighter and crisper on a TV. It's a file on the SD card (`apps/riftwii/font.ttf`); the in-app update brings it. If it's missing, RiftWii uses the Wii Menu's font and tells you.
- **Covers you choose.** Put a PNG named after the game's ID in `riftwii/covers` on the SD card or USB drive (`RMCE01.png`) and RiftWii shows it instead of GameTDB's. Games with a second cover on GameTDB (Brawl, Mario Kart Wii) get a **GameTDB cover** row in their settings: First or Second.
- **Home's shortcuts on every controller.** Changing the view, a cover up close and a game at random now work on a GameCube controller (Y + Z, Y or Z, hold B), a Classic or Pro Controller (Y + X, Y or X, hold ZL) and the Wii U GamePad (X + Y), not only the Wii Remote.
- **EA's 007 GameCube games** (NightFire, Agent Under Fire, Everything or Nothing) no longer stop on a black screen: the programs they load from the disc get RiftWii's patches too. NightFire plays in Dolphin; the other two aren't tested.
- **007 Quantum of Solace** (Wii) no longer stays black: it gets all of the Wii's fast memory, as Resident Evil 4 does.
- **Prince of Persia: The Forgotten Sands** no longer stays black after the Ubisoft logo: games that check their own code get no RiftWii changes beyond their own fix, and their settings page says which rows don't apply.
- **Restart and Power off with a USB drive** no longer crash.
- **Homebrew apps started from RiftWii** see the GameCube adapter, USB pads and a USB keyboard again.

## What to test

1. **Four Wii Remotes** in the menu, connected at start and one at a time: does it stay responsive?
2. **A USB gamepad or a wired Xbox 360 pad** in a GameCube game (plug it in before starting the game).
3. **Your own cover**: a PNG in `sd:/riftwii/covers`, and the GameTDB cover row on Brawl or Mario Kart Wii.
4. **007 Agent Under Fire or Everything or Nothing**, if you have them.

## Known issues

- **USB gamepads:** `MultiIn` 2 to 4 and pads that pack their sticks in their own way aren't supported yet; a pad (other than a wired Xbox 360 pad) needs a config.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
- **Retro Rewind online from the disc** gave one tester error 50330 on a Wii U. A likely fix is in; a problem report from anyone who still sees it helps.
- **WADs:** Korean and vWii-only WADs aren't read yet. WADs need an SD card for the emulated NAND.
- **Emulators and other USB partitions:** some emulators may not understand `usb1:/` or `usb2:/` paths for games on a second partition.
- **GameCube is a trial:** some games may not start or may stop. Send a problem report from Settings > Help when one doesn't.
- **The channel installer goes black on some Wii U consoles** (#317). RiftWii offers Not now instead of locking you out, but the cause isn't confirmed: `sd:/riftwii/channel.log` in a problem report helps.
- **The Wii U Pro Controller's stick can stop, then drift, in the menu** after opening a game's page (#239).
- **The GameCube memory card in other regions' copies of Resident Evil 2 (PAL, Japan) isn't covered yet.**
- **Zelda Collector's Edition may show "No Signal"** at 480p on a real Wii (seen once; Dolphin doesn't show it).
- **Cars 2 is cursed:** it goes black if RiftWii changes anything in it. RiftWii warns before you play it (and Toy Story 3 and Disney Infinity, which share its engine).
- **Ghostbusters from an RVZ** can still freeze in the hotel: play it from an ISO or WBFS.

## Download

Grab `riftwii-{{TAG}}.zip` below. `riftwii.dol` is the same build as the zip's `boot.dol` (SHA256 {{DOL_SHA256}}, {{DOL_BYTES}} bytes). `riftwii-update.pack` is what the in-app update reads (the plugins, the themes and the channel installer); you don't need to download it.

If something goes wrong, use Settings > Help > Send a problem report and send the link (you don't need to take out your SD card), or open an issue with `sd:/riftwii/session.log`, `sd:/riftwii/boot.log`, your Wii model, your Menu IOS and your cIOS.

## License and source

RiftWii is free software under the GNU General Public License, version 3 or later, with no warranty. Its source is this repository at the tag `{{TAG}}`; the zip carries `LICENSE.txt`, `NOTICE.md` and `SOURCE.txt`. [NOTICE.md](https://github.com/KakarottoCake/Riftwii/blob/main/NOTICE.md) credits every project RiftWii contains or follows, with each piece's origin and license.
