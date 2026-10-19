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

GameCube sound, a faster Home, and fixes from your reports:

* Kirby Air Ride plays clean instead of static, and Melee's music is back (Dolphin only; likely still wrong on a Wii)
* Home reads the SD card and USB drive in the background: SD games show first, USB games join them, Settings opens at once
* REX starts again with saves on the SD card turned on (its save stays on the Wii for now)
* A mod in a "Title [ID]" folder shows on Home under the folder's name
* When a GameCube game doesn't show up, session.log says why; .ciso on USB gets the popup too
* Cheats download over https and never change a cheat you turned on; updates come only from RiftWii's own release files
* POWER/HOME wait for a WAD install, the oldest GameCube card backup survives a restore, a failed cover keeps the old one

To test, please:

1. Kirby Air Ride and Melee on a Wii: clean sound? Melee's music?
2. Home with games on both SD and USB: do SD games show first?
3. REX with SD saves, and mods in "Title [ID]" folders

Sending logs (you don't need to take out your SD card anymore): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![A mod kept in a folder named with its own ID shows on Home under the folder name](docs/screenshots/2610-541-mod-folder.png)
