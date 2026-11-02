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

* 007 Agent Under Fire and Everything or Nothing (GameCube) start now
* The Wii Remote works in NightFire's missions and Everything or Nothing's menus
* USB controllers work without a file on the SD card: about 40 pads built in, DualShock 3 and 4, DualSense, Logitech, 8BitDo, Mayflash and more

To test, please:

1. The three 007 GameCube games with a Wii Remote: start a mission, use the menus and the pause menu
2. Everything or Nothing should go back to RiftWii for a moment, then start the main game by itself
3. A USB pad with no file on the SD card, a DualShock 3 or 4 especially: does it work in a GameCube game?

Known: Everything or Nothing's THX and MGM logo movies have the wrong colours.

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![007 Agent Under Fire](docs/screenshots/2610-007-agent-under-fire.png)
![007 Everything or Nothing](docs/screenshots/2610-007-everything-or-nothing.png)
![007 NightFire](docs/screenshots/2610-007-nightfire.png)
