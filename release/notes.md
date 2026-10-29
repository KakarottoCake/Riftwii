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
**RiftWii {{VERSION}}.** An easier Home (a Layout button, a Show chooser, help when a list is empty), WiiWare and Virtual Console straight from WAD files, pictures for emulator games, banner music and disc art on game pages, and clearer messages when something goes wrong. Every RiftWii offers this update by itself (Settings > Check for a new version).

![The Layout button's picker on Home]({{SCREENSHOTS}}2610-layout-picker.png)

## What changed

### Home

- **A Layout button.** The round button in the top-left corner (on the bar in Channels) opens a picker with the six layouts: Covers, 3D covers, Carousel, Shelf, Channels and Names. Before, the layout changed only in Settings or with 1 and 2 together, which still works.
- **A Show chooser.** The bottom-left button now opens a chooser with Games with mods, All games, Homebrew, Emulators, Recently played and Favourites, each with how many games it has. The 1 button still steps through them.
- **No more empty Home.** When the list you chose has nothing in it, Home says why and offers "Show all games".
- **Settings > Home > Games from is no longer hidden.** When it is set to one drive, Home says so beside the list's name, and Search now finds games on every drive.

### Games

- **WiiWare and Virtual Console from WAD files, with no install step.** Put the `.wad` files in a `wiiware` folder on the SD card or a USB drive. They show on Home with their name and banner. The first Play unpacks the game into the SD card's emulated NAND (`sd:/nands/01`); later Plays start at once. Saves stay there, and are kept if you remove the WAD. This uses d2x's NAND emulation and hasn't been tried on a real Wii yet.
- **Retro Rewind online (error 50330):** RiftWii now fully shuts down the menu's internet connection before every game starts. A tester got error 50330 when the menu had been online first; this is the most likely cause, but it couldn't be checked in Dolphin.
- **Asks before playing with missing mod files.** If some files of the mods you switched on are missing, RiftWii says which changes will be left out, with Back, Review files and Play anyway.
- **Clearer messages:** a GameCube memory card that can't be read no longer says "No memory card yet"; a mod error shows in full instead of cut off; the missing-cIOS message names the right drive (SD or USB); a USB partition that can't be read is named in Settings > Drives > USB scan details.

### Game pages

- **Banner music and disc art.** Two new switches in Settings, both off at first: the game's banner music plays when its page opens from any layout (Channels already did), and GameTDB's picture of the disc shows under the box.
- **A Save location page.** The Saves tile opens a page that shows where the game's new saves go and lets you choose. Before, each press switched it. Saves already made never move.
- **Use default options for this game**, the last row of a game's Options, puts its picture, language, controller and launch choices back to following Settings.
- **About lists the controllers in words** ("Needed" and "Also works with"), for players using the D-pad.
- **WiiWare and VC games without box art** show their channel icon in a card of its own shape.

### Emulators

- **Pictures for emulator games in the Channels view**, instead of a plain tile: your own PNG first, then libretro's title screens, SteamGridDB (when on), or part of the cover.
- **Pick a picture yourself when online:** on an emulator game's page, Picture shows the pictures found online, six at a time, and saves the one you pick to the SD card.

### Small things

- The menu's words are plainer: Settings paths in full, "Downloads" for the online switches, and hints that say what A does.
- The icon on the Layout button sits in the middle of the button.

## What to test

1. **WiiWare or VC from a WAD:** put one in `sd:/wiiware` (or a USB drive's `wiiware` folder). Does it show on Home, unpack on the first Play, start, save, and start again without unpacking?
2. **Retro Rewind online from the disc**, after the menu has been online (covers or names downloading): does it connect now? If it shows an error, send a problem report right after.
3. **The Layout and Show buttons** on Home: do they do what you expect? Is anything hard to find?
4. **Mods with a file missing:** do you get the question, and does Play anyway work?
5. **Banner music and disc art** (Settings): do they show and play on your games?

## Known issues

- **Retro Rewind online from the disc** gave one tester error 50330 on a Wii U. This release has a likely fix that couldn't be checked in Dolphin; a problem report from anyone who still sees it helps.
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

![Mario Kart Wii's page with its disc art]({{SCREENSHOTS}}2610-disc-art.png)

## Download

Grab `riftwii-{{TAG}}.zip` below. `riftwii.dol` is the same build as the zip's `boot.dol` (SHA256 {{DOL_SHA256}}, {{DOL_BYTES}} bytes). `riftwii-update.pack` is what the in-app update reads (the plugins, the themes and the channel installer); you don't need to download it.

If something goes wrong, use Settings > Help > Send a problem report and send the link (you don't need to take out your SD card), or open an issue with `sd:/riftwii/session.log`, `sd:/riftwii/boot.log`, your Wii model, your Menu IOS and your cIOS.

## License and source

RiftWii is free software under the GNU General Public License, version 3 or later, with no warranty. Its source is this repository at the tag `{{TAG}}`; the zip carries `LICENSE.txt`, `NOTICE.md` and `SOURCE.txt`. [NOTICE.md](https://github.com/KakarottoCake/Riftwii/blob/main/NOTICE.md) credits every project RiftWii contains or follows, with each piece's origin and license.
