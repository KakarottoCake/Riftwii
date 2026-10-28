// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii::wii {

struct FrontendState;

// RiftWii-RPC (riftwii/presence.hpp, docs/PRESENCE.md): the game being
// played, told to the local network so a computer there can show it on
// Discord. Nothing with Settings > Online off or presence = off in
// settings.txt, and nothing while the network is down: none of these
// start the network themselves, so a Wii without a connection never waits
// for one.

// The menu is open (from wii/main.cpp, around MainMenu, on the main
// thread; the first call makes the lock the GUI thread's tick takes once
// the menu is open). PresenceGame closes it too, so no "in the menu" can
// follow a game's message.
void PresenceMenuOpen(bool open);
// From the GUI thread, every frame, whatever screen is up (a popup or a
// page has its own loop, the GUI thread runs under all of them): says
// RiftWii is in its menu as soon as the network is up, then every
// kPresenceMenuSeconds. Only while no network job is running, and without
// a thread of its own: a datagram leaves at once.
void PresenceMenuTick();
// The game about to start, as the menu or a headless launch has it.
// `restart`: the game's own Reset started it again. `background`: on the
// network's thread (the menu, which waits for it before the game); else
// here and now.
void PresenceGame(const FrontendState& state, bool restart, bool background);
// RiftWii is closing: the computer clears the game at once instead of
// waiting for its timeout.
void PresenceBye();

}  // namespace riftwii::wii
