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

* Pick a genre on Home: hold B, press Down or Up, let go
* USB drives over 2 TiB start games again
* Japanese GameCube games on a Western console: memory cards no longer read as damaged in more games
* Wii Menu music and sounds keep working after you quit a game
* GameCube games in another top folder of the drive are found
* Box names in the Carousel and Shelf stay on their spines

To test, please:

1. A USB drive bigger than 2 TiB: do games start?
2. A Japanese GameCube game with its memory card: does the save load?
3. Quit a game with HOME > Wii Menu (Wii Menu music on): does the music play back in RiftWii?
4. The genre picker: does it find your games' genres?

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![Picking a genre on Home](docs/screenshots/2610-genre-picker.png)
