<!--
The message to the testers' Discord channel for the next release, posted
by the release workflow after the GitHub release is up. Discord's own
markdown: at most 2000 characters once filled in (the workflow counts).

  {{TESTER_ROLE}}  pings the tester role (the DISCORD_TESTER_ROLE_ID variable)
  {{VERSION}}, {{TAG}}, {{RELEASE_URL}}  as in release/notes.md

A line that is only an image, ![what it shows](docs/screenshots/<file>.png),
is sent as an attachment under the message (up to 10, 10 MB in all)
instead of as text.

Only what a player notices goes in: leave out CI and workflows, review
bots (CodeRabbit), tools/, tests, docs and refactors (docs/RELEASING.md).

Rewrite it for every release and delete the line below.
-->
{{TESTER_ROLE}} RiftWii {{VERSION}} is out. {{RELEASE_URL}}

New:

* Emulators as add-ons, with settings you can change per game
* Covers for emulator games (SD card, then libretro, then SteamGridDB if on)
* WiiFlow Lite's emulator plugins work in RiftWii
* Games from every partition of a USB drive (the others show as usb1:/, usb2:/...)
* Quitting a game on a d2x cIOS lands back in RiftWii again
* Leave a frozen menu with HOME on a Classic, Pro or GamePad, or Start on a GameCube pad
* Smoother menu motion on PAL and NTSC, and more free memory

To test, please:

1. If your USB drive has more than one partition: do the games on the others show and start?
2. 4 players: play a game with four Wii Remotes or four GameCube controllers (or a mix). Do all four work?
3. If you use WiiFlow plugins or emulators: do the games show with covers and start?
4. Quit a game with HOME > Wii Menu on a d2x cIOS: back in RiftWii?
5. Retro Rewind online from the disc: does it connect? One tester got error 50330, so a report from anyone who sees it helps a lot.

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![Emulator games with covers](docs/screenshots/2610-emulator-covers.png)
![An emulator game's settings](docs/screenshots/2610-emulator-settings.png)
