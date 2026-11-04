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

New and fixed:

* The whole menu in the Wii's own style: glossy buttons, a springy hover, page bands, a glint on Play, in every theme
* Smoother waits and progress bars, buttons that dip when pressed
* Message boxes light the safe choice
* Home's Show has Wii games and GameCube games views
* Phone-style PIN keypad for parental controls
* Goblet of Fire (GameCube) no longer runs out of memory
* Need for Speed Carbon (GameCube): the Wii Remote should keep working past the second "press a button" screen
* Retro Rewind no longer asks about missing Mission files

To test, please:

1. The new look in your theme: Settings, a game page, the HOME Menu. Anything cut off on your TV?
2. Goblet of Fire with menu music on
3. NFS Carbon with a Wii Remote
4. Show > Wii games / GameCube games, and the PIN keypad

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![Settings, before and after](docs/screenshots/2611-wii-look-settings.gif)
![The game page, before and after](docs/screenshots/2611-wii-look-game.gif)
![Settings in six themes](docs/screenshots/2611-wii-look-themes.png)
