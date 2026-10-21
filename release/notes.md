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
**RiftWii {{VERSION}}.** GameCube colors and saves get fixed, more games get their sound on a Wii, and the four-Remote freeze gets logging to find it. Every RiftWii offers this update by itself (Settings > Check for a new version).

## What changed

- **Mario Party 4 and Sonic Gems Collection get their true colors in Wii mode.** Paletted textures now use their own colors: Mario Party 4 no longer draws silver stars, a green text window, noise on the envelope, and streaks on the Mini-Game Mode curtain, and Sonic Gems Collection's Yes/No prompt keeps its capitals. Checked in Dolphin.
- **A GameCube memory card copied from another console or emulator no longer reads as damaged.** RiftWii now re-signs the copy the game sees with this Wii's ID, so a tester's shared card no longer says Slot A is damaged in Sonic Gems Collection. The file itself keeps its own header until the game writes. Checked in Dolphin.
- **More GameCube games should play their sound right on a Wii:** Melee, Mario Party 4 and 5, Sonic Mega Collection, Star Fox Assault, Beyond Good and Evil and Paper Mario: The Thousand-Year Door. Their sound program is now patched to read from the Wii's memory; before, only simpler ones like Sunshine's and Luigi's Mansion's were, so Mario Party 4 played noise on a tester's Wii U. Dolphin cannot hear this difference; not tested on a Wii yet.
- **A GameCube game with an unusual disc ID finds its old memory card again.** A card saved under the old ID-only name is copied to its new name at launch when that is not there yet.
- **The freeze with four Wii Remotes (#138) gets diagnostics.** `session.log` now says when the Wii Remote stack starts, which Remotes are connected, and how much memory it has left when one connects or leaves. No behavior change.
- **A pack that changes one disc file too many times is refused, naming the file.** The limit is now 128 changes to one file (it was 512, which could run the Wii out of stack), far more than any real pack makes.

## What to test

1. If you saw the menu freeze with four Wii Remotes (#138): connect four Remotes again. If it freezes, turn the Wii off, start RiftWii and send a problem report, saying it was the four-Remote freeze.
2. Mario Party 4 and Sonic Gems Collection on a Wii: are the colors right, and does the sound play?
3. Melee, Sonic Mega Collection, Mario Party 5, Star Fox Assault, Beyond Good and Evil, and Paper Mario: The Thousand-Year Door on a Wii: does the sound play?
4. A GameCube memory card copied from another console or emulator: does it load instead of reading as damaged?

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
