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

* USB gamepads (PS3, PS4, generic pads, arcade sticks) play as GameCube controllers, with Nintendont's controller.ini configs: Settings > Controllers > USB input devices, and Check USB controllers to see a pad's buttons
* The RiftWii channel fits a 16:9 Wii Menu: no more stretched banner, and it sits above the buttons
* No more tearing in the channel's start animation
* USB LOADER under the name, on the channel and the Homebrew Channel icon

To test, please:

1. A USB gamepad with its controller.ini: does Check USB controllers show its buttons, and does it play in a GameCube game?
2. Update the channel (Settings > Help > RiftWii channel on the Wii Menu): does the banner look right on your TV, and does the start animation play cleanly?

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![USB controller settings](docs/screenshots/2610-usb-controllers.png)
![The channel banner on 16:9](docs/screenshots/2610-channel-banner.png)
