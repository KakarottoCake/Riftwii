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
**RiftWii {{VERSION}}.** The Wii Menu's own sounds and music, a GameCube-controller way out of Wii games, fixes for the RiftWii channel on a Wii U, WiiWare fixes, and an SD card you can take out and put back. Every RiftWii offers this update by itself (Settings > Check for a new version).

![The Wii Menu's sounds and music in Settings > Menu]({{SCREENSHOTS}}2610-720-wii-menu-sounds.png)

## What changed

### New

- **The Wii Menu's own sounds and music,** Settings > Menu > Wii Menu sounds and Wii Menu music (both off until you turn them on). RiftWii reads them from your Wii each time it starts and keeps them in memory only; nothing of Nintendo's is in the download. A theme's own sounds and music still come first, and anything missing falls back to RiftWii's own. Checked in Dolphin with Wii Menu 4.3U; not tested on a real Wii or a vWii yet.
- **Leave a Wii game from a GameCube controller:** with the GameCube adapter on, hold L, R, Z and Start for two seconds. It goes back the way the HOME Menu's Wii Menu button does (to RiftWii, or to the Wii Menu if you set that), and waits for a save in progress to finish first. Tested on a Wii U.
- **Home starts on your last game** once its drive is read, unless you've already pressed a button. Settings: Home starts on, Last game or First page.

### The RiftWii channel on a Wii U

- **A channel update that won't run no longer locks you out.** On some Wii U consoles the channel installer went black, and RiftWii wouldn't start until the channel was updated. Now, if the installer never comes back, the next start says so and offers **Not now**. The installer logs before it switches cIOS, only enters a slot it has checked holds a d2x (or asks first when it can't check), and stops with a reason instead of hanging. Not tested on an affected Wii U yet: please try it if your installer went black.
- **A game's own Reset (HOME Menu > Reset) reaches the RiftWii channel** and keeps the GameCube adapter on. Tested on a Wii U.

### WiiWare

- **WiiWare from an emulated NAND on the SD card (vWii):** RiftWii lets go of the SD card before d2x mounts it, the cause of the "mount -101" failures. Not tested on a Wii U yet.
- **WiiWare that quits straight back to RiftWii** (WarioWare: D.I.Y. Showcase): an educated guess at what the game checks at start-up, with log lines that tell whether it was right. Please send a problem report from any WiiWare game that quits.

### Everything else

- **Taking the SD card out and putting it back** no longer leaves RiftWii failing every read: it notices, says so, and reads the card again (a different card too). Games and Settings wait until the card is back. Not tested on a real Wii yet.

## What to test

1. **Wii Menu sounds and music** (Settings > Menu): turn them on, move around Home and Settings, start a game. Wii U (vWii) owners especially, and Wiis from other regions than the USA.
2. **Hold L, R, Z and Start** on a GameCube controller in a Wii game (adapter on), also while the game saves.
3. **Wii U owners whose channel installer went black:** try the channel update again, and send a problem report either way.
4. **WiiWare** from the Wii or an emulated NAND.
5. **Take the SD card out and put it back** on Home.

## Known issues

- **GameCube is a trial:** some games may not start or may stop. Send a problem report from Settings > Help when one doesn't.
- **The GameCube memory card in other regions' copies of Resident Evil 2 (PAL, Japan) isn't covered yet.**
- **The RiftWii channel freezes on some vWii consoles** (#228). Channel 14, which RiftWii offers on a vWii, shows the step it stops at; please send what the screen says.
- **The channel installer goes black on some Wii U consoles** (#317). RiftWii now offers Not now instead of locking you out, but the cause isn't confirmed: `sd:/riftwii/channel.log` in a problem report helps.
- **The Wii U Pro Controller's stick can stop, then drift, in the menu** after opening a game's page (#239).
- **Mario Party 7 on a real Wii** loses its sound at Press START, then stops taking input (#199).
- **Zelda Collector's Edition may show "No Signal"** at 480p on a real Wii (seen once; Dolphin doesn't show it).
- **Pokémon Channel** stops on The Pokémon Company logo (#194).
- **Cars 2 is cursed:** it goes black if RiftWii changes anything in it. RiftWii warns before you play it (and Toy Story 3 and Disney Infinity, which share its engine).
- **Ghostbusters from an RVZ** can still freeze in the hotel: play it from an ISO or WBFS.

Everything new since 2.7.0: [docs/CHANGES_2610.md](https://github.com/KakarottoCake/Riftwii/blob/main/docs/CHANGES_2610.md).

## Download

Grab `riftwii-{{TAG}}.zip` below. `riftwii.dol` is the same build as the zip's `boot.dol` (SHA256 {{DOL_SHA256}}, {{DOL_BYTES}} bytes). `riftwii-update.pack` is what the in-app update reads (the plugins, the themes and the channel installer); you don't need to download it.

If something goes wrong, use Settings > Help > Send a problem report and send the link (you don't need to take out your SD card), or open an issue with `sd:/riftwii/session.log`, `sd:/riftwii/boot.log`, your Wii model, your Menu IOS and your cIOS.

## License and source

RiftWii is free software under the GNU General Public License, version 3 or later, with no warranty. Its source is this repository at the tag `{{TAG}}`; the zip carries `LICENSE.txt`, `NOTICE.md` and `SOURCE.txt`. [NOTICE.md](https://github.com/KakarottoCake/Riftwii/blob/main/NOTICE.md) credits every project RiftWii contains or follows, with each piece's origin and license.
