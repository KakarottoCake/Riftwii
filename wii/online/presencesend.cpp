// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "presencesend.hpp"

#include <network.h>
#include <ogc/es.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

#include "banners.hpp"
#include "frontend.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "nandchannels.hpp"
#include "netsock.hpp"
#include "riftwii/nandchannel.hpp"
#include "riftwii/presence.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

// A datagram can be lost on a busy Wi-Fi, and nothing answers it, so a
// game's message (the one that matters) goes out a few times; the
// computer takes the copies as one.
constexpr int kGameCopies = 3;
constexpr unsigned kCopyGapUs = 60 * 1000;

std::string g_packet;      // for SendJob, on the network's thread
int g_copies = 1;
// The GUI thread's tick and the menu thread's launch both send: one lock,
// so a tick that has begun ends before the game's message goes, and none
// begins after it (the computer would take "in the menu" as the game's end).
mutex_t g_lock = LWP_MUTEX_NULL;
bool g_menu_open = false;
u64 g_last_menu = 0;       // when the menu last said it was there (0: not yet)
bool g_logged_first = false;
// presence_host once looked up: its name, and its address (0 when it
// could not be found; not looked up again until the setting changes).
std::string g_host_name;
std::uint32_t g_host_ip = 0;

bool Enabled() { return Settings().online && Settings().presence; }

std::string ConsoleTag() {
    static std::string tag;
    if (tag.empty()) {
        u32 id = 0;
        if (ES_GetDeviceID(&id) >= 0) tag = presence_console_tag(id);
    }
    return tag;
}

PresenceMessage Base(PresenceEvent event) {
    PresenceMessage m;
    m.event = event;
    m.console = ConsoleTag();
    m.version = RIFTWII_VERSION;
    return m;
}

// presence_host's address. A host name is looked up only where
// `may_look_up` (the network's thread: a lookup can take seconds), and
// then kept.
std::uint32_t HostAddress(bool may_look_up) {
    const std::string& host = Settings().presence_host;
    if (host.empty()) return 0;
    if (host == g_host_name) return g_host_ip;
    NetServer server;
    std::string error;
    const bool dotted = host.find_first_not_of("0123456789.") == std::string::npos;
    if (!dotted && !may_look_up) return 0;
    g_host_name = host;
    g_host_ip = ResolveServer(host, kPresencePort, server, error) ? server.ip : 0;
    // Not the name itself: problem reports carry the log, and the name may
    // be a public one.
    if (g_host_ip == 0) logf("Presence: presence_host could not be found; only the broadcast is sent\n");
    return g_host_ip;
}

sockaddr_in AddressOf(std::uint32_t ip) {
    sockaddr_in a;
    std::memset(&a, 0, sizeof(a));
    a.sin_len = sizeof(a);
    a.sin_family = AF_INET;
    a.sin_port = htons(kPresencePort);
    a.sin_addr.s_addr = htonl(ip);
    return a;
}

// The packet to everyone on the network, and to presence_host. UDP: it
// leaves at once and nothing waits for an answer.
void Send(const std::string& packet, int copies, bool may_look_up) {
    const s32 s = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s < 0) {
        logf("Presence: no socket (%d)\n", static_cast<int>(s));
        return;
    }
    int on = 1;
    net_setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    std::vector<sockaddr_in> targets = {AddressOf(0xFFFFFFFFu)};
    if (const std::uint32_t host = HostAddress(may_look_up); host != 0) targets.push_back(AddressOf(host));
    for (int copy = 0; copy < copies; ++copy) {
        if (copy > 0) usleep(kCopyGapUs);
        for (sockaddr_in& to : targets)
            net_sendto(s, packet.data(), static_cast<s32>(packet.size()), 0, reinterpret_cast<sockaddr*>(&to),
                       sizeof(to));
    }
    net_close(s);
}

void SendJob() { Send(g_packet, g_copies, true); }

// Made by PresenceMenuOpen on the main thread, before the GUI thread runs.
struct Locked {
    Locked() {
        if (g_lock == LWP_MUTEX_NULL) LWP_MutexInit(&g_lock, false);
        LWP_MutexLock(g_lock);
    }
    ~Locked() { LWP_MutexUnlock(g_lock); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;
};

std::string GameTitle(const FrontendState& state) {
    if (state.use_channel && state.channel_index < state.channels.size())
        return nand_channel_name(state.channels[state.channel_index], BannerLanguageIndex());
    if (state.use_usb && state.usb_index < state.usb_catalog.games.size())
        return state.usb_catalog.games[state.usb_index].display;
    if (state.use_sd && state.sd_index < state.sd_catalog.games.size())
        return state.sd_catalog.games[state.sd_index].display;
    return GameDisplayName(state.game_id, state.disc_title);
}

void SetPlatform(const FrontendState& state, PresenceMessage& m) {
    m.platform = "wii";
    if (state.use_channel && state.channel_index < state.channels.size()) {
        const ChannelClass& kind = state.channels[state.channel_index].kind;
        if (kind.kind == ChannelKind::WiiWare) m.platform = "wiiware";
        if (kind.kind == ChannelKind::VirtualConsole) {
            m.platform = "vc";
            m.system = kind.system;
        }
        return;
    }
    const bool gamecube = (state.use_usb && state.usb_index < state.usb_catalog.games.size() &&
                           state.usb_catalog.games[state.usb_index].gamecube) ||
                          (state.use_sd && state.sd_index < state.sd_catalog.games.size() &&
                           state.sd_catalog.games[state.sd_index].gamecube);
    if (gamecube) m.platform = "gamecube";
}

}  // namespace

void PresenceMenuOpen(bool open) {
    Locked lock;
    g_menu_open = open;
}

void PresenceMenuTick() {
    // Unlocked first: every frame, and nearly always nothing to do.
    if (!g_menu_open || !Enabled() || !NetIsUp() || NetBackgroundBusy()) return;
    const u64 now = gettime();
    if (g_last_menu != 0 && diff_msec(g_last_menu, now) < kPresenceMenuSeconds * 1000ull) return;
    Locked lock;
    if (!g_menu_open) return;
    g_last_menu = now;
    PresenceMessage m = Base(PresenceEvent::Menu);
    m.next_seconds = kPresenceMenuSeconds;
    Send(presence_packet(m), 1, false);
    if (!g_logged_first) {
        g_logged_first = true;
        logf("Presence: told the network RiftWii is in its menu (again every %u s)\n", kPresenceMenuSeconds);
    }
}

void PresenceGame(const FrontendState& state, bool restart, bool background) {
    {
        Locked lock;
        g_menu_open = false;
    }
    if (!Enabled() || state.game_id.empty()) return;
    if (!NetIsUp()) {
        logf("Presence: the network is not up; %s is not told to the network\n", state.game_id.c_str());
        return;
    }
    PresenceMessage m = Base(PresenceEvent::Game);
    m.game_id = state.game_id;
    m.title = GameTitle(state);
    SetPlatform(state, m);
    m.restart = restart;
    for (const LaunchPackage& p : state.model.packages) {
        if (!p.valid || !p.enabled || !show_package(p)) continue;
        std::string name = pack_title(p);
        if (name.empty()) name = p.file.size() > 4 ? p.file.substr(0, p.file.size() - 4) : p.file;
        m.mods.push_back(name);
    }
    g_packet = presence_packet(m);
    g_copies = kGameCopies;
    logf("Presence: %s (%s) told to the network%s\n", m.game_id.c_str(), m.platform.c_str(),
         m.mods.empty() ? "" : ", with its mods");
    if (background) {
        NetWaitForBackground();  // a cover download, say: seconds at most
        if (NetRunInBackground(SendJob)) return;
    }
    SendJob();
}

void PresenceBye() {
    if (!Enabled() || !NetIsUp() || g_last_menu == 0) return;
    Send(presence_packet(Base(PresenceEvent::Bye)), kGameCopies, false);
    logf("Presence: told the network RiftWii is closing\n");
}

}  // namespace riftwii::wii
