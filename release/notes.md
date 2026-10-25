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
**RiftWii {{VERSION}}.** Korean menus on any Wii, WiiWare and Virtual Console games the Wii Menu moved to the SD card, your RiiTag in Settings, and a game's Settings page that opens at once. Every RiftWii offers this update by itself (Settings > Check for a new version).

![RiftWii's Settings in Korean]({{SCREENSHOTS}}2610-korean-settings.png)

## What changed

- **Korean menus on any Wii or Wii U.** Until now Korean text only showed with a Korean Wii's own menu font. RiftWii now brings its own: a small, cut-down copy of Noto Sans KR (free license, credited in Settings > Credits), in `apps/riftwii/korean.ttf` beside RiftWii and read from the SD card only when Korean text is on screen. Updating from the menu puts it in place by itself; if you copy RiftWii by hand, copy `korean.ttf` and its license too. Without the file, the menu shows in English and the log says which file to copy.
- **A game's Settings page opens at once.** Pressing Settings on a game's banner screen used to lock the menu for a few seconds (the banner and its music kept playing) while RiftWii read every mod pack on the SD card again. It now uses what Home already read: in a test with 48 packs and a card as slow as a real SD card, from almost 10 seconds to a fifth of a second. Problem reports now say how long the page took to open.
- **WiiWare and Virtual Console games the Wii Menu moved to the SD card** (Data Management > Channels > Move) show on Home again and start. As the Wii Menu does, RiftWii copies the game back to the Wii for the session (you need that much free space on the Wii) and removes the copy the next time RiftWii starts. A card from another Wii works with that Wii's BootMii `keys.bin` at `sd:/keys.bin`. Checked in Dolphin with the real Wii Menu doing the move; not yet on a real console. Thanks to quatric.
- **See your RiiTag on the TV:** put your Discord ID in `sd:/riftwii/settings.txt` as `riitag_id = <your ID>`, and Settings > Online > My RiiTag downloads your tag and shows it (the Guide says how to find the ID). Thanks to quatric.
- **45 KiB more free memory** for the menu and games, with nothing you can see. Thanks to grantendo.
- **From 2610-697:** Virtual Console and WiiWare games in an emulated NAND that the Wii refused to start now try again with a generic ticket. If you had one that wouldn't start, please try it again.

## What to test

1. **Korean:** set the menu to Korean (Settings > Language) and look around Home, Settings and a game's page. Korean Wii owners: also try it with Settings > Menu font set to the Wii Menu's.
2. **A game's Settings page:** press Settings on a few games' banner screens, with your usual SD card full of mods. It should open right away. A problem report afterwards shows the time it took.
3. **A WiiWare or VC game moved to the SD card** with the Wii Menu: does it show on Home and start? Start RiftWii again afterwards (it removes the copy). A problem report shows the copying and the cleanup.
4. **My RiiTag** (Settings > Online), if you have a RiiTag.
5. **VC and WiiWare from an emulated NAND**, if one didn't start before 2610-697.

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
