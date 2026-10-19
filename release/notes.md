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
**RiftWii {{VERSION}}.** Fixes for 2610-489, from your reports. Every RiftWii offers this update by itself (Settings > Check for a new version).

## What changed

- **REX starts again with saves on the SD card turned on.** RiftWii refused it as "don't work together with Riivolution packs yet" even with no pack picked: the SD card save was the only thing that needed RiftWii inside the game, and d2x can't give a code build like REX the SD card's saves. Now REX starts and keeps its save on the Wii for now, and `boot.log` says so. A Riivolution pack's files with a code build are still refused, as before. We couldn't test this on a Wii yet.
- **A mod in a folder named with its own ID shows under that folder's name.** For example, `usb:/games/Mario Kart Double Dash Plus [GM4E05]/game.iso` holds a disc that still says GM4E01, the original game's ID, so Home showed it as Mario Kart: Double Dash!! and it was hard to find. Home now uses the folder's name when its ID isn't the disc's. A folder named with the disc's own ID (as backup managers name them) still gets the name from GameTDB, and covers and saves still follow the disc's ID.
- **When a GameCube game doesn't show up, the log says why.** Before, some files were passed over without a word (a tester's Simpsons: Hit & Run). Now `session.log` names each file in a game's folder that RiftWii can't use and what to do about it: a Dolphin `.gcz`, a `.wia`, an NKit image or an archive (convert it with Dolphin, or unpack it), a file that isn't a GameCube disc, or one the SD card or USB drive can't read, in the file system's own words.
- **`.ciso` files on a USB drive** now get Home's popup on how to convert them, as the SD card's do.

## What to test

1. REX (or another code build) with saves on the SD card turned on: does it start, and does `boot.log` say where the save is?
2. A mod in a folder named with its own ID (`Title [ID]`, like Mario Kart Double Dash Plus `[GM4E05]`): does Home show the folder's name?
3. A GameCube game that didn't show up before: does `session.log` now say why?
4. A `.ciso` on the USB drive: does Home show the popup?

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
