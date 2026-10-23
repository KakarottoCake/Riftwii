// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "httpmem.hpp"

#include <ogc/lwp_watchdog.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "netsock.hpp"
#include "tls.hpp"
#include "riftwii/http.hpp"

namespace riftwii::wii {
namespace {

constexpr int kRedirectHops = 3;

// What the headers said, found in place in the buffer.
struct Headers {
    bool whole = false;          // the blank line that ends them was seen
    std::size_t body_at = 0;
    int status = 0;
    bool has_length = false;
    std::uint64_t length = 0;
    bool chunked = false;
    std::string location;
};

bool NameIs(const char* line, std::size_t line_size, const char* name) {
    const std::size_t n = std::strlen(name);
    if (line_size <= n || line[n] != ':') return false;
    for (std::size_t i = 0; i < n; ++i) {
        const char c = line[i] >= 'A' && line[i] <= 'Z' ? static_cast<char>(line[i] + 32) : line[i];
        if (c != name[i]) return false;
    }
    return true;
}

// Reads the status line and the headers of the answer in buffer[0, used).
void ReadHeaders(const std::uint8_t* buffer, std::size_t used, Headers& h) {
    const char* text = reinterpret_cast<const char*>(buffer);
    std::size_t end = 0;
    for (; end + 4 <= used; ++end) {
        if (std::memcmp(text + end, "\r\n\r\n", 4) == 0) {
            h.whole = true;
            break;
        }
    }
    if (!h.whole) return;
    h.body_at = end + 4;
    // "HTTP/1.1 200 OK"
    const char* space = static_cast<const char*>(std::memchr(text, ' ', end));
    h.status = space ? std::atoi(space + 1) : 0;
    std::size_t at = 0;
    while (at < end) {
        const char* line = text + at;
        const char* stop = static_cast<const char*>(std::memchr(line, '\n', end - at));
        std::size_t size = stop ? static_cast<std::size_t>(stop - line) : end - at;
        at += stop ? size + 1 : size;
        if (size > 0 && line[size - 1] == '\r') --size;
        if (NameIs(line, size, "content-length")) {
            h.has_length = true;
            h.length = std::strtoull(std::string(line + 15, size - 15).c_str(), nullptr, 10);
        } else if (NameIs(line, size, "transfer-encoding")) {
            h.chunked = std::string(line + 18, size - 18).find("chunked") != std::string::npos;
        } else if (NameIs(line, size, "location")) {
            std::size_t from = 9;
            while (from < size && line[from] == ' ') ++from;
            h.location.assign(line + from, size - from);
        }
    }
}

// A chunked body in buffer[from, used) rewritten in place as plain bytes;
// false when it is not whole or not well formed. `length` gets its size.
bool Dechunk(std::uint8_t* buffer, std::size_t from, std::size_t used, std::size_t& length) {
    std::size_t read = from, written = from;
    for (;;) {
        std::size_t line_end = read;
        while (line_end + 1 < used && !(buffer[line_end] == '\r' && buffer[line_end + 1] == '\n')) ++line_end;
        if (line_end + 1 >= used) return false;
        char digits[12];
        const std::size_t digit_count = std::min<std::size_t>(line_end - read, sizeof(digits) - 1);
        std::memcpy(digits, buffer + read, digit_count);
        digits[digit_count] = '\0';
        char* stop = nullptr;
        const unsigned long chunk = std::strtoul(digits, &stop, 16);
        if (stop == digits) return false;
        read = line_end + 2;
        if (chunk == 0) break;
        if (chunk > used - read || used - read - chunk < 2) return false;
        std::memmove(buffer + written, buffer + read, chunk);
        written += chunk;
        read += chunk + 2;
    }
    length = written - from;
    return true;
}

// One exchange with `url`: the answer into get.buffer.
bool Exchange(const HttpUrl& url, MemoryGet& get, Headers& headers, std::string& error, int timeout_ms,
              const HttpProgress& progress) {
    NetServer server;
    if (!ResolveServer(url.host, url.port, server, error)) return false;
    if (http_local_ipv4(server.ip)) {
        error = url.host + " (" + server.label() + ") is an address on this network, not the internet; not used";
        return false;
    }
    SocketTransport socket;
    if (!socket.connect(server, timeout_ms, error)) return false;
    // The whole answer gets four times the timeout, or longer for a big
    // download at 16 KiB/s, so a trickle cannot hold the page forever.
    const std::uint64_t budget_ms =
        std::max<std::uint64_t>(4ull * static_cast<std::uint64_t>(timeout_ms), get.capacity / 16u);
    const u64 started = gettime();
    const auto too_slow = [&] {
        error = url.host + " is answering too slowly; given up after " + std::to_string(budget_ms / 1000) + " s";
        return false;
    };
    TlsStream tls;
    tls.use_memory(get.tls_block, get.tls_bytes);
    tls.set_deadline(started + millisecs_to_ticks(budget_ms));
    if (url.tls && !tls.open(socket, url.host, error)) return tls.timed_out() ? too_slow() : false;
    const std::string request =
        get.user_agent && *get.user_agent ? http_get_request(url, get.user_agent) : http_get_request(url);
    const bool sent = url.tls ? tls.send(request.data(), request.size()) : socket.send(request.data(), request.size());
    if (!sent) {
        error = "cannot send the request to " + url.host;
        return false;
    }
    std::size_t used = 0;
    headers = Headers{};
    for (;;) {
        if (ticks_to_millisecs(diff_ticks(started, gettime())) > budget_ms) return too_slow();
        if (used == get.capacity) {
            error = url.host + " sent more than the " + std::to_string(get.capacity) + " bytes RiftWii has room for";
            return false;
        }
        std::size_t got = 0;
        const std::size_t room = std::min<std::size_t>(get.capacity - used, 4096);
        std::uint8_t* at = get.buffer + used;
        const bool ok = url.tls ? tls.receive_some(at, room, got) : socket.receive_some(at, room, got);
        if (!ok) {
            if (url.tls && tls.timed_out()) return too_slow();
            error = "no answer from " + url.host;
            if (url.tls && tls.last_error() != 0) error += " (TLS error " + std::to_string(tls.last_error()) + ")";
            return false;
        }
        if (got == 0) break;  // the server closed: the answer is whole
        used += got;
        if (progress) progress(used);
        if (!headers.whole) ReadHeaders(get.buffer, used, headers);
        if (headers.whole) {
            if (headers.has_length && used - headers.body_at >= headers.length) break;
            if (headers.chunked && used - headers.body_at >= 5 &&
                std::memcmp(get.buffer + used - 5, "0\r\n\r\n", 5) == 0)
                break;
        }
    }
    if (!headers.whole) ReadHeaders(get.buffer, used, headers);
    if (!headers.whole) {
        error = url.host + " sent an answer that is not HTTP";
        return false;
    }
    if (headers.chunked) {
        if (!Dechunk(get.buffer, headers.body_at, used, get.body_length)) {
            error = url.host + " sent a body that did not arrive whole";
            return false;
        }
    } else if (headers.has_length) {
        if (used - headers.body_at < headers.length) {
            error = url.host + " closed the connection before the download was whole";
            return false;
        }
        get.body_length = static_cast<std::size_t>(headers.length);
    } else {
        get.body_length = used - headers.body_at;
    }
    get.body_offset = headers.body_at;
    return true;
}

}  // namespace

bool HttpGetInMemory(const std::string& url, MemoryGet& get, std::string& error, int timeout_ms,
                     const HttpProgress& progress) {
    if (!NetStart(error)) return false;
    HttpUrl parsed;
    if (!parse_http_url(url, parsed, error)) return false;
    for (int hop = 0; hop <= kRedirectHops; ++hop) {
        Headers headers;
        if (!Exchange(parsed, get, headers, error, timeout_ms, progress)) return false;
        if (headers.status >= 300 && headers.status < 400 && !headers.location.empty()) {
            // Only to another path on the same server, never to another one.
            HttpUrl next;
            if (!http_redirect(parsed, headers.location, next, error)) return false;
            if (!http_redirect_allowed(HttpRedirects::SameHost, parsed, next)) {
                error = parsed.host + " answered " + std::to_string(headers.status) + " (a redirect to another server, not followed)";
                return false;
            }
            parsed = next;
            continue;
        }
        if (headers.status != 200) {
            error = parsed.host + " answered " + std::to_string(headers.status);
            return false;
        }
        return true;
    }
    error = "too many redirects for " + http_url_for_messages(url);
    return false;
}

}  // namespace riftwii::wii
