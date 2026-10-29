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
**RiftWii {{VERSION}}.** Pick a genre on Home, and fixes for USB drives over 2 TiB, Japanese GameCube games' memory cards, and the Wii Menu music after quitting a game. Every RiftWii offers this update by itself (Settings > Check for a new version).

![Picking a genre on Home]({{SCREENSHOTS}}2610-genre-picker.png)

## What changed

- **Pick a genre on Home.** Hold B (L on a GameCube controller) and press Down or Up: a list of your games' genres opens. Let go of B and Home shows only that genre's games; pick Every genre to see them all again. The genres come from GameTDB's game details.
- **USB drives over 2 TiB start games again.** A drive that big reports its size in a way RiftWii took for "the drive didn't start", so every USB launch stopped with "d2x can't read the USB drive in this port" (#413). Not tried on such a drive yet.
- **Japanese GameCube games on a Western Wii or Wii U** see a Japanese console in more games. Super Robot Taisen GC said its memory card save was damaged and asked to format it (#391). Not tried with that game yet; if a Japanese game still complains, a problem report now shows why.
- **The Wii Menu music and sounds keep working after you quit a game.** When RiftWii came back through its channel, they were replaced by RiftWii's own until it was started again from the Homebrew Channel. Settings > Menu font reads the Wii Menu's font the same new way.
- **GameCube games kept in another folder** at the top of the SD card or USB drive (for example `game1/<Title [ID]>/game.iso`, as Nintendont names them) are found and listed.
- **Box names in the Carousel and Shelf** stay on their spines: a far box's name is shortened and made smaller, and a spine seen almost edge-on gets none.

## What to test

1. **A USB drive bigger than 2 TiB:** do games start?
2. **A Japanese GameCube game** on a Western console with its memory card: does the save load?
3. **Quit a game with HOME > Wii Menu** with Wii Menu music on: does the music play when you land back in RiftWii?
4. **The genre picker** on Home: does it find your games' genres?

## Known issues

- **Retro Rewind online from the disc** gave one tester error 50330 on a Wii U. The last release has a likely fix that couldn't be checked in Dolphin; a problem report from anyone who still sees it helps.
- **WADs:** Korean and vWii-only WADs aren't read yet. WADs need an SD card for the emulated NAND.
- **Emulators and other USB partitions:** some emulators may not understand `usb1:/` or `usb2:/` paths for games on a second partition.
- **GameCube is a trial:** some games may not start or may stop. Send a problem report from Settings > Help when one doesn't.
- **The channel installer goes black on some Wii U consoles** (#317). RiftWii offers Not now instead of locking you out, but the cause isn't confirmed: `sd:/riftwii/channel.log` in a problem report helps.
- **The Wii U Pro Controller's stick can stop, then drift, in the menu** after opening a game's page (#239).
- **Mario Party 7 on a real Wii** loses its sound at Press START, then stops taking input (#199).
- **The GameCube memory card in other regions' copies of Resident Evil 2 (PAL, Japan) isn't covered yet.**
- **Zelda Collector's Edition may show "No Signal"** at 480p on a real Wii (seen once; Dolphin doesn't show it).
- **Pokémon Channel** stops on The Pokémon Company logo (#194).
- **Cars 2 is cursed:** it goes black if RiftWii changes anything in it. RiftWii warns before you play it (and Toy Story 3 and Disney Infinity, which share its engine).
- **Ghostbusters from an RVZ** can still freeze in the hotel: play it from an ISO or WBFS.

## Download

Grab `riftwii-{{TAG}}.zip` below. `riftwii.dol` is the same build as the zip's `boot.dol` (SHA256 {{DOL_SHA256}}, {{DOL_BYTES}} bytes). `riftwii-update.pack` is what the in-app update reads (the plugins, the themes and the channel installer); you don't need to download it.

If something goes wrong, use Settings > Help > Send a problem report and send the link (you don't need to take out your SD card), or open an issue with `sd:/riftwii/session.log`, `sd:/riftwii/boot.log`, your Wii model, your Menu IOS and your cIOS.

## License and source

RiftWii is free software under the GNU General Public License, version 3 or later, with no warranty. Its source is this repository at the tag `{{TAG}}`; the zip carries `LICENSE.txt`, `NOTICE.md` and `SOURCE.txt`. [NOTICE.md](https://github.com/KakarottoCake/Riftwii/blob/main/NOTICE.md) credits every project RiftWii contains or follows, with each piece's origin and license.
