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
**RiftWii {{VERSION}}.** Real GameCube memory cards, the Zelda bonus discs, a HOME Menu Reset that keeps a Wii game's mods, and a long list of GameCube game fixes. Every RiftWii offers this update by itself (Settings > Check for a new version).

![A GameCube game's Memory Card page: copy from a memory card in the Wii, or save a copy of the whole card]({{SCREENSHOTS}}2610-587-real-memory-card.png)
![Settings, Online: Download missing covers]({{SCREENSHOTS}}2610-587-missing-covers.png)

## What changed

### GameCube

- **Real GameCube memory cards, on a Wii with GameCube slots.** A game's Saves page copies saves from a card in slot A or B into its SD card, and backs up a whole card to `sd:/riftwii/gc/backups/real/` without writing to it. Memory card > "Real card in slot A" lets the game save to the card itself; if no card answers, RiftWii offers the SD card for that launch only. A Wii U doesn't show these options. Checked in Dolphin; not tested on a real Wii yet.
- **The Zelda bonus discs start their games.** Collector's Edition and the Ocarina of Time / Master Quest disc go back through RiftWii when you pick a game from their menu, which starts it with the usual patches, instead of a black screen. Checked in Dolphin; not tested on a real Wii yet.
- **Sonic Gems Collection's Sonic CD, Sonic R and Sonic the Fighters** start the same way, through RiftWii, instead of staying black. Not tested with the game itself yet.
- **Mario Party 5, 6 and 7 and DDR Mario Mix start** instead of hanging with no picture.
- **Japanese games show their Japanese text on a Western Wii or Wii U**, instead of gaps and stray letters in their system and memory card messages.
- **Memory cards formatted on another console are accepted,** the way a GameCube accepts them, without changing the card.
- **Pikmin 2 from an RVZ starts** instead of staying black.
- **Doshin the Giant (PAL) and NBA Courtside 2002 start,** and Doshin's Reset brings it back.
- **Eternal Darkness no longer freezes on its opening quote.**
- **F-Zero GX keeps a GameCube controller's port** while a Wii Remote is connected too. Checked in Dolphin; not tested on a real Wii yet.
- **Luigi's Mansion no longer goes black after you choose progressive scan.**
- **Pokémon Channel's memory card is RiftWii's again.** The game itself still stops on The Pokémon Company logo; that's a separate problem.
- **480p games are handed over already in 480p**, our best guess at Super Mario Sunshine's wrong colors and speed on a Wii U. Not tested on a Wii U yet.
- **A CISO, GCZ, WIA or RVZ renamed to .iso** is now named in the log with what to do about it, and a CISO gets the CISO DETECTED popup, instead of the game just not showing up.

### Everything else

- **HOME Menu > Reset in a Wii game with mods keeps the mods.** The game starts again through RiftWii with its packs, cheats and choices. Checked in Dolphin; not tested on a real Wii yet.
- **The in-app update shows every step:** one bar for all the downloads and writes, the step's name, and the screen no longer dims while it works.
- **Settings > Online > Download missing covers** asks again for every cover, box and 3D cover Home is missing, including ones GameTDB didn't have last time.
- **Virtual SD card images on a well-used SD card start,** and RiftWii warns before building one on a card whose free space is too scattered.
- **Custom inject channels such as TIME show on Home,** Discross no longer does, and the log says why each channel is left off.
- **Safer popups during an update,** after a tester's crash. The cause isn't certain yet, so the update now logs more to place it.

## What to test

1. **The Zelda bonus discs** (Collector's Edition, Ocarina of Time / Master Quest): pick each game from the disc's menu. It should start, not go black. On a real Wii with 480p on, report any "No Signal" after the rumble screen, with `sd:/riftwii/boot.log` and `sd:/riftwii/gc/trace.bin`.
2. **HOME Menu > Reset in a Wii game with mods:** the game should restart with its mods still on. The interesting games are the ones that used to lose them, like Wii Party. Also try the console's own Reset button.
3. **Real GameCube memory cards** (a Wii with GameCube slots only): on a game's Saves page, copy saves from a card in slot A, then slot B, and load them in the game. Back up a whole card. Set Memory card to "Real card in slot A" and save in the game. With slot A empty, the SD card offer should be for that launch only. Pulling the card out during a copy shouldn't hang.
4. **F-Zero GX** with only a GameCube controller while a Wii Remote is also connected; unplug and replug the controller during a race.
5. **The GameCube fixes:** Mario Party 5, 6 and 7, DDR Mario Mix, a Japanese game's text, a card from another console, Pikmin 2 from an RVZ, Doshin's Reset, Eternal Darkness past its opening, Luigi's Mansion with progressive scan, Sunshine on a Wii U.
6. **The update to this release:** every step with a moving bar, and the screen shouldn't dim.
7. **Settings > Online > Download missing covers.**

## Known issues

- **GameCube is a trial:** some games may not start or may stop. Send a problem report from Settings > Help when one doesn't.
- **Super Mario Sunshine's in-game Reset goes black** on a real Wii (#198).
- **Mario Party 7 on a real Wii** loses its sound at Press START, then stops taking input (#199).
- **Zelda Collector's Edition may show "No Signal"** at 480p on a real Wii (seen once; Dolphin doesn't show it).
- **Pokémon Channel** stops on The Pokémon Company logo.
- **Cars 2 is cursed:** it goes black if RiftWii changes anything in it. RiftWii warns before you play it (and Toy Story 3 and Disney Infinity, which share its engine).
- **Ghostbusters from an RVZ** can still freeze in the hotel: play it from an ISO or WBFS.

Everything new since 2.7.0: [docs/CHANGES_2610.md](https://github.com/KakarottoCake/Riftwii/blob/main/docs/CHANGES_2610.md).

## Download

Grab `riftwii-{{TAG}}.zip` below. `riftwii.dol` is the same build as the zip's `boot.dol` (SHA256 {{DOL_SHA256}}, {{DOL_BYTES}} bytes). `riftwii-update.pack` is what the in-app update reads (the plugins, the themes and the channel installer); you don't need to download it.

If something goes wrong, use Settings > Help > Send a problem report and send the link (you don't need to take out your SD card), or open an issue with `sd:/riftwii/session.log`, `sd:/riftwii/boot.log`, your Wii model, your Menu IOS and your cIOS.

## License and source

RiftWii is free software under the GNU General Public License, version 3 or later, with no warranty. Its source is this repository at the tag `{{TAG}}`; the zip carries `LICENSE.txt`, `NOTICE.md` and `SOURCE.txt`. [NOTICE.md](https://github.com/KakarottoCake/Riftwii/blob/main/NOTICE.md) credits every project RiftWii contains or follows, with each piece's origin and license.
