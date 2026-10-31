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
**RiftWii {{VERSION}}.** USB gamepads play as GameCube controllers, and the RiftWii channel fits a 16:9 Wii Menu. RiftWii is now a USB loader and a Riivolution replacement, and says so. Every RiftWii offers this update by itself (Settings > Check for a new version).

![Settings > Controllers: USB input devices and Check USB controllers]({{SCREENSHOTS}}2610-usb-controllers.png)

![The RiftWii channel's banner in a 16:9 Wii Menu]({{SCREENSHOTS}}2610-channel-banner.png)

## What changed

- **USB gamepads play as GameCube controllers**, as in Nintendont: a PS3 or PS4 pad, a generic USB pad or an arcade stick, in GameCube games, in Wii games that read GameCube controllers, and in the menu. Each pad needs a Nintendont `controller.ini` for its make, in the same format, so the configs you already have work: `sd:/riftwii/controllers/VID_PID.ini`, `sd:/controllers/VID_PID.ini` or `sd:/controller.ini`. A pad with no config isn't guessed at; the log names it and says where its config goes. Settings > Controllers has **USB input devices** to turn this on or off, and **Check USB controllers** shows a pad's buttons so you can check its config. Not tried with real pads on a Wii yet.
- **The RiftWii channel fits a 16:9 Wii Menu.** Its banner and grid tile were stretched sideways and sat too low; the name now keeps its shape, sits in the middle of what you see, and the line under it shows above the Wii Menu / Start buttons.
- **No more tearing in the channel's start animation** after you press Start: a frame was sometimes shown half drawn (the flash covering only the top of the screen).
- **USB LOADER** under the name in the channel banner and on the Homebrew Channel icon, instead of MOD LOADER. The Homebrew Channel listing also says mod packs can go in `usb:/riivolution`.
- **To update the channel** (version 15): Settings > Help > RiftWii channel on the Wii Menu opens the installer. On a Wii U RiftWii asks you to by itself.

## What to test

1. **A USB gamepad** with its `controller.ini`: does Check USB controllers show its buttons, and does it play in a GameCube game?
2. **The RiftWii channel** after updating it: does the banner look right on your TV (16:9 or 4:3), and does the start animation play cleanly?

## Known issues

- **USB gamepads:** `MultiIn` 2 to 4 and pads that pack their sticks in their own way aren't supported yet; a pad needs a config.
- **Retro Rewind online from the disc** gave one tester error 50330 on a Wii U. A likely fix is in; a problem report from anyone who still sees it helps.
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
