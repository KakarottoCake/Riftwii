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

New and fixed:

* One emulated NAND for every game's saves (Settings > Games > Wii saves), and it can be on the USB drive now, so two Wiis sharing a drive see the same saves. Optionally the Wii's settings and Miis too
* Manage game on a game's page: hide, rename or delete it
* Press 1 on a game's page for a QR code to report how it runs
* GameCube games can play from Nintendont's or Dolphin's memory card (Better-Nintendont's too)
* Optional countdown that starts your last game (Settings > Home)
* The disc slot light pulses while a game loads
* Wii Menu sounds should load on Wiis where they didn't, and start-up is faster there
* Smoother loading dots on 16:9
* N64 VC games in an emulated NAND start again

To test, please:

1. Emulated NAND on USB: Wii saves = Emulated NAND, Emulated NAND = USB drive (or USB Loader GX's usb1:/nands/01/). Play a USB or SD game, save, and check the save in USB Loader GX
2. Wii Menu sounds if they didn't load for you before
3. Manage game, and a GameCube game on Nintendont's card

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![Settings > Games: the new save rows](docs/screenshots/2611-emulated-nand-settings.png)
![A game's page with Manage game](docs/screenshots/2611-game-page-manage.png)
![Press 1: report how it runs](docs/screenshots/2611-report-how-it-runs.png)
