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
**RiftWii {{VERSION}}.** Your own menu music, a closer look at every cover, shelf boxes that look like real Wii cases, covers for homebrew, WiiWare and Virtual Console games, a USB keyboard and mouse in the menu, and more ways back to RiftWii after a game. Every RiftWii offers this update by itself (Settings > Check for a new version).

![The shelf with Wii cases and full-width spines on a widescreen TV]({{SCREENSHOTS}}2610-shelf-cases.png)

## What changed

### Home

- **Press 1 and 2 together to change the view** (covers, 3D covers, carousel, shelf, channels, names) without opening Settings. Pressed alone, 1 and 2 still do their own jobs.
- **See a cover up close.** Point at a cover in the Covers view, or a box on the shelf or carousel, and press 1 or 2: it zooms up in the middle of the screen. 1, 2, A or B puts it back. On a game's page, A on the box shows it up close too, and Down turns it over to its back.
- **The shelf's boxes look like real Wii cases**, with the colored plastic showing round the cover, in the color each game's case really has. You can pick your own colors in `sd:/riftwii/case_colors.txt`.
- **Spines are no longer squeezed**, and full covers (front, spine and back) are now downloaded for every game, not just the ones near you on the shelf. Small 2D covers stay square where they stand in for a box, and covers fill the box's front right to the corners.
- **Hold B on the search button for about a second** to start a random game from the ones shown.
- **Covers for more games:** homebrew apps get their GameTDB covers (found by their name), and WiiWare and Virtual Console games now get their covers, shelf boxes and 3D covers too.
- **Custom games** that GameTDB lists (Newer Super Mario Bros. Wii, Wiimm's Mario Kart Fun and others) now show under Mods, with their own banners.
- **A game's age rating** on its About page uses the rating board's own mark (ESRB, PEGI, CERO, GRAC, GSRR).
- **The Wii Menu font is crisp** on a widescreen TV (Settings > Menu font).

### Menu music and controls

- **Your own menu music:** in Settings > Music file, pick a song (`.ogg` or `.mp3`), an `.m3u` playlist, or a whole folder to play shuffled, from the SD card or the USB drive.
- **A USB keyboard, a USB mouse, USB pen tablets and the uDraw GameTablet** work in the menu as player 1's controls. The keyboard types into search and tile names, and the mouse points like a Wii Remote. Not yet tested on a real Wii (Dolphin can't plug these in).
- **Naming a Home tile:** Shift also types symbols, so names like "Project+" or "Kamen Rider: Super Climax Heroes" can be typed.

### Games

- **Quitting a game lands in RiftWii more often.** A GameCube game's held HOME tries the RiftWii channel first. Resident Evil 4, older LEGO games and Ghostbusters keep their way back. N64 Virtual Console games now leave with the GameCube controller's L+R+Z+Start held for 2 seconds, like other games. The setting is now called **Quitting a game** (Settings > Games), so it's easier to find.
- **WiiWare and Virtual Console:** when the Wii refuses a channel's own ticket, RiftWii tries again with a copy that works on any console, which helps games copied from another Wii.
- **RVZ and GameCube games are found wherever you put them:** a Wii `.rvz` in `wbfs/`, or a GameCube `.iso`, `.gcm` or `.rvz` loose in `games/` or `wbfs/`, no folder of its own needed.
- **Dragon Quest X from its USB drive:** with this Wii's WFS key at `sd:/riftwii/wfskey.bin`, RiftWii reads the game's WFS drive and starts Dragon Quest X like the Wii Menu does. Tried in Dolphin, not yet on a real Wii.
- **HOME Menu:** without Priiloader, its third button is now Restart. On a Wii U it goes back to the Wii U.
- **The updater** says the real reason when a secure connection is refused, instead of always blaming the Wii's clock.

## What to test

1. **Press 1 and 2 together on Home** a few times: does it go through the views and remember the last one?
2. **Press 1 on a cover** in the Covers view and on a shelf box: does it zoom up and go back with B?
3. **Look at the shelf** with downloads on: do the cases' colors and spines look right? Does anything freeze or restart while the boxes download?
4. **Pick your own menu music** (Settings > Music file): a song, a playlist and a folder.
5. **Plug in a USB keyboard or mouse** if you have one: can you search, type a name and point?
6. **Quit a game** with HOME or L+R+Z+Start, especially a GameCube or N64 Virtual Console game: do you land back in RiftWii?

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

![A cover zoomed up close in the Covers view]({{SCREENSHOTS}}2610-cover-zoom.png)

## Download

Grab `riftwii-{{TAG}}.zip` below. `riftwii.dol` is the same build as the zip's `boot.dol` (SHA256 {{DOL_SHA256}}, {{DOL_BYTES}} bytes). `riftwii-update.pack` is what the in-app update reads (the plugins, the themes and the channel installer); you don't need to download it.

If something goes wrong, use Settings > Help > Send a problem report and send the link (you don't need to take out your SD card), or open an issue with `sd:/riftwii/session.log`, `sd:/riftwii/boot.log`, your Wii model, your Menu IOS and your cIOS.

## License and source

RiftWii is free software under the GNU General Public License, version 3 or later, with no warranty. Its source is this repository at the tag `{{TAG}}`; the zip carries `LICENSE.txt`, `NOTICE.md` and `SOURCE.txt`. [NOTICE.md](https://github.com/KakarottoCake/Riftwii/blob/main/NOTICE.md) credits every project RiftWii contains or follows, with each piece's origin and license.
