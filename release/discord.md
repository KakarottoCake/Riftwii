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

* Your own menu music: a song, a playlist or a shuffled folder (Settings > Music file)
* Press 1 and 2 together on Home to change the view
* Press 1 on a cover or shelf box to see it up close
* Shelf boxes look like real Wii cases, with full-width spines and full covers for every game
* Hold B on the search button to start a random game
* Covers for homebrew, WiiWare and VC games; custom games like Newer show under Mods
* USB keyboard, mouse, pen tablets and uDraw in the menu
* Quitting a game lands in RiftWii more often (GameCube, N64 VC, RE4)
* Dragon Quest X from its USB drive (needs sd:/riftwii/wfskey.bin)

To test, please:

1. Press 1 and 2 together on Home a few times: does it go through the views?
2. Press 1 on a cover and on a shelf box: does it zoom up and go back with B?
3. Leave the shelf open while the boxes download: anything freeze or restart?
4. Pick your own music in Settings > Music file
5. If you have a USB keyboard or mouse, try it in the menu
6. Quit a GameCube or N64 VC game: do you land back in RiftWii?

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![The shelf with Wii cases](docs/screenshots/2610-shelf-cases.png)
![A cover up close](docs/screenshots/2610-cover-zoom.png)
![What's new](docs/screenshots/2610-whatsnew-music.png)
