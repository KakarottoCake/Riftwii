// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "netsock.hpp"

#include <fcntl.h>
#include <network.h>
#include <ogc/lwp.h>
#include <ogc/lwp_watchdog.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "log.hpp"

namespace riftwii::wii {
namespace {

bool g_up = false;
volatile bool g_failed = false;
// A start that failed is not tried again for a minute: each try takes 20 s
// or more (and a tester's menu sat for minutes on one, opening Cheats with
// the network down); every download in between gets the same answer.
constexpr unsigned kRetryMs = 60 * 1000;
u64 g_failed_at = 0;
std::string g_failed_error;

lwp_t g_job_thread = LWP_THREAD_NULL;
void (*g_job)() = nullptr;
volatile bool g_job_done = true;
volatile bool g_job_cancellable = false;
volatile bool g_job_cancel = false;

// The background job was told to stop (only its own calls see this).
bool cancelled() { return g_job_cancel && g_job_thread != LWP_THREAD_NULL && LWP_GetSelf() == g_job_thread; }

void* run_job(void*) {
    g_job();
    g_job_done = true;
    return nullptr;
}

// /dev/net/ip/top's non-blocking flag for fcntl: 4 (Dolphin's socket code
// checks the same bit). Not newlib's O_NONBLOCK (0x4000), which
// <fcntl.h> defines first, nor libogc's (04000): with either, IOS kept the
// socket blocking and a server that never answered held connect for the
// stack's own three minutes (a 2.4.3 tester's GameTDB download).
constexpr u32 kIosNonBlock = 4;
// A TCP handshake takes well under a second; this allows a lost SYN or two.
constexpr int kConnectTimeoutMs = 8000;

sockaddr_in address_of(const NetServer& server) {
    sockaddr_in a;
    std::memset(&a, 0, sizeof(a));
    a.sin_len = sizeof(a);
    a.sin_family = AF_INET;
    a.sin_port = htons(server.port);
    a.sin_addr.s_addr = htonl(server.ip);
    return a;
}

// Polls `socket` for `events` until one comes or `timeout_ms` passes, in
// short slices: a poll can return early with nothing (Dolphin's does), so
// one long poll is not a wait, and the time waited is measured, not
// counted in slices (early returns ran a 10 s timeout out in 0.2 s). The
// revents seen are left in `revents`.
s32 poll_for(std::int32_t socket, std::uint32_t events, int timeout_ms, std::uint32_t& revents) {
    constexpr int kSlice = 100;
    s32 polled = 0;
    const u64 started = gettime();
    for (;;) {
        const u64 waited = diff_msec(started, gettime());
        if (cancelled()) return 0;
        pollsd p;
        p.socket = socket;
        p.events = events;
        p.revents = 0;
        polled = net_poll(&p, 1, kSlice);
        revents = p.revents;
        if (polled != 0 || timeout_ms <= 0 || waited >= static_cast<u64>(timeout_ms)) return polled;
        usleep(1000);  // in case the poll returned at once
    }
}

// Waits until `socket` has `events`, at most `timeout_ms`.
bool wait_for(std::int32_t socket, std::uint32_t events, int timeout_ms) {
    std::uint32_t revents = 0;
    return poll_for(socket, events, timeout_ms, revents) > 0 && (revents & events) != 0;
}

// Reads the decimal number at `text` (digits only, at most `max_digits` of
// them) and moves `text` past it. By hand: RiftWii calls no scanf, which
// costs MEM1 (docs/MEMORY.md, "Linked out").
bool read_decimal(const char*& text, unsigned max_digits, unsigned& value) {
    unsigned digits = 0;
    value = 0;
    while (digits < max_digits && *text >= '0' && *text <= '9') {
        value = value * 10 + unsigned(*text - '0');
        ++text;
        ++digits;
    }
    return digits > 0;
}

// Reads a dotted IPv4 address ("192.168.1.20") at `text` and moves `text`
// past it.
bool read_ipv4(const char*& text, std::uint32_t& ip) {
    ip = 0;
    for (int part = 0; part < 4; ++part) {
        if (part > 0 && *text++ != '.') return false;
        unsigned octet = 0;
        if (!read_decimal(text, 3, octet) || octet > 255) return false;
        ip = (ip << 8) | octet;
    }
    return true;
}

}  // namespace

bool NetStart(std::string& error, bool retry) {
    NetWaitForBackground();
    if (g_up) return true;
    if (!retry && g_failed && diff_msec(g_failed_at, gettime()) < kRetryMs) {
        error = g_failed_error;
        return false;
    }
    // IOS answers -EAGAIN while the interface comes up.
    s32 rc = -EAGAIN;
    for (int tries = 0; tries < 200 && rc == -EAGAIN; ++tries) {
        // A cancelled job stops within a tenth of a second, not after 20 s.
        if (cancelled()) {
            // Not a failure of the network: the next start tries at once.
            g_failed = false;
            error = "stopped before the network started";
            return false;
        }
        rc = net_init();
        if (rc == -EAGAIN) usleep(100 * 1000);
    }
    if (rc < 0) {
        // A job cancelled meanwhile leaves no failure behind: whoever
        // starts the network next tries it for themselves.
        if (cancelled()) {
            g_failed = false;
            error = "stopped before the network started";
            return false;
        }
        g_failed = true;
        error = "the network did not start (" + std::to_string(rc) +
                "); check the connection in the Wii's Internet settings";
        g_failed_at = gettime();
        g_failed_error = error;
        return false;
    }
    g_up = true;
    g_failed = false;
    const u32 ip = net_gethostip();
    logf("Network: up, this Wii is %u.%u.%u.%u\n", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    return true;
}

bool NetFailed() { return g_failed; }

bool NetIsUp() { return g_up; }

void NetStop() {
    NetWaitForBackground();
    if (!g_up) return;
    net_deinit();
    g_up = false;
}

bool NetRunInBackground(void (*job)(), bool cancellable, bool run_here) {
    if (!g_job_done) return false;
    NetWaitForBackground();  // a finished one's thread
    g_job = job;
    g_job_cancel = false;
    g_job_cancellable = cancellable;
    g_job_done = false;
    // TLS keeps its state on the heap; 32 KiB covers its RSA and EC math.
    if (LWP_CreateThread(&g_job_thread, run_job, nullptr, nullptr, 32768, 40) < 0) {
        g_job_thread = LWP_THREAD_NULL;
        g_job_done = true;
        if (!run_here) return false;
        job();
    }
    return true;
}

bool NetBackgroundBusy() { return !g_job_done; }

void NetCancelBackground() {
    if (!g_job_done && g_job_cancellable) g_job_cancel = true;
}

bool NetCancelRequested() { return cancelled(); }

void NetWaitForBackground() {
    // The job's own NetStart and NetStop go straight through.
    if (g_job_thread == LWP_THREAD_NULL || LWP_GetSelf() == g_job_thread) return;
    LWP_JoinThread(g_job_thread, nullptr);
    g_job_thread = LWP_THREAD_NULL;
}

std::string NetServer::label() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
                  static_cast<unsigned>(port));
    return buf;
}

std::string NetServer::folder() const {
    std::string s = label();
    s[s.find(':')] = '_';
    return s;
}

bool ParseServerFolder(const std::string& name, NetServer& out) {
    const char* text = name.c_str();
    std::uint32_t ip = 0;
    unsigned port = 0;
    if (!read_ipv4(text, ip) || *text++ != '_') return false;
    if (!read_decimal(text, 5, port) || *text != '\0') return false;
    if (port == 0 || port > 65535) return false;
    out.ip = ip;
    out.port = static_cast<std::uint16_t>(port);
    return true;
}

bool ResolveServer(const std::string& address, std::uint16_t port, NetServer& out, std::string& error) {
    const char* text = address.c_str();
    std::uint32_t dotted = 0;
    if (read_ipv4(text, dotted) && *text == '\0') {
        out.ip = dotted;
        out.port = port;
        return true;
    }
    hostent* host = net_gethostbyname(address.c_str());
    if (host == nullptr || host->h_addr_list == nullptr || host->h_addr_list[0] == nullptr) {
        error = "cannot find the address of " + address;
        return false;
    }
    std::uint32_t ip;
    std::memcpy(&ip, host->h_addr_list[0], sizeof(ip));
    out.ip = ntohl(ip);
    out.port = port;
    return true;
}

std::vector<NetServer> DiscoverServers(std::uint16_t port, int timeout_ms) {
    std::vector<NetServer> found;
    const s32 s = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s < 0) return found;
    int on = 1;
    net_setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    // The ping: one big-endian word, 0x10. A server answers with its TCP
    // port, from its own address.
    NetServer everyone;
    everyone.ip = 0xFFFFFFFFu;
    everyone.port = port;
    sockaddr_in to = address_of(everyone);
    const std::uint8_t ping[4] = {0, 0, 0, 0x10};
    for (int round = 0; round < 3; ++round) {
        net_sendto(s, ping, sizeof(ping), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        const int slice = timeout_ms / 3;
        const u64 started = gettime();
        for (;;) {
            // A reply must not restart the round's timeout: duplicate or
            // malformed datagrams can otherwise keep discovery here forever.
            const u64 elapsed = diff_msec(started, gettime());
            if (slice <= 0 || elapsed >= static_cast<u64>(slice)) break;
            const int remaining = slice - static_cast<int>(elapsed);
            if (!wait_for(s, POLLIN, remaining)) break;
            std::uint8_t reply[4];
            sockaddr_in from;
            socklen_t from_len = sizeof(from);
            if (net_recvfrom(s, reply, sizeof(reply), 0, reinterpret_cast<sockaddr*>(&from), &from_len) != 4) continue;
            NetServer server;
            server.ip = ntohl(from.sin_addr.s_addr);
            const std::uint32_t p = (std::uint32_t(reply[0]) << 24) | (std::uint32_t(reply[1]) << 16) |
                                    (std::uint32_t(reply[2]) << 8) | reply[3];
            if (p == 0 || p > 65535) continue;
            server.port = static_cast<std::uint16_t>(p);
            bool seen = false;
            for (const NetServer& f : found) seen = seen || (f.ip == server.ip && f.port == server.port);
            if (!seen) found.push_back(server);
        }
        if (!found.empty()) break;
    }
    net_close(s);
    return found;
}

SocketTransport::~SocketTransport() { close(); }

bool SocketTransport::connect(const NetServer& server, int timeout_ms, std::string& error) {
    close();
    socket_ = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket_ < 0) {
        error = "cannot open a socket (" + std::to_string(socket_) + ")";
        socket_ = -1;
        return false;
    }
    // Non-blocking while connecting, so a PC that is off costs the timeout
    // and not the stack's own minute.
    const s32 flags = net_fcntl(socket_, F_GETFL, 0);
    if (flags >= 0) net_fcntl(socket_, F_SETFL, static_cast<u32>(flags) | kIosNonBlock);
    sockaddr_in to = address_of(server);
    s32 rc = -EINPROGRESS;
    const int limit = timeout_ms < kConnectTimeoutMs ? timeout_ms : kConnectTimeoutMs;
    for (int waited = 0; waited <= limit && !cancelled(); waited += 50) {
        rc = net_connect(socket_, reinterpret_cast<sockaddr*>(&to), sizeof(to));
        if (rc == 0 || rc == -EISCONN) {
            rc = 0;
            break;
        }
        if (rc != -EINPROGRESS && rc != -EALREADY) break;
        usleep(50 * 1000);
    }
    if (flags >= 0) net_fcntl(socket_, F_SETFL, static_cast<u32>(flags) & ~kIosNonBlock);
    if (rc != 0) {
        error = "cannot connect to " + server.label() + " (" + std::to_string(rc) + ")";
        close();
        return false;
    }
    int on = 1;
    net_setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    return true;
}

void SocketTransport::close() {
    if (socket_ >= 0) net_close(socket_);
    socket_ = -1;
}

bool SocketTransport::send(const void* data, std::size_t length) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    while (length > 0 && socket_ >= 0) {
        // A server that stops reading must not hold the send for ever.
        if (!wait_for(socket_, POLLOUT, timeout_ms_)) return false;
        const s32 n = net_send(socket_, p, static_cast<s32>(length > 0x8000 ? 0x8000 : length), 0);
        if (n == -EAGAIN) continue;
        if (n <= 0) return false;
        p += n;
        length -= static_cast<std::size_t>(n);
    }
    return length == 0;
}

bool SocketTransport::receive(void* data, std::size_t length) {
    auto* p = static_cast<std::uint8_t*>(data);
    while (length > 0 && socket_ >= 0) {
        if (!wait_for(socket_, POLLIN, timeout_ms_)) return false;
        const s32 n = net_recv(socket_, p, static_cast<s32>(length > 0x8000 ? 0x8000 : length), 0);
        if (n == -EAGAIN) continue;
        if (n <= 0) return false;
        p += n;
        length -= static_cast<std::size_t>(n);
    }
    return length == 0;
}

bool SocketTransport::receive_some(void* data, std::size_t max, std::size_t& got) {
    got = 0;
    if (socket_ < 0) return false;
    for (;;) {
        // A server that answers and closes at once can leave only a hang-up
        // in revents: the reply is still waiting, so read it (recv then
        // gives the data, or 0 for the close).
        std::uint32_t revents = 0;
        const s32 polled = poll_for(socket_, POLLIN, timeout_ms_, revents);
        if (polled <= 0 || (revents & (POLLIN | POLLHUP | POLLERR)) == 0) {
            logf("Net: nothing to read (poll %d, revents 0x%x)\n", static_cast<int>(polled), static_cast<unsigned>(revents));
            return false;
        }
        const s32 n = net_recv(socket_, data, static_cast<s32>(max > 0x8000 ? 0x8000 : max), 0);
        if (n == -EAGAIN) continue;
        if (n < 0) {
            logf("Net: recv failed (%d)\n", static_cast<int>(n));
            return false;
        }
        got = static_cast<std::size_t>(n);
        return true;
    }
}

}  // namespace riftwii::wii
