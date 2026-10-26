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

Korean menus, SD card channels, RiiTag and a faster game Settings page:

* Korean menus on any Wii or Wii U: RiftWii now brings its own small Korean font (korean.ttf beside RiftWii; updating from the menu adds it for you)
* Pressing Settings on a game's banner screen no longer locks the menu for a few seconds: it reuses what Home already read instead of rereading every mod on the SD card
* WiiWare/VC games the Wii Menu moved to the SD card show on Home and start (RiftWii copies them back to the Wii for the session)
* Settings > Online > My RiiTag shows your RiiTag (add riitag_id = <your Discord ID> to settings.txt)
* From 2610-697: VC and WiiWare from an emulated NAND that wouldn't start try again with a generic ticket

To test, please:

1. Set the menu to Korean (Settings > Language) and look around
2. Press Settings on a few games' banner screens with your usual SD card: it should open right away
3. A WiiWare/VC game moved to the SD card with the Wii Menu: start it from RiftWii, then restart RiftWii
4. My RiiTag, if you have one
5. Any VC or WiiWare game from your emulated NAND that didn't start before

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![RiftWii in Korean](docs/screenshots/2610-korean-settings.png)
