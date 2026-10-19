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
**RiftWii {{VERSION}}.** Kirby Air Ride and Melee get their sound back, Home shows the SD card's games without waiting for the USB drive, and fixes from your reports and a code audit. Every RiftWii offers this update by itself (Settings > Check for a new version).

![A mod kept in a folder named with its own ID shows on Home under the folder name]({{SCREENSHOTS}}2610-541-mod-folder.png)

## What changed

- **Kirby Air Ride's sound plays clean instead of static, and Melee gets its music back.** RiftWii didn't find where Kirby's sound engine sets up its sound memory, so the sound chip played the wrong memory, and both games kept their sound in too small a space: Kirby stopped after its save screen and Melee played almost nothing. Tested in Dolphin only (Kirby reaches a race with no static, Melee plays its music). On a Wii the sound is likely still wrong; we couldn't test it on one yet.
- **Home reads the SD card and the USB drive in the background.** The menu answers while they are read: the SD card's games and the Wii's channels show as soon as the card is done, and the USB drive's games join them. Settings opens at once instead of waiting for the scan (12 seconds behind a tester's 326-game WBFS drive), and a WBFS drive is read sooner. Checked in Dolphin; please try it on a Wii.
- **REX starts again with saves on the SD card turned on.** RiftWii refused it as "don't work together with Riivolution packs yet" even with no pack picked. Now REX starts and keeps its save on the Wii for now, and `boot.log` says so. A Riivolution pack's files with a code build are still refused, as before. We couldn't test this on a Wii yet.
- **A mod in a folder named with its own ID shows under that folder's name.** For example, `usb:/games/Mario Kart Double Dash Plus [GM4E05]/game.iso` holds a disc that still says GM4E01, the original game's ID, so Home showed it as Mario Kart: Double Dash!!. Home now uses the folder's name when its ID isn't the disc's. Covers and saves still follow the disc's ID.
- **When a GameCube game doesn't show up, the log says why.** `session.log` names each file in a game's folder that RiftWii can't use and what to do about it: a Dolphin `.gcz`, a `.wia`, an NKit image or an archive (convert it with Dolphin, or unpack it), a file that isn't a GameCube disc, or one the drive can't read. `.ciso` files on a USB drive now get Home's popup on how to convert them, as the SD card's do.
- **Cheats and updates are safer to download.** Cheats come over https, and a new download no longer changes the code of a cheat you turned on. The in-app update downloads only RiftWii's own release files from GitHub.
- **Fewer ways to lose a save, a cover or an install.** POWER and HOME wait for a WAD install to finish writing; restoring the oldest GameCube memory card backup no longer deletes it; a cover that fails to write keeps the old one; a damaged GameCube patch no longer stops RiftWii at every start; and the game's saves on the SD card are kept safe from several rare file system mistakes. Most of these can't be shown in Dolphin and weren't tested on a Wii.

## What to test

1. Kirby Air Ride and Melee on a Wii: is the sound clean, and does Melee play its music?
2. Home with games on both the SD card and a USB drive: do the SD card's games show first, and does Settings open at once?
3. REX (or another code build) with saves on the SD card turned on: does it start, and does `boot.log` say where the save is?
4. A mod in a `Title [ID]` folder (like Mario Kart Double Dash Plus `[GM4E05]`): does Home show the folder's name?
5. A GameCube game that didn't show up before: does `session.log` now say why?

## Known issues

- **GameCube is a trial:** some games may not start or may stop. Send a problem report from Settings > Help when one doesn't.
- **Cars 2 is cursed:** it goes black if RiftWii changes anything in it. RiftWii warns before you play it (and Toy Story 3 and Disney Infinity, which share its engine).
- **Ghostbusters from an RVZ** can still freeze in the hotel: play it from an ISO or WBFS.

Everything new since 2.7.0: [docs/CHANGES_2610.md](https://github.com/KakarottoCake/Riftwii/blob/main/docs/CHANGES_2610.md).

## Download

Grab `riftwii-{{TAG}}.zip` below. `riftwii.dol` is the same build as the zip's `boot.dol` (SHA256 {{DOL_SHA256}}, {{DOL_BYTES}} bytes). `riftwii-update.pack` is what the in-app update reads (the plugins, the themes and the channel installer); you don't need to download it.

If something goes wrong, use Settings > Help > Send a problem report and send the link (you don't need to take out your SD card), or open an issue with `sd:/riftwii/session.log`, `sd:/riftwii/boot.log`, your Wii model, your Menu IOS and your cIOS.

## License and source

RiftWii is free software under the GNU General Public License, version 3 or later, with no warranty. Its source is this repository at the tag `{{TAG}}`; the zip carries `LICENSE.txt`, `NOTICE.md` and `SOURCE.txt`. [NOTICE.md](https://github.com/KakarottoCake/Riftwii/blob/main/NOTICE.md) credits every project RiftWii contains or follows, with each piece's origin and license.
