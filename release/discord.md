<!--
The message to the testers' Discord channel for the next release, posted
by the release workflow after the GitHub release is up. Discord's own
markdown: at most 2000 characters once filled in (the workflow counts).

  {{TESTER_ROLE}}  pings the tester role (the DISCORD_TESTER_ROLE_ID variable)
  {{VERSION}}, {{TAG}}, {{RELEASE_URL}}  as in release/notes.md

A line that is only an image, ![what it shows](docs/screenshots/<file>.png),
is sent as an attachment under the message (up to 10, 10 MB in all)
instead of as text.

Rewrite it for every release and delete the line below.
-->
{{TESTER_ROLE}} RiftWii {{VERSION}} is out. {{RELEASE_URL}}

Fixes for 2610-489:

* REX starts again with saves on the SD card turned on (its save stays on the Wii for now)
* A mod in a folder named with its own ID (like Mario Kart Double Dash Plus [GM4E05]) shows on Home under that folder's name
* When a GameCube game doesn't show up, session.log now says why (a .gcz, .wia, NKit image, an archive, or a file the drive can't read)
* .ciso files on a USB drive get the conversion popup too

To test, please:

1. REX with saves on the SD card: does it start?
2. Mods in a "Title [ID]" folder: does Home show the folder's name?
3. GameCube games that didn't show up: what does session.log say?

Sending logs (you don't need to take out your SD card anymore): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.
