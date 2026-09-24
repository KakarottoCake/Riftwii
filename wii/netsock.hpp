// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/riifs.hpp"

// The Wii's network for RiiFS: IOS sockets through libogc, only while a
// copy runs (NetStart before, NetStop after; the game starts with the
// network as the system menu leaves it).
namespace riftwii::wii {

// Brings the network up with the console's saved connection. Takes a few
// seconds; fails when no connection is set up or it cannot be reached.
bool NetStart(std::string& error);
void NetStop();
// The last NetStart failed (and the next would wait as long again).
bool NetFailed();

struct NetServer {
    std::uint32_t ip = 0;  // host order
    std::uint16_t port = 0;
    std::string label() const;  // "192.168.1.20:1137"
    std::string folder() const; // "192.168.1.20_1137", the cache folder's name
};
bool ParseServerFolder(const std::string& name, NetServer& out);

// An address as written in <network address>: dotted IPv4 or a host name.
bool ResolveServer(const std::string& address, std::uint16_t port, NetServer& out, std::string& error);
// RiiFS servers on the local network that answer a broadcast ping on
// `port` within `timeout_ms`.
std::vector<NetServer> DiscoverServers(std::uint16_t port, int timeout_ms);

// A TCP connection to a server; every receive waits at most `timeout_ms`.
class SocketTransport final : public riifs::Transport {
public:
    ~SocketTransport() override;
    bool connect(const NetServer& server, int timeout_ms, std::string& error);
    void close();
    bool send(const void* data, std::size_t length) override;
    bool receive(void* data, std::size_t length) override;
    // Whatever has arrived, up to `max`: `got` is 0 when the server closed.
    // False when nothing came within the timeout or the socket failed.
    bool receive_some(void* data, std::size_t max, std::size_t& got);

private:
    std::int32_t socket_ = -1;
    int timeout_ms_ = 10000;
};

}  // namespace riftwii::wii
