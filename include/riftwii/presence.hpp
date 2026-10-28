// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The game RiftWii is playing, told to the local network for RiftWii-RPC
// (rpc/), which shows it as Discord rich presence on a computer. The
// message format is in docs/PRESENCE.md; this is the Wii's side of it and
// the parser the host tests check it with.
namespace riftwii {

// UDP, sent to the whole local network (255.255.255.255) and to the
// player's own computer when settings.txt names one (presence_host).
constexpr std::uint16_t kPresencePort = 51130;
// One datagram, well under any network's smallest MTU.
constexpr std::size_t kPresenceMaxBytes = 1024;
// The first line of every message, with the format's version.
constexpr const char* kPresenceMagic = "RIFTWII-PRESENCE 1";
// How often the menu says it is still there.
constexpr unsigned kPresenceMenuSeconds = 30;

enum class PresenceEvent {
    Menu,  // RiftWii started, or is still in its menu: a game before it has ended
    Game,  // a game is starting; nothing more is heard until RiftWii is back
    Bye,   // RiftWii is closing (to the Wii Menu, the Homebrew Channel, off)
};

struct PresenceMessage {
    PresenceEvent event = PresenceEvent::Menu;
    // Which Wii sent it, when several are on one network: a hash of the
    // console's ID (presence_console_tag).
    std::string console;
    std::string version;   // RiftWii's ("2610-541")
    std::string game_id;   // "RMCE01", or a channel's four letters and maker
    std::string title;     // as the menu names the game
    std::string platform;  // wii, gamecube, wiiware, vc
    std::string system;    // a Virtual Console game's system ("NES"), else empty
    std::vector<std::string> mods;  // the packs turned on, by the names the menu shows
    // The game's own Reset started it again: the same play goes on.
    bool restart = false;
    // Menu only: seconds until the next one, after which the computer may
    // take RiftWii as gone.
    unsigned next_seconds = 0;
};

// The message as sent: at most kPresenceMaxBytes, every value on one line
// without control characters and cut short (whole UTF-8 characters) when
// too long; mods beyond what fits are left out.
std::string presence_packet(const PresenceMessage& message);
// False for anything that is not a version 1 message. Unknown keys are
// skipped, so a newer RiftWii can add some.
bool parse_presence_packet(const std::string& packet, PresenceMessage& out);
// The console field for a console ID (ES_GetDeviceID): 8 hex digits.
std::string presence_console_tag(std::uint32_t device_id);

}  // namespace riftwii
