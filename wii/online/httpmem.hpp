// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "online.hpp"

// One https GET read straight into a buffer the caller owns, for the theme
// browser (wii/menu/themestore.hpp): online.hpp's HttpGet builds its answer
// in std::vectors, which grow in MEM1's heap, and a theme's zip can be a
// few MB. Here the whole answer, headers and body, goes into one block of
// MEM2, and the TLS connection's state in another.
namespace riftwii::wii {

struct MemoryGet {
    // The whole answer is read into this: headers and body together.
    std::uint8_t* buffer = nullptr;
    std::size_t capacity = 0;
    // The TLS connection's state (TlsStream::memory_needed() bytes), or null
    // for the heap.
    void* tls_block = nullptr;
    std::size_t tls_bytes = 0;
    // The User-Agent to send (riftwii/http.hpp's http_get_request); "RiftWii"
    // when empty.
    const char* user_agent = nullptr;
    // What was received: the body is buffer[body_offset, body_offset + body_length).
    std::size_t body_offset = 0;
    std::size_t body_length = 0;
};

// GETs `url`, following up to three redirects on the same server, and
// reads the answer into `get`. A 200 answer whose body fits the buffer
// is a success; anything else fails with a plain-sentence `error`. Meant for
// the network's background thread (netsock.hpp): it ends within a tenth of a
// second of NetCancelBackground when the job is cancellable.
bool HttpGetInMemory(const std::string& url, MemoryGet& get, std::string& error, int timeout_ms = 15000,
                     const HttpProgress& progress = nullptr);

}  // namespace riftwii::wii
