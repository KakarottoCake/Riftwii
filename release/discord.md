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

* Wired Switch Pro Controller: plays as a GameCube controller, nothing to put on the SD card
* The menu no longer drops you to the Homebrew Channel when it runs out of memory: it restarts, says so and offers a report
* Covers download around what's on screen first, and wait it out when GameTDB is busy
* Harry Potter and the Goblet of Fire (GameCube): the Wii Remote works
* Just Dance from an RVZ runs again
* Super Mario Sunshine's 16:9/60 fps mod starts again
* Wii Menu font, sounds and music should load again when started from the RiftWii channel (vWii)
* A Wii with a wrong clock should be able to update and download again

To test, please:

1. A wired Switch Pro Controller in the menu and a game or two
2. Goblet of Fire with only a Wii Remote
3. Just Dance from RVZ, Sunshine with its 16:9/60 fps mod
4. A big library: if RiftWii ever restarts with "ran out of memory", send the report it offers

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![RiftWii ran out of memory: the report prompt](docs/screenshots/2611-out-of-memory.png)
![Harry Potter and the Goblet of Fire's first menu, answered by a Wii Remote](docs/screenshots/2611-goblet-of-fire.png)
