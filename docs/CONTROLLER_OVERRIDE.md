# Controller override

The controller override lets another controller stand in for a Wii controller, so a Wii game that wants a Wii Remote, a Nunchuk or a Classic Controller can be played without one. The game sees the Wii controller it expects; RiftWii fills it in from the controller in your hand.

**Today it works with GameCube controllers**: on the Wii U's GameCube controller adapter (or a copy that works like it) and on a Wii's own GameCube ports. A USB controller RiftWii has a mapping for works too, as the GameCube controller it plays as. A Classic Controller standing in for a Wii Remote is planned to use the same setting; this page will grow with it.

This page says what the override does, what it doesn't, and how to help test it. How each game does is in [CONTROLLER_OVERRIDE_GAMES.md](CONTROLLER_OVERRIDE_GAMES.md).

## Turning it on

Open a game's page, choose **Options**, and find **Controllers act as** under Other:

- **Default**: no override. GameCube controllers work only in games made for them (Mario Kart Wii, Super Smash Bros. Brawl and others), as before. If a mod pack you turned on for the game asks for one of the choices below, Default follows it, and the row says so, for example "Default (Classic)". With no such pack it reads "Default (Game's own)".
- **Classic Controller**
- **Wii Remote + Nunchuk**
- **Sideways Wii Remote**
- **Pointing Wii Remote**

Each GameCube controller then plays as that Wii controller: port 1 as Wii Remote 1, port 2 as Wii Remote 2, and so on. A choice you make always wins over what a pack asks for. The setting is kept for each game.

Which one to pick: the one the game itself is played with. The back of the box, or the game's **About** tile, lists the controllers it takes. If a game takes a Classic Controller, that's usually the best fit, because nothing has to be faked. One limit: a GameCube controller has no button for the Classic Controller's ZL or −, so for a game that needs either, pick another controller the game takes, or use a pack that gives them a button.

## The buttons (GameCube controller)

**Classic Controller**

| GameCube | Classic Controller |
| --- | --- |
| Control stick, C-stick | Left stick, right stick |
| A, B, X, Y | a, b, x, y |
| L, R | L, R |
| Z | ZR |
| Start | + |
| D-pad | D-pad |

**Wii Remote + Nunchuk**

| GameCube | Wii Remote + Nunchuk |
| --- | --- |
| Control stick | Nunchuk stick |
| A, B | A, B |
| Y | 2 |
| Z | − |
| Start | + |
| L, R | Nunchuk Z and C (a light pull counts) |
| D-pad | D-pad |
| X | Shake the Wii Remote |
| C-stick | Move the pointer (it shows while the game has a pointer up, and for 2 seconds after the C-stick moves) |

**Sideways Wii Remote** (held like a NES pad, D-pad on the left)

| GameCube | Sideways Wii Remote |
| --- | --- |
| A | 2 |
| B | 1 |
| X | A |
| Y | B |
| Z | − |
| Start | + |
| D-pad, control stick | D-pad, turned the way it reads held sideways (right is right on screen) |
| R | Shake the Wii Remote |
| C-stick | Tilt it like a steering wheel, or move the pointer while the game shows one |

**Pointing Wii Remote** (aimed at the screen)

| GameCube | Pointing Wii Remote |
| --- | --- |
| Control stick | Move the pointer |
| A, B | A, B |
| X | 1 |
| Y | 2 |
| Z | − |
| Start | + |
| D-pad | D-pad |
| C-stick | Twist it (roll) |
| R | Shake it |

In every choice, holding **L + R + Z + Start** together opens the game's HOME Menu on that player's own controller. Where RiftWii can't open the game's HOME Menu (a game without one, or one whose HOME Menu RiftWii doesn't find), it goes back to RiftWii instead. With Settings > **Leaving a game** set to **HOME only**, the combo does nothing.

A real Wii Remote that's turned on for the same player takes over from the GameCube controller, and the GameCube controller gets the player back when the Wii Remote is turned off.

## What it is, and what it isn't

**It is** a stand-in: the game sees a Wii controller that's connected, with its buttons, sticks and a pointer. Menus, walking, jumping, shooting and most button-based play work as they would on the real controller. The shake button gives a short, steady shake, which is what most "shake to do X" moves want (a spin, a punch, a roll).

**It isn't** a full motion controller. A GameCube controller can't swing, twist or point the way a Wii Remote does, so:

- **Swings, throws and gestures** (Wii Sports' tennis and bowling, a pitch in Mario Super Sluggers, The Godfather's jabs) don't happen from the shake button. Each game reads its own motion in its own way; giving those moves a button takes a mod pack made for that game.
- **The Nunchuk can't be shaken** separately from the Wii Remote, so a game that wants both shaken in turn (Donkey Kong: Barrel Blast's racing) needs a pack.
- **The pointer moves from a stick**, so pointing is slower and less exact than a real Wii Remote. Fine for menus and most aiming, harder for fast pointer games.

Packs that bring their own GameCube controls (like quatric's controller patches, or packs made for one game) still work. When a pack you turned on brings its own controller code, it keeps the controls, and the launch screen says "A pack brings its own controls: 'Controllers act as' is off." You don't have to change the setting back. One exception: a pack that uses the game's own GameCube controller code without adding any of its own (quatric's Sonic and the Secret Rings patch, for example) isn't recognised, so for a game with such a pack, leave **Controllers act as** on Default. One more exception, with a GameCube controller adapter or a USB controller RiftWii has a mapping for plugged in (or the GameCube adapter setting On, so one can be plugged in during the game): a pack whose only controller code reads the GameCube ports straight from the hardware (like quatric's) sees just the Wii's own ports, so its controls can't reach the adapter or the USB controller, and RiftWii doesn't turn the override off for it. To play such a game on the adapter or the USB controller, pick a form in **Controllers act as** (or use a pack that asks for one): the override then plays it with that form's controls, not the pack's, and the launch screen says nothing. On Default with no form asked for, the override stays off.

Games that already support GameCube controllers don't need the override at all: leave them on Default. Default still follows a pack you turned on that asks for a form, so to play such a game with its own GameCube controls, turn that pack off.

## A few things RiftWii does for you

- The screen doesn't dim while you play: in most games, moving a GameCube controller counts as using the Wii Remote for the Wii's Screen Burn-In Reduction (the games where it doesn't yet are in the games list).
- Most games that ask the Wii Remote for its battery are told it's full; a few still show a low-battery message (they're in the games list).
- If a game has no GameCube controller code of its own, RiftWii reads the adapter for it. On a Wii's own GameCube ports this needs the game to have that code (the ports can only be read through it); the log says so when it's missing.

## Helping test

We need to know how each game does. To report one:

1. Use the latest RiftWii (or the test build linked in the issue you're answering).
2. Pick the game's controller in **Controllers act as**, with every mod off unless you're testing a pack.
3. Play for a while: the menus, a level or match, any move the game teaches early on.
4. Open a report with the **Controller override: a game report** form on the project's Issues page. It asks for the game, its ID (on the game's page), the choice you used, how far you got and what didn't work.
5. If you can, attach `sd:/riftwii/boot.log` from that launch; it names what RiftWii found in the game.

Reports go into [CONTROLLER_OVERRIDE_GAMES.md](CONTROLLER_OVERRIDE_GAMES.md).
