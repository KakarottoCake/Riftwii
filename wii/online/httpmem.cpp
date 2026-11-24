// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "httpmem.hpp"

#include <ogc/lwp_watchdog.h>

#include <malloc.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>

#include "netsock.hpp"
#include "tls.hpp"
#include "riftwii/http.hpp"
#include "riftwii/checkedalloc.hpp"

namespace riftwii::wii {
namespace {

constexpr int kRedirectHops = 3;
constexpr std::size_t kMaxHeaderBytes = 64u << 10;
constexpr std::size_t kMaxLocationBytes = 4096;

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
bool ReadHeaders(const std::uint8_t* buffer, std::size_t used, Headers& h, std::string& error) {
    const char* text = reinterpret_cast<const char*>(buffer);
    std::size_t end = 0;
    for (; end + 4 <= used; ++end) {
        if (std::memcmp(text + end, "\r\n\r\n", 4) == 0) {
            h.whole = true;
            break;
        }
    }
    if ((!h.whole && used > kMaxHeaderBytes) || (h.whole && end + 4 > kMaxHeaderBytes)) {
        error = "the server sent HTTP headers longer than 64 KiB";
        return false;
    }
    if (!h.whole) return true;
    h.body_at = end + 4;
    // "HTTP/1.1 200 OK"
    const char* space = static_cast<const char*>(std::memchr(text, ' ', end));
    if (space != nullptr && text + end - space >= 4) {
        for (int i = 1; i <= 3; ++i) {
            if (space[i] < '0' || space[i] > '9') {
                h.status = 0;
                break;
            }
            h.status = h.status * 10 + space[i] - '0';
        }
        // "HTTP/1.1 2000" is not a 200: the digits end the status.
        const char after = text + end - space > 4 ? space[4] : '\r';
        if (after != ' ' && after != '\r') h.status = 0;
    }
    std::size_t at = 0;
    while (at < end) {
        const char* line = text + at;
        const char* stop = static_cast<const char*>(std::memchr(line, '\n', end - at));
        std::size_t size = stop ? static_cast<std::size_t>(stop - line) : end - at;
        at += stop ? size + 1 : size;
        if (size > 0 && line[size - 1] == '\r') --size;
        if (NameIs(line, size, "content-length")) {
            h.has_length = true;
            h.length = 0;
            std::string_view value(line + 15, size - 15);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
            if (value.empty()) {
                error = "the server sent an empty HTTP Content-Length";
                return false;
            }
            for (char c : value) {
                if (c < '0' || c > '9' || h.length > (std::numeric_limits<std::uint64_t>::max() - (c - '0')) / 10) {
                    error = "the server sent an invalid HTTP Content-Length";
                    return false;
                }
                h.length = h.length * 10 + c - '0';
            }
        } else if (NameIs(line, size, "transfer-encoding")) {
            // Transfer codings are case-insensitive ("Chunked").
            std::string coding(line + 18, size - 18);
            for (char& c : coding) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
            h.chunked = coding.find("chunked") != std::string::npos;
        } else if (NameIs(line, size, "location")) {
            std::size_t from = 9;
            while (from < size && line[from] == ' ') ++from;
            if (size - from > kMaxLocationBytes) {
                error = "the server sent an HTTP redirect longer than 4096 bytes";
                return false;
            }
            if (!try_reserve(h.location, size - from)) {
                error = "not enough memory for the HTTP redirect";
                return false;
            }
            h.location.assign(line + from, size - from);
        }
    }
    return true;
}

// A whole size line, including any extension. Both the receive-time limit
// and the final copy use this parser, so they agree on each chunk's size.
bool ReadChunkSize(const std::uint8_t* buffer, std::size_t from, std::size_t used,
                   std::size_t& chunk, std::size_t& data_at) {
    std::size_t end = from;
    while (end + 1 < used && !(buffer[end] == '\r' && buffer[end + 1] == '\n')) ++end;
    if (end + 1 >= used) return false;
    chunk = 0;
    std::size_t at = from;
    for (; at < end && buffer[at] != ';' && buffer[at] != ' ' && buffer[at] != '\t'; ++at) {
        const unsigned char c = buffer[at];
        unsigned digit = 0;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        if (chunk > (std::numeric_limits<std::size_t>::max() - digit) / 16) return false;
        chunk = chunk * 16 + digit;
    }
    if (at == from) return false;
    while (at < end && (buffer[at] == ' ' || buffer[at] == '\t')) ++at;
    if (at < end && buffer[at] != ';') return false;
    data_at = end + 2;
    return true;
}

struct ChunkLimit {
    std::size_t next = 0;
    std::size_t counted = 0;
};

// Count payload, excluding framing, as size lines arrive. Completed chunks
// are skipped on later reads; an oversized chunk is refused before its data
// needs to arrive. Malformed or unfinished framing is rejected by Dechunk.
bool ChunkBodyTooLarge(const std::uint8_t* buffer, std::size_t from, std::size_t used,
                       std::size_t limit, ChunkLimit& reading) {
    if (reading.next == 0) reading.next = from;
    for (;;) {
        std::size_t chunk = 0;
        std::size_t data_at = 0;
        if (!ReadChunkSize(buffer, reading.next, used, chunk, data_at)) return false;
        if (chunk > limit - reading.counted) return true;
        if (chunk == 0) return false;
        if (chunk > used - data_at || used - data_at - chunk < 2) return false;
        reading.counted += chunk;
        reading.next = data_at + chunk + 2;
    }
}

// A chunked body in buffer[from, used) rewritten in place as plain bytes;
// false when it is not whole or not well formed. `length` gets its size.
bool Dechunk(std::uint8_t* buffer, std::size_t from, std::size_t used, std::size_t& length) {
    std::size_t read = from, written = from;
    for (;;) {
        std::size_t chunk = 0;
        if (!ReadChunkSize(buffer, read, used, chunk, read)) return false;
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
    ChunkLimit chunk_limit;
    const auto too_big = [&] {
        error = url.host + " sent more than the " + std::to_string(get.max_body) + " bytes RiftWii takes";
        return false;
    };
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
        if (!headers.whole && !ReadHeaders(get.buffer, used, headers, error)) return false;
        if (headers.whole) {
            if (get.max_body) {
                if (headers.chunked) {
                    if (ChunkBodyTooLarge(get.buffer, headers.body_at, used, get.max_body, chunk_limit))
                        return too_big();
                } else if (headers.has_length) {
                    if (headers.length > get.max_body) return too_big();
                } else if (used - headers.body_at > get.max_body) {
                    return too_big();
                }
            }
            // Transfer-Encoding takes precedence over Content-Length.
            if (!headers.chunked && headers.has_length && used - headers.body_at >= headers.length) break;
            if (headers.chunked && used - headers.body_at >= 5 &&
                std::memcmp(get.buffer + used - 5, "0\r\n\r\n", 5) == 0)
                break;
        }
    }
    if (!headers.whole && !ReadHeaders(get.buffer, used, headers, error)) return false;
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
    if (get.max_body && get.body_length > get.max_body) return too_big();
    get.body_offset = headers.body_at;
    return true;
}

// One GET of a streamed download. Only a 200's body goes to the sink; a
// redirect or an error answer is left unread once its headers are here.
bool StreamExchange(const HttpUrl& url, StreamGet& get, Headers& headers, std::string& error, int timeout_ms,
                    const HttpProgress& progress) {
    NetServer server;
    if (!ResolveServer(url.host, url.port, server, error)) return false;
    if (http_local_ipv4(server.ip)) {
        error = url.host + " (" + server.label() + ") is an address on this network, not the internet; not used";
        return false;
    }
    SocketTransport socket;
    if (!socket.connect(server, timeout_ms, error)) return false;
    // The headers get four times the timeout; the body then gets as long as
    // its size takes at 16 KiB/s, so a trickle cannot hold the download forever.
    std::uint64_t budget_ms = 4ull * static_cast<std::uint64_t>(timeout_ms);
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
    const auto too_big = [&] {
        error = url.host + " sent more than the " + std::to_string(get.max_body) + " bytes RiftWii takes";
        return false;
    };
    const auto sink_stopped = [&] {
        if (error.empty()) error = "the download from " + url.host + " could not be saved";
        return false;
    };
    headers = Headers{};
    HttpChunkDecoder chunks(get.max_body);
    std::size_t used = 0;          // bytes held in the window
    std::uint64_t body_seen = 0;   // of a body that is not chunked
    bool closed = false;
    // A trickle is cut off: under 32 KiB in 30 s, the download has stalled
    // (the time budget alone would let 128 MB take two hours).
    constexpr std::uint64_t kStallMs = 30000;
    constexpr std::uint64_t kStallBytes = 32u << 10;
    u64 stall_since = gettime();
    std::uint64_t stall_bytes = 0;
    for (;;) {
        if (ticks_to_millisecs(diff_ticks(started, gettime())) > budget_ms) return too_slow();
        if (used == get.capacity) {
            error = url.host + " sent HTTP headers too long for RiftWii's download window";
            return false;
        }
        std::size_t got = 0;
        const std::size_t room = std::min<std::size_t>(get.capacity - used, 4096);
        const bool ok = url.tls ? tls.receive_some(get.buffer + used, room, got)
                                : socket.receive_some(get.buffer + used, room, got);
        if (!ok) {
            if (url.tls && tls.timed_out()) return too_slow();
            error = "no answer from " + url.host;
            if (url.tls && tls.last_error() != 0) error += " (TLS error " + std::to_string(tls.last_error()) + ")";
            return false;
        }
        if (got == 0) closed = true;
        used += got;

        if (!headers.whole) {
            if (!ReadHeaders(get.buffer, used, headers, error)) return false;
            if (!headers.whole) {
                if (closed) {
                    error = url.host + " sent an answer that is not HTTP";
                    return false;
                }
                continue;
            }
            // Only a 200's body is wanted: HttpGetStream follows a redirect
            // or reports the status without it.
            if (headers.status != 200) return true;
            if (!headers.chunked && headers.has_length && headers.length > get.max_body) return too_big();
            const std::uint64_t expected = headers.has_length && !headers.chunked ? headers.length : get.max_body;
            budget_ms = std::max<std::uint64_t>(budget_ms, expected / 16u);
            tls.set_deadline(started + millisecs_to_ticks(budget_ms));
            // The body's first bytes came with the headers: move them to the
            // window's start, so `used` counts body bytes only from here on.
            std::memmove(get.buffer, get.buffer + headers.body_at, used - headers.body_at);
            used -= headers.body_at;
        }

        bool done = false;
        if (headers.chunked) {
            // Transfer-Encoding takes precedence over Content-Length.
            std::size_t taken = 0;
            const auto result = chunks.feed(get.buffer, used, taken, [&](const std::uint8_t* data, std::size_t size) {
                return get.sink(data, size);
            });
            if (chunks.stopped()) return sink_stopped();
            if (result == HttpChunkDecoder::Result::Bad) {
                if (chunks.body() >= get.max_body) return too_big();
                error = url.host + " sent a body that did not arrive whole";
                return false;
            }
            std::memmove(get.buffer, get.buffer + taken, used - taken);
            used -= taken;
            get.body_length = chunks.body();
            done = result == HttpChunkDecoder::Result::Done;
        } else {
            std::size_t take = used;
            if (headers.has_length) take = static_cast<std::size_t>(std::min<std::uint64_t>(take, headers.length - body_seen));
            if (!headers.has_length && take > get.max_body - body_seen) return too_big();
            if (take > 0 && !get.sink(get.buffer, take)) return sink_stopped();
            body_seen += take;
            used = 0;  // anything past Content-Length is not part of the answer
            get.body_length = body_seen;
            done = headers.has_length && body_seen == headers.length;
        }
        if (progress && got > 0) progress(static_cast<std::size_t>(get.body_length));
        if (headers.whole && ticks_to_millisecs(diff_ticks(stall_since, gettime())) >= kStallMs) {
            if (get.body_length - stall_bytes < kStallBytes) {
                error = url.host + " stopped sending (under 32 KB in 30 s); given up";
                return false;
            }
            stall_since = gettime();
            stall_bytes = get.body_length;
        }
        if (done) return true;
        if (closed) {
            if (!headers.chunked && !headers.has_length) return true;  // ends with the connection
            error = url.host + " closed the connection before the download was whole";
            return false;
        }
    }
}

}  // namespace

bool HttpGetInMemory(const std::string& url, MemoryGet& get, std::string& error, int timeout_ms,
                     const HttpProgress& progress, HttpRedirects redirects) {
    get.body_offset = 0;
    get.body_length = 0;
    if (!NetStart(error)) return false;
    HttpUrl parsed;
    if (!parse_http_url(url, parsed, error)) return false;
    for (int hop = 0; hop <= kRedirectHops; ++hop) {
        Headers headers;
        if (!Exchange(parsed, get, headers, error, timeout_ms, progress)) return false;
        if (headers.status >= 300 && headers.status < 400 && !headers.location.empty()) {
            // The theme browser stays on its server; release assets may
            // follow only the HTTPS GitHub download hosts.
            HttpUrl next;
            if (!http_redirect(parsed, headers.location, next, error)) {
                // By host: the refused address can hold a player's ID too.
                error = parsed.host + " answered " + std::to_string(headers.status) + " (a redirect that cannot be followed)";
                return false;
            }
            if (!http_redirect_allowed(redirects, parsed, next)) {
                error = parsed.host + " answered " + std::to_string(headers.status) + " (a redirect not allowed for this download)";
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
    // By host, as above: the path can hold a player's ID (RiiTag), and this
    // text reaches session.log.
    error = "too many redirects from " + parsed.host;
    return false;
}

bool HttpGetStream(const std::string& url, StreamGet& get, std::string& error, int timeout_ms,
                   const HttpProgress& progress, HttpRedirects redirects) {
    get.body_length = 0;
    if (!get.sink || get.buffer == nullptr || get.capacity < kMaxHeaderBytes + 4096 || get.max_body == 0) {
        error = "the download was not set up";
        return false;
    }
    if (!NetStart(error)) return false;
    HttpUrl parsed;
    if (!parse_http_url(url, parsed, error)) return false;
    for (int hop = 0; hop <= kRedirectHops; ++hop) {
        Headers headers;
        if (!StreamExchange(parsed, get, headers, error, timeout_ms, progress)) return false;
        if (headers.status >= 300 && headers.status < 400 && !headers.location.empty()) {
            HttpUrl next;
            if (!http_redirect(parsed, headers.location, next, error)) {
                error = parsed.host + " answered " + std::to_string(headers.status) + " (a redirect that cannot be followed)";
                return false;
            }
            if (!http_redirect_allowed(redirects, parsed, next)) {
                error = parsed.host + " answered " + std::to_string(headers.status) + " (a redirect not allowed for this download)";
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
    error = "too many redirects from " + parsed.host;
    return false;
}

namespace {

// The window HttpGetStream reads through: a receive's worth of headers
// and body at a time (the headers must fit, at least 64 KiB).
constexpr std::size_t kFileWindow = (64u << 10) + (8u << 10);

struct HeapBlock {
    void* data = nullptr;
    explicit HeapBlock(std::size_t bytes) : data(memalign(32, bytes)) {}
    ~HeapBlock() { std::free(data); }
    HeapBlock(const HeapBlock&) = delete;
    HeapBlock& operator=(const HeapBlock&) = delete;
};

}  // namespace

bool HttpDownloadToFile(const std::string& url, const std::string& path, std::uint64_t max_bytes, FileDownload& got,
                        std::string& error, int timeout_ms, const HttpProgress& progress, HttpRedirects redirects) {
    got = FileDownload{};
    HeapBlock tls(TlsStream::memory_needed());
    HeapBlock window(kFileWindow);
    if (tls.data == nullptr || window.data == nullptr) {
        error = "not enough memory for the download";
        return false;
    }
    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        error = "cannot write " + path;
        return false;
    }
    Sha256 hash;
    bool write_failed = false;
    StreamGet get;
    get.buffer = static_cast<std::uint8_t*>(window.data);
    get.capacity = kFileWindow;
    get.max_body = max_bytes;
    get.tls_block = tls.data;
    get.tls_bytes = TlsStream::memory_needed();
    get.sink = [&](const std::uint8_t* data, std::size_t size) {
        if (std::fwrite(data, 1, size, file) != size) {
            write_failed = true;
            return false;
        }
        hash.update(data, size);
        return true;
    };
    bool ok = HttpGetStream(url, get, error, timeout_ms, progress, redirects);
    if (write_failed) error = "the SD card could not be written (is it full?)";
    if (std::fclose(file) != 0 && ok) {
        ok = false;
        error = "the SD card could not be written (is it full?)";
    }
    if (!ok) {
        // Never half a file: whoever reads it next would take it for whole.
        std::remove(path.c_str());
        return false;
    }
    got.length = get.body_length;
    got.digest = hash.finish();
    return true;
}

bool DigestMatches(const Sha256Digest& digest, const std::string& expected) {
    if (expected.size() != 2 * digest.size()) return false;
    static const char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < digest.size(); ++i) {
        if (expected[2 * i] != hex[digest[i] >> 4] || expected[2 * i + 1] != hex[digest[i] & 15]) return false;
    }
    return true;
}

}  // namespace riftwii::wii
