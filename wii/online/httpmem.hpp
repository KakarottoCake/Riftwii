// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
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
    // The longest body taken, or 0 for whatever fits. A longer one fails as
    // soon as its Content-Length or its bytes show it.
    std::size_t max_body = 0;
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

// GETs `url`, following up to three redirects allowed by `redirects`, and
// reads the answer into `get`. A 200 answer whose body fits the buffer
// is a success; anything else fails with a plain-sentence `error`. Meant for
// the network's background thread (netsock.hpp): it ends within a tenth of a
// second of NetCancelBackground when the job is cancellable.
bool HttpGetInMemory(const std::string& url, MemoryGet& get, std::string& error, int timeout_ms = 15000,
                     const HttpProgress& progress = nullptr, HttpRedirects redirects = HttpRedirects::SameHost);

// A GET whose body is handed on as it arrives instead of kept: a download
// bigger than memory (an app store app, up to 128 MB) goes to the SD card
// piece by piece. Only a window of `capacity` bytes is held (the headers
// must fit it: at least 64 KiB plus a receive's 4 KiB). A chunked body is
// decoded on the way, so `sink` sees the body's own bytes only.
struct StreamGet {
    std::uint8_t* buffer = nullptr;
    std::size_t capacity = 0;
    // The longest body taken; a longer one fails as soon as it shows.
    std::uint64_t max_body = 0;
    void* tls_block = nullptr;
    std::size_t tls_bytes = 0;
    const char* user_agent = nullptr;
    // Each piece of the body, in order. Returning false stops the
    // download (a write failed); `error` then says so.
    std::function<bool(const std::uint8_t* data, std::size_t size)> sink;
    // What was received in all.
    std::uint64_t body_length = 0;
};
bool HttpGetStream(const std::string& url, StreamGet& get, std::string& error, int timeout_ms = 15000,
                   const HttpProgress& progress = nullptr, HttpRedirects redirects = HttpRedirects::Follow);

}  // namespace riftwii::wii
