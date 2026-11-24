<!--

The message to the testers' Discord channel for the next release, posted

by the release workflow after the GitHub release is up. Discord's own

markdown: at most 2000 characters once filled in (the workflow counts).

-->
{{TESTER_ROLE}} RiftWii {{VERSION}} is out. {{RELEASE_URL}}

New, from a tester's idea:

* **Mod tiles can use their GameTDB entry** (like Newer Super Mario Bros. Wii, SMNP03). Pick it from a list, or type its ID, when you make a mod's Home tile (or later in its Options > GameTDB entry). The tile gets the mod's own name, cover and About page.

And two fixes from your reports:

* **USA games on a Japanese Wii** don't crash anymore (Super Mario Galaxy 2's green screen). A game from another region now gets a language it has when it's set to follow the Wii's language (not WiiWare or Virtual Console yet).
* **The Wii Remote pointer** works again after the Remote times out and wakes up.

Everything from 2611-233 (the Rift Store and more) is in it too.

Tried in Dolphin only so far. To test, please:

1. Make a Home tile for a mod with a GameTDB entry (Newer, Retro Rewind...) and pick the entry: right name, cover and About page?
2. A game from another region (for example USA on a Japanese Wii): does it start, in English?
3. Let the Wii Remote time out in the menu, then press A: does the pointer go where you aim?

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![What's new](docs/screenshots/2611-236-whats-new.png)
![GameTDB entry](docs/screenshots/2611-236-gametdb-entry.png)
