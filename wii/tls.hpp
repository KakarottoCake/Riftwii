// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "netsock.hpp"

// TLS 1.2 over a connected socket, with BearSSL (vendor-bearssl): the
// server's certificate must chain to one of the roots in wii/tlsroots.c,
// for the host name asked for. Used for the update check (GitHub only
// speaks https). About 20 KB of heap while a connection is open.
namespace riftwii::wii {

class TlsStream {
public:
    TlsStream();
    ~TlsStream();
    // The handshake with `host` over `socket`, which stays owned by the
    // caller and must outlive this stream.
    bool open(SocketTransport& socket, const std::string& host, std::string& error);
    bool send(const void* data, std::size_t length);
    // Like SocketTransport::receive_some: `got` is 0 once the server has
    // closed the connection.
    bool receive_some(void* data, std::size_t max, std::size_t& got);
    // BearSSL's error code of the last failure (0 for none).
    int last_error() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace riftwii::wii
