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
**RiftWii {{VERSION}}.** Emulators as add-ons with their own settings, covers for emulator games, WiiFlow Lite's emulator plugins, games from every partition of a USB drive, and a few fixes. Every RiftWii offers this update by itself (Settings > Check for a new version).

![Emulator games on Home with their covers]({{SCREENSHOTS}}2610-emulator-covers.png)

## What changed

### Emulators

- **Emulators as add-ons.** An emulator can come with a `riftwii-emulator.ini` that lists its settings and how to start it. On an emulator game's page you can change those settings for that one game, or for all its games. `docs/EMULATORS.md` says how to write one.
- **Covers for emulator games.** RiftWii looks on the SD card first (`sd:/riftwii/covers/emulators/<system>/<game>.png`), then in libretro's thumbnails, then on SteamGridDB if you switch that on in Settings > Online.
- **WiiFlow Lite's emulator plugins work in RiftWii.** If you set up emulators for WiiFlow Lite (`wiiflow/plugins/*.ini`), RiftWii reads those files as emulators, with their games and covers.

### Games and drives

- **Games from every partition of a USB drive.** Before, RiftWii read only one partition. The partition it used before is still `usb:/`; the others are `usb1:/`, `usb2:/` and so on, in their order on the drive. Each game starts from its own partition.
- **Quitting a game on a d2x cIOS lands back in RiftWii again.** d2x was refusing RiftWii's "come back here" request because it was sent too late.

### Menu

- **Leave a frozen menu without a Wii Remote:** HOME on a Classic Controller, Pro Controller or GamePad, or Start on a GameCube controller.
- **Menu motion matches on PAL and NTSC TVs**, and the menu has 152 KiB more free memory.
- **A game-names file that was cut off while downloading** no longer counts as complete; it is read only once it is whole.

## What to test

1. **Games on a second USB partition**, if your drive has more than one: do they show on Home and start? Wii games (WBFS, ISO, RVZ, split files) and GameCube games.
2. **4 players:** play a game with four Wii Remotes, or four GameCube controllers, or a mix. Do all four work all the way through?
3. **Emulators:** if you use WiiFlow Lite plugins or add an emulator, do the games show with covers and start?
4. **Quit a game** with HOME > Wii Menu on a d2x cIOS: do you land back in RiftWii?
5. **Retro Rewind online from the disc:** does it connect? If it shows an error, please send a problem report right after.

## Known issues

- **Retro Rewind online from the disc** gave one tester error 50330 on a Wii U. We're looking into it; a problem report from anyone who sees it helps.
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

![An emulator game's own settings]({{SCREENSHOTS}}2610-emulator-settings.png)

## Download

Grab `riftwii-{{TAG}}.zip` below. `riftwii.dol` is the same build as the zip's `boot.dol` (SHA256 {{DOL_SHA256}}, {{DOL_BYTES}} bytes). `riftwii-update.pack` is what the in-app update reads (the plugins, the themes and the channel installer); you don't need to download it.

If something goes wrong, use Settings > Help > Send a problem report and send the link (you don't need to take out your SD card), or open an issue with `sd:/riftwii/session.log`, `sd:/riftwii/boot.log`, your Wii model, your Menu IOS and your cIOS.

## License and source

RiftWii is free software under the GNU General Public License, version 3 or later, with no warranty. Its source is this repository at the tag `{{TAG}}`; the zip carries `LICENSE.txt`, `NOTICE.md` and `SOURCE.txt`. [NOTICE.md](https://github.com/KakarottoCake/Riftwii/blob/main/NOTICE.md) credits every project RiftWii contains or follows, with each piece's origin and license.
