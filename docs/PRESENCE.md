# Discord rich presence (RiftWii-RPC)

RiftWii tells the local network which game it is starting. RiftWii-RPC, a
small app for Windows, macOS and Linux (`rpc/`), hears it and shows the
game on Discord the way Dolphin does for games played in it: the game's
name, its cover, the mods turned on and how long it has been played.

How to install and set it up is in [rpc/README.md](../rpc/README.md). This
page is how it works.

## Why the Wii tells the computer, and not the other way round

A Wii game owns the Wii while it runs. RiftWii's network stops when the
game starts (the game's IOS replaces RiftWii's), so nothing of RiftWii can
answer on the network during a game, and most games never start a network
of their own. That rules out the computer finding the Wii (Bonjour, a scan
or a ping): there is nothing on the Wii to answer while it matters.

So the Wii speaks, while it still can, and the computer keeps what it last
heard:

| When | The Wii sends | The computer |
| --- | --- | --- |
| RiftWii's menu is open and its network is up | `menu`, then every 30 s | "In the RiftWii menu" (or nothing, if the player turned that off) until three in a row are missed |
| Play is chosen, before the game's IOS takes the network | `game`, three copies | the game, from now, until RiftWii is heard from again or the timeout passes |
| A game's own Reset starts it again through RiftWii (headless) | `game` with `restart 1`, if the network is up | the same play, its time kept |
| RiftWii closes (Wii Menu, Homebrew Channel, power off from the menu) | `bye`, three copies | nothing, at once |

"Heard from again" is how a game's end is noticed: leaving a game returns
to RiftWii (when Settings > Return to is RiftWii), whose menu speaks within
seconds. A Wii switched off from a game says nothing at all, so the
presence also ends after a timeout the player picks, from ten minutes to
twelve hours in steps of ten (RiiTag-RPC's choices; two hours unless
changed).

None of this starts the network. The menu speaks only once its network is
up (for game names, covers or the update check) and only between other
network jobs, and a launch only when the network is already up, so a Wii
without a connection never waits for one. With Settings > Online off, or
`presence = off` in `sd:/riftwii/settings.txt`, nothing is sent.

## The messages

UDP datagrams to port **51130**, sent to `255.255.255.255` (everyone on the
local network) and, when `presence_host` names a computer in
`settings.txt`, to that one too, for networks that drop broadcasts (some
mesh Wi-Fi and "client isolation" setups). At most 1024 bytes, UTF-8, one
`key value` pair per line after the first:

```
RIFTWII-PRESENCE 1
event game
console 1a2b3c4d
version 2610-737
id RMCP01
title Mario Kart Wii
platform wii
mod CTGP Revolution
```

| Key | What |
| --- | --- |
| `event` | `menu`, `game` or `bye` |
| `console` | which Wii, when there are several: 8 hex digits made from the console's ID. It only tells Wiis apart; it is not a secret |
| `version` | RiftWii's version |
| `id` | `game` only: the game's ID (six letters and digits; four for a channel) |
| `title` | `game` only: the name the menu shows |
| `platform` | `game` only: `wii`, `gamecube`, `wiiware` or `vc` |
| `system` | `game` only, Virtual Console: the original system ("NES") |
| `mod` | `game` only, once per pack turned on (eight at most) |
| `restart` | `game` only: `1` when the game's own Reset started it again |
| `next` | `menu` only: seconds until the next `menu` |

Values never hold line breaks or control characters and are cut at 128
bytes (whole characters). A reader skips keys it does not know, so a later
RiftWii can add some; a new first line means a format a reader must not
guess at. `src/net/presence.cpp` writes and reads them. RiftWii-RPC builds
that same file into itself, so the two cannot drift apart.

Anyone on the network can send to the port, so RiftWii-RPC takes only
whole messages, checks the game ID before putting it into a link, and keeps
every text within Discord's limits.

## What Discord shows

Discord shows the name of the Discord application whose ID RiftWii-RPC
connects with ("Playing RiftWii"), then:

- the game's name;
- its platform, and the mods turned on ("Wii · CTGP Revolution");
- its front cover from GameTDB, found as RiiTag-RPC finds covers (its
  region first, then English, US and Japanese ones, then the disc), with
  RiftWii-RPC's icon small in the corner; without a cover, the icon alone;
- the time since it started;
- a "Get RiftWii" button (it can be turned off).

RiftWii-RPC talks to the Discord app on the same computer through
Discord's local socket, as Discord's own SDK does, so it needs nothing
installed besides itself.

## RiiTag and its Discord bot

RiftWii already updates RiiTag itself when a game starts (GUIDE.md,
RiiTag), and RiftWii-RPC does not touch RiiTag. RiiConnect24's
RiiTag-Emulator-Bot updates RiiTag from Discord presences it recognises by
their application's name ("Dolphin Emulator", "Cemu", "Nintendo 3DS",
"Nintendo Switch", "Switch", "Yuzu", "Ryujinx", "citra", or one containing
"(3DS)"). RiftWii-RPC's application must use none of those names, or the bot
would count each play a second time. "RiftWii" is the name to use.
