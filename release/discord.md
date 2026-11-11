<!--
The message to the testers' Discord channel for the next release, posted
by the release workflow after the GitHub release is up. Discord's own
markdown: at most 2000 characters once filled in (the workflow counts).

  {{TESTER_ROLE}} RiftWii {{VERSION}} is out. {{RELEASE_URL}}

CTGP-R, and a few fixes:

* CTGP-R reads its cup layouts from your own install, loads only when you start it, and installs that updated themselves work again
* CTGP-R's Mario Kart Channel button brings back the RiftWii that started it, from any folder
* Sonic Unleashed sees the GameCube adapter on a Wii U
* Brawl with a file-swap pack loads a match
* New: Settings > Drives > Game folders, and named memory cards for GameCube games
* Wii Remote no longer stuck on a Classic Controller or Nunchuk (Eternal Darkness), DNS servers from the Wii's settings, xdelta patches that only mention compression

Not tried on a real Wii yet: the Rankings button, SD or disc launch of CTGP-R, Game folders on USB, named cards, the Classic Controller fix and the DNS lookup.

To test, please:

1. CTGP-R from USB, SD and disc, with an updated install
2. The channel button, with RiftWii in another folder
3. Sonic Unleashed with the adapter on a Wii U
4. Brawl with a file-swap pack: a match and Reset
5. A named card in a GameCube game, then Game folders on a USB drive

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![What's new](docs/screenshots/2611-162-whats-new.png)
![Game folders](docs/screenshots/2611-162-game-folders.png)
![Named memory cards](docs/screenshots/2611-162-named-cards.png)
