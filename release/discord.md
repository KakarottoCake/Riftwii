<!--

The message to the testers' Discord channel for the next release, posted

by the release workflow after the GitHub release is up. Discord's own

markdown: at most 2000 characters once filled in (the workflow counts).

-->
{{TESTER_ROLE}} RiftWii {{VERSION}} is out. It replaces 2611-239, which we took down because it gave a black screen or a DSI exception on every real Wii. Sorry about that! {{RELEASE_URL}}

**If you updated to 239 and RiftWii won't start:** copy boot.dol from the zip to sd:/apps/riftwii/ (replace the old one). After that it updates itself as usual.

* **RiftWii starts on a real Wii again.** The crash came from where things ended up in memory, and it never shows in Dolphin. The build now guards against it, and every release gets started on a real Wii before it goes out.
* **Big game libraries use less memory.** Updates and game details download straight to the SD card, so RiftWii no longer runs out of memory and restarts with hundreds of games.
* **Settings has new buttons**, in the Wii Menu's style.

To test: start RiftWii from the Homebrew Channel and from the RiftWii channel. Does the menu come up?

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here.

![Settings](docs/screenshots/2611-239-settings.png)
