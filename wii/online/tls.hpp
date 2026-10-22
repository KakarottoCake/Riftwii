// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "netsock.hpp"

// TLS 1.2 over a connected socket, with BearSSL (vendor-bearssl): the
// server's certificate must chain to one of the roots in wii/online/tlsroots.c,
// for the host name asked for. Used for the update check (GitHub only
// speaks https). About 20 KB of heap while a connection is open.
namespace riftwii::wii {

class TlsStream {
public:
    TlsStream();
    ~TlsStream();
    // Keeps the connection's state (BearSSL's contexts and record buffer,
    // about 33 KB) in `block`, which must outlive the stream and be
    // memory_needed() bytes, instead of on the heap: the theme browser
    // gives it MEM2. Set before open(); open() falls back to the heap when
    // the block is too small.
    void use_memory(void* block, std::size_t bytes);
    static std::size_t memory_needed();
    // The handshake with `host` over `socket`, which stays owned by the
    // caller and must outlive this stream.
    bool open(SocketTransport& socket, const std::string& host, std::string& error);
    // Reads (the handshake's too) fail once the time base reaches
    // `deadline` (gettime() ticks; 0 for none). Set before open() to
    // count the handshake.
    void set_deadline(std::uint64_t deadline);
    // Whether a read failed for the deadline.
    bool timed_out() const;
    bool send(const void* data, std::size_t length);
    // Like SocketTransport::receive_some: `got` is 0 once the server has
    // closed the connection.
    bool receive_some(void* data, std::size_t max, std::size_t& got);
    // BearSSL's error code of the last failure (0 for none).
    int last_error() const;

    struct State;
    // Frees a heap state, or only ends one placed in caller memory.
    struct StateRelease {
        StateRelease() : placed(false) {}
        explicit StateRelease(bool in_caller_memory) : placed(in_caller_memory) {}
        bool placed;
        void operator()(State* state) const;
    };

private:
    using StatePtr = std::unique_ptr<State, StateRelease>;
    StatePtr state_;
    void* memory_ = nullptr;
    std::size_t memory_bytes_ = 0;
    std::uint64_t deadline_ = 0;
};

}  // namespace riftwii::wii
