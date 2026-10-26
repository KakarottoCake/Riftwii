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

Fixes and polish:

* Starting RiftWii again after the Homebrew Channel no longer gives a black screen after a game failed to start (RiftWii now cleans up what the stopped session left behind)
* A game's Settings page now really opens at once, even with lots of mods on the SD card
* Every theme polished for widescreen TVs: button sizes, no seam in the banner stripes, a more readable gold in Indigo
* The Wii Menu's music sounds closer to the real thing

To test, please:

1. Go back to the Homebrew Channel and start RiftWii again, a few times, also after a WiiWare or VC game. Black screen? Switch the Wii off, start RiftWii and send a report
2. Press Settings on a few games' banner screens with your usual SD card: it should open right away
3. Look at your theme on a widescreen TV: anything cut off or out of place?
4. Turn on the Wii Menu's music (Settings > Menu): does it sound right?

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![Settings in the Indigo theme](docs/screenshots/2610-indigo-settings.png)
