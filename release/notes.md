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
**RiftWii {{VERSION}}.** A fix for a black screen when starting RiftWii again after the Homebrew Channel, a game's Settings page that now really opens at once, themes polished for widescreen TVs, and Wii Menu music that sounds more like the Wii's own. Every RiftWii offers this update by itself (Settings > Check for a new version).

![Settings in the Indigo theme on a widescreen TV]({{SCREENSHOTS}}2610-indigo-settings.png)

## What changed

- **No more black screen when starting RiftWii again after the Homebrew Channel.** After a WiiWare game failed to start, a Wii could show a black screen at every RiftWii start until it was switched off. To read a game's files, RiftWii makes a small change inside the Wii's system software; if a session stops before putting it back, the next start now puts it back. RiftWii also no longer trusts notes left in memory by a session that isn't its own. Reading a WiiWare or Virtual Console game's files now writes a log line for each step, and a start that stops early is named in the next `session.log` ("The last start stopped at: ..."). Not yet tested on a real Wii.
- **A game's Settings page opens at once, this time for real.** On a real Wii with lots of mods, 2610-703 still paused for about a second and a half: the page read every mod's options again, even mods for other games. Home now keeps a short summary of each mod, and the banner screen gets the game's saved choices ready while you look at it. In Dolphin with 19 big mods: from 2 seconds to under a tenth of a second.
- **Themes look right on widescreen TVs.** Every built-in theme got a pass at 16:9 and 4:3: button sizes, the banner stripes (no more seam where they meet), and Indigo's gold highlight, now easier to read with white text. Indigo in Get more themes has the same fix.
- **Wii Menu music sounds closer to the Wii's.** With the Wii Menu's sounds on (Settings > Menu), notes now fade in, fade out and mix as on the console, checked against recordings from Dolphin. It isn't identical yet in the upper-middle tones; tell us if something sounds off.

## What to test

1. **Going back to the Homebrew Channel and starting RiftWii again**, a few times, also after a WiiWare or Virtual Console game. If you get a black screen: switch the Wii off, start RiftWii, and send a problem report.
2. **A game's Settings page** with your usual SD card full of mods: it should open right away.
3. **Your theme on a widescreen TV** (and on 4:3 if you can): anything cut off, too big or out of place?
4. **The Wii Menu's music** (Settings > Menu): does it sound like your Wii Menu?

## Known issues

- **GameCube is a trial:** some games may not start or may stop. Send a problem report from Settings > Help when one doesn't.
- **The GameCube memory card in other regions' copies of Resident Evil 2 (PAL, Japan) isn't covered yet.**
- **The channel installer goes black on some Wii U consoles** (#317). RiftWii offers Not now instead of locking you out, but the cause isn't confirmed: `sd:/riftwii/channel.log` in a problem report helps.
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
