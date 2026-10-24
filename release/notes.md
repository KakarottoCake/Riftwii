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
**RiftWii {{VERSION}}.** Wii channels, WiiWare and your homebrew apps on Home, themes to download, two-disc GameCube games, and Retro Rewind online again. Every RiftWii offers this update by itself (Settings > Check for a new version).

> **Update the RiftWii channel to version 14.** On a vWii, RiftWii asks at start: press Update now. On a Wii, go to Settings > Help > RiftWii channel on the Wii Menu and update it there. If it still shows the old version afterwards, copy `apps/riftwii_channel` from this release's zip to the SD card and update again.

![Home with the Wii's channels and WiiWare]({{SCREENSHOTS}}2610-615-wii-channels.png)
![The Homebrew view on Home]({{SCREENSHOTS}}2610-615-homebrew.png)
![Settings > Theme > Get more themes]({{SCREENSHOTS}}2610-615-get-more-themes.png)
![What's new in this release]({{SCREENSHOTS}}2610-615-whats-new.png)

## What changed

### Home

- **GameCube controls on Home:** L/R turn pages (full clicks), Z opens Settings, and B opens Search. With Home sorted A to Z, hold L and press Left or Right to jump by letter.

- **Wii channels and WiiWare on Home,** with their banners. Settings > Games > Show non-game channels adds Nintendo's own channels too. A channel's settings have its own Game IOS, and Boot picks the Wii's NAND or the emulated one. Settings > Home > Channel background puts a white card behind banners that would look see-through. Checked in Dolphin; WiiWare and Virtual Console still need testing on a real Wii.
- **Your homebrew apps on Home.** Press Y until the view says Homebrew: every app in `sd:/apps` is there, and A starts it. An app keeps RiftWii's IOS and hardware access; started from the RiftWii channel, it exits to the Wii Menu.
- **Settings > Theme > Get more themes** lists the community themes, downloads one into `sd:/riftwii/themes` and deletes one you don't want.
- **Settings > Drives > Games on the SD card: Don't look,** for a setup with every game on a USB drive. Home stops saying the card has no games, and the card still serves packs, themes and saves.
- **The Homebrew Channel no longer shows up as a game,** and wide app icons stay inside their tiles.

### GameCube

- **Two-disc games are listed once, and change discs when the game asks for disc 2.** Put `disc2.iso` next to `game.iso` in the game's folder. Checked in Dolphin; not tested with a real two-disc game yet.
- **Steadier controls,** after a tester found Double Dash slippery with a near freeze: RiftWii no longer writes its diagnostic trace to the SD card while you play. An empty file `sd:/riftwii/gc/test/trace` turns the full trace back on when we ask for one.
- **A game from USB no longer fails with "sdio: card reset failed"** on Wiis whose cIOS doesn't start the SD card itself; RiftWii starts it the way libogc does. Not tested on that Wii yet.
- **The screen goes black at the jump into the game** instead of showing RiftWii's last picture, which Super Mario Sunshine in 480p showed green.
- **The Wii U's GameCube adapter keeps the screen lit** while you play with its controllers, and a stick's center is taken only while it rests, so a stick held at start no longer drifts (thanks to grantendo).
- **A damaged RVZ says what's wrong** in the log (the Zstandard error), instead of only that it couldn't be read.

### Online and USB

- **Retro Rewind goes online again** (error 20911). RiftWii held one of the Wii's three ES connections for the whole game so HOME Menu Reset could restart it; Retro Rewind needs one to sign its login. Now RiftWii takes it only when you press Reset. Reset with the pack still works on a real Wii.
- **Settings > Online > Network Test** checks whether the Wii gets online.
- **A USB game d2x can't read stops with a message** that says which port to try, instead of crashing or sitting at "3%", after trying each cIOS slot.
- **HOME Menu Reset keeps the mods** of a game started from one of its extra Home tiles too.

### Everything else

- **The RiftWii channel shows each step while it starts,** and a step that hangs turns into a screen naming it, for the vWii consoles where the channel freezes (#228). This is channel version 14: RiftWii offers it on a vWii by itself.
- **The last 10 sessions' logs** are kept in `sd:/riftwii/logs`.
- **Real GameCube memory cards:** a progress bar while copying, a memory check first, and a message on a Wii U, which has no card slots.

## What to test

1. **Wii channels and WiiWare:** start a few from Home, try a channel's Game IOS and Boot settings, and Show non-game channels.
2. **Homebrew:** press Y to the Homebrew view and start an app, from the Homebrew Channel and from the RiftWii channel.
3. **Retro Rewind online** (and any other online mod), then HOME Menu > Reset.
4. **A two-disc GameCube game** (Resident Evil 4, Tales of Symphonia, Twin Snakes...): play up to the disc change.
5. **Double Dash and other GameCube games:** do the controls feel like a GameCube's now? Any freeze?
6. **A game from a USB drive on a Wii U:** it should start, or stop with a clear message.
7. **Get more themes,** and Games on the SD card set to Don't look.

## Known issues

- **GameCube is a trial:** some games may not start or may stop. Send a problem report from Settings > Help when one doesn't.
- **The RiftWii channel freezes on some vWii consoles** (#228). Channel 14, which RiftWii offers on a vWii, shows the step it stops at; please send what the screen says.
- **A sideways Wii Remote doesn't work in GameCube games on a vWii** (#135).
- **The Wii U Pro Controller's stick can stop, then drift, in the menu** after opening a game's page (#239).
- **Super Mario Sunshine's in-game Reset goes black** on a real Wii (#198).
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
