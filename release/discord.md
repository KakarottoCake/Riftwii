<!--
The message to the testers' Discord channel for the next release, posted
by the release workflow after the GitHub release is up. Discord's own
markdown: at most 2000 characters once filled in (the workflow counts).
-->
{{TESTER_ROLE}} RiftWii {{VERSION}} is out. {{RELEASE_URL}}

Lots of your requests in this one:

* Home shows your games at once: no more rescanning at every start
* Games start behind their box; press any button for the log (Settings > Menu > Launch screen)
* Settings > Games > Manage emulated NAND: delete installed channels, copy saves and Miis to and from the Wii, remove its Wii settings
* Your own covers, 3D covers and boxes for homebrew, named after the app (ScummVM.png), and homebrew favorites
* Settings > Controllers > Import from Nintendont copies your controller.ini
* Type your SteamGridDB key in Settings > Online; pictures are no longer downloaded again every start
* Covers view shows the pages either side, GameCube progress bar moves, disc close-up on the game page

Not tried on a real Wii yet: copying saves or Miis into the Wii's own memory.

To test, please:

1. Start RiftWii twice: are your games there at once the second time?
2. Start a game: box shown, and a button shows the log?
3. Manage emulated NAND on a real Wii, especially copying a save or the Miis
4. Import your Nintendont controller.ini, then play with that pad
5. SteamGridDB: start twice, does your allowance stay put?

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![A game starting behind its box](docs/screenshots/2611-200-launch-box.png)
![Manage emulated NAND](docs/screenshots/2611-200-emulated-nand.png)
![Your own homebrew cover](docs/screenshots/2611-200-homebrew-cover.png)
