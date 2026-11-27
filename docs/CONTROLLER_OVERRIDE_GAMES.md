# Controller override: how each game does

How Wii games play with the controller override (**Controllers act as**, [CONTROLLER_OVERRIDE.md](CONTROLLER_OVERRIDE.md)). Every result here is with GameCube controllers only, no Wii Remote, and no mod pack unless the notes say so. When the override covers other controllers, their results get a column of their own.

To add a game or correct one, open a **Controller override: a game report** on the project's Issues page (how: [Helping test](CONTROLLER_OVERRIDE.md#helping-test)).

**What the status means** (the GameCube controller column):

- **Works**: plays from start to finish as far as we got, with the choice listed.
- **Works, some moves need a pack**: the game plays, but some motion moves (swings, throws, gestures) don't come from the shake button. A mod pack made for the game can give them buttons.
- **Partly**: gets into the game, but something gets in the way. The notes say what.
- **Doesn't work yet**: can't get past the start.
- **Not available**: RiftWii keeps its controller code out of this game for now, so the override can't run in it.

**Checked on**: *Console* is a Wii U's Wii mode with the GameCube controller adapter; *Dolphin* is the Dolphin emulator with its emulated adapter. "A fix is ready" means a fix that's written and tested in Dolphin, coming in a later update.

| Game | ID | Controllers act as | GameCube controller | Notes | Checked on |
| --- | --- | --- | --- | --- | --- |
| Batman: The Brave and the Bold – The Videogame | S3BEWR | Wii Remote + Nunchuk | Works | X (shake) gives the heavy attack | Dolphin |
| Bully: Scholarship Edition | RB7E54 | Wii Remote + Nunchuk | Works | The stick walks, the D-pad turns the camera, X fights. The class minigames (shake and gesture ones) aren't checked yet | Dolphin |
| Donkey Kong: Barrel Blast | RDKE01 | Wii Remote + Nunchuk | Works, some moves need a pack | Menus work. Racing wants the Wii Remote and the Nunchuk shaken in turn, which the shake button can't do. It shows "P1's batteries are running low" before every race | Dolphin |
| Donkey Kong Country Returns | SF8E01 | Wii Remote + Nunchuk | Works | X at "shake to start", X while running rolls, X while standing pounds | Dolphin |
| Elebits | RELEA4 | Wii Remote + Nunchuk | Works | The C-stick turns the view and B fires the capture beam. Holding the view level is hard with a stick | Dolphin |
| Excite Truck | REXE01 | Sideways Wii Remote | Works | The C-stick steers | Console |
| The Godfather: Blackhand Edition | RGFE69 | Wii Remote + Nunchuk | Works, some moves need a pack | Walking, the camera and the lock-on (L) work. The jabs want a forward flick of each hand, which the shake doesn't give | Dolphin |
| Kamen Rider: Dragon Knight | SKREG9 | Classic Controller | Works | | Console |
| LEGO Star Wars III: The Clone Wars | SC4E64 | Wii Remote + Nunchuk | Works | X swings the lightsaber | Dolphin |
| LEGO Star Wars: The Complete Saga | RLGE64 | Wii Remote + Nunchuk | Works | | Console |
| Mario Party 8 | RM8E01 | Pointing Wii Remote | Works, some moves need a pack | Menus and pointer minigames play. Minigames played by swinging or punching (Punch-a-Bunch) need a pack | Dolphin |
| Mario Sports Mix | RMKE01 | Wii Remote + Nunchuk | Works | The menus use the pointer only (the C-stick), not the D-pad. In a match the stick moves your player | Dolphin |
| Mario Strikers Charged | R4QE01 | Wii Remote + Nunchuk | Works | | Dolphin |
| Mario Super Sluggers | RMBE01 | Wii Remote + Nunchuk | Works, some moves need a pack | The menus and the roster work. Pitching wants a real throwing motion; the shake never throws the ball | Dolphin |
| Metroid: Other M | R3OE01 | Sideways Wii Remote | Partly | Goes straight into first-person view and stays there, because the game sees the pointer all the time. A fix is ready. The screen may also dim after about 5 minutes (a fix is ready) | Dolphin |
| New Super Mario Bros. Wii | SMNE01 | Sideways Wii Remote | Works | R (shake) spins Mario | Dolphin |
| Pokémon Battle Revolution | RPBE01 | Pointing Wii Remote | Works | | Console |
| Punch-Out!! | R7PE01 | Sideways Wii Remote | Works | The screen may dim after about 5 minutes (a fix is ready) | Console |
| Rayman Raving Rabbids | RRBE41 | Wii Remote + Nunchuk | Partly | The pointer hides 2 seconds after the C-stick stops, so the profile list can't be picked. A fix is ready. The screen may also dim after about 5 minutes (a fix is ready) | Dolphin |
| Red Steel | REDE41 | Wii Remote + Nunchuk | Partly | Menus work, but the pointer hides 2 seconds after the C-stick stops, which stops the first tutorial ("point at the red fish"). A fix is ready | Dolphin |
| Scarface: The World Is Yours | RSCE7D | Wii Remote + Nunchuk | Partly | A new game's "Combat Tutorial" start screen ignores A (a save gets past it). In the open world: the stick walks, the C-stick aims, B fires, the D-pad picks weapons; X taunts instead of punching | Dolphin |
| Sonic and the Secret Rings | RSRE8P | Sideways Wii Remote | Works | The C-stick tilt steers Sonic | Dolphin |
| Super Mario Galaxy 2 | SB4E01 | Wii Remote + Nunchuk | Works | X spins, the C-stick moves the Star Pointer | Console |
| Super Monkey Ball: Banana Blitz | RSME8P | Sideways Wii Remote | Doesn't work yet | No button works on the title screen | Dolphin |
| Super Paper Mario | R8PE01 | Sideways Wii Remote | Partly | The game sees the pointer all the time, so it stays in Tippi's pointer mode and Mario can't walk. A fix is ready | Dolphin |
| Tetris Party Deluxe | STEETR | Classic Controller | Works | | Dolphin |
| The Legend of Zelda: Twilight Princess | RZDE01 | Wii Remote + Nunchuk | Works | X (shake) swings the sword. Only a short run so far | Dolphin |
| Trauma Center: Second Opinion | RKDEEB | Wii Remote + Nunchuk | Partly | The pointer hides 2 seconds after the C-stick stops, which stops the first suture. A fix is ready | Dolphin |
| Wario Land: Shake It! | RWLE01 | Sideways Wii Remote | Works | R (shake) does the earthquake punch | Dolphin |
| WarioWare: Smooth Moves | RODE01 | Pointing Wii Remote | Doesn't work yet | The pointer reaches START on the title, but A doesn't start the game. The screen may also dim after about 5 minutes (a fix is ready) | Dolphin |
| Wii Play | RHAE01 | Pointing Wii Remote | Works, some moves need a pack | Shooting Range plays (the crosshair follows the stick, B shoots). The other games, several played by motion, aren't checked yet | Dolphin |
| Wii Sports | RSPE01 | Pointing Wii Remote | Works, some moves need a pack | Menus work. The sports want swings: in tennis the shake tosses the serve but doesn't hit it, and bowling wants the Wii Remote swung back and forward | Console |
| Ghostbusters: The Video Game | RGQE70 | – | Not available | RiftWii keeps its controller code out of this game for now (the game uses the memory it would sit in); being looked at | – |
| 007: Quantum of Solace | RJ2E52 | – | Not available | As Ghostbusters | – |
