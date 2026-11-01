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

* Games without a cover get a made-up one with their name, and a paper spine on the Shelf view
* Parental controls with a PIN (Settings > Games > Parental controls): lock or hide games above an age rating
* Copy a game disc to a .wbfs file on USB or SD (Settings > Drives > Copy a game disc, needs a d2x cIOS)
* Themes can have a moving background (try Vaporwave from Get more themes)
* Updating the channel needs no Wii Remote anymore
* Cover fixes, a mod's own cover on its Home tile, and code mods remember how they start

To test, please:

1. Games without a cover in each Home view, the Shelf view especially
2. Parental controls: set a PIN, try a locked game, then Hide
3. Copy a game disc if you have d2x, then play the copy without the disc (not tried on a real Wii yet)

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![Made-up covers on the Shelf view](docs/screenshots/2610-made-up-covers.png)
![Parental controls](docs/screenshots/2610-parental-controls.png)
