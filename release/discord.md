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

A small fix for Virtual Console and WiiWare games in an emulated NAND on the SD card:

* Games that stopped with "ES would not run it as its own title" on every cIOS (seen with Super Mario 64 and Cruis'n USA on a Wii U): RiftWii now retries with a generic ticket, as USB Loader GX does
* Check for a new version reads the release info more strictly

To test, please:

1. Start a VC or WiiWare game from your emulated NAND, especially one that wouldn't start in 2610-693

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested. The report now says which ticket the system took.
