// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/http.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace riftwii {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

// Where the headers end (the index after the blank line), or npos. The
// search starts at `from`.
std::size_t header_end(const std::vector<std::uint8_t>& raw, std::size_t from = 0) {
    for (std::size_t i = from; i + 3 < raw.size(); ++i) {
        if (raw[i] == '\r' && raw[i + 1] == '\n' && raw[i + 2] == '\r' && raw[i + 3] == '\n') return i + 4;
    }
    return std::string::npos;
}

bool parse_headers(const std::vector<std::uint8_t>& raw, std::size_t end, HttpResponse& out, std::string& error) {
    const std::string text(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(end));
    std::size_t line_end = text.find("\r\n");
    const std::string status = text.substr(0, line_end);
    if (status.compare(0, 5, "HTTP/") != 0 || status.find(' ') == std::string::npos) {
        error = "not an HTTP response";
        return false;
    }
    out.status = std::atoi(status.c_str() + status.find(' ') + 1);
    std::size_t at = line_end + 2;
    while (at < text.size()) {
        const std::size_t next = text.find("\r\n", at);
        if (next == std::string::npos || next == at) break;
        const std::string line = text.substr(at, next - at);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) out.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
        at = next + 2;
    }
    return true;
}

bool chunked(const HttpResponse& r) {
    const auto it = r.headers.find("transfer-encoding");
    return it != r.headers.end() && lower(it->second).find("chunked") != std::string::npos;
}

// A Content-Length: decimal digits only ("abc" or "-1" is no length).
bool parse_length(const std::string& text, std::uint64_t& out) {
    if (text.empty() || text.size() > 18) return false;
    out = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        out = out * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return true;
}

int hex_digit(std::uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

constexpr std::size_t kMaxChunkLine = 1024;

// Walks a chunked body from the size line at `at`, appending the data to
// `out` when given: 1 when the last chunk was reached, 0 when more must
// arrive first (`at` then names the chunk to continue from), -1 when
// the body is malformed.
int walk_chunks(const std::vector<std::uint8_t>& raw, std::size_t& at, std::vector<std::uint8_t>* out) {
    for (;;) {
        std::size_t line = at;
        while (line + 1 < raw.size() && !(raw[line] == '\r' && raw[line + 1] == '\n')) {
            if (++line - at > kMaxChunkLine) return -1;
        }
        if (line + 1 >= raw.size()) return 0;  // the size line is not all here yet
        // Hex digits, then perhaps an extension (";name=value") to ignore.
        std::uint64_t size = 0;
        std::size_t i = at;
        for (; i < line && hex_digit(raw[i]) >= 0; ++i) {
            if (i - at >= 15) return -1;  // no chunk is that large
            size = size * 16 + static_cast<std::uint64_t>(hex_digit(raw[i]));
        }
        if (i == at || (i < line && raw[i] != ';' && raw[i] != ' ' && raw[i] != '\t')) return -1;
        const std::size_t data = line + 2;
        if (size == 0) {
            at = data;
            return 1;
        }
        if (raw.size() - data < size + 2) return 0;  // the chunk and its CRLF are not all here yet
        if (out) {
            out->insert(out->end(), raw.begin() + static_cast<std::ptrdiff_t>(data),
                        raw.begin() + static_cast<std::ptrdiff_t>(data + size));
        }
        at = data + static_cast<std::size_t>(size) + 2;
    }
}

}  // namespace

std::string http_url_for_messages(const std::string& url) {
    const std::size_t cut = url.find_first_of("?#");
    return cut == std::string::npos ? url : url.substr(0, cut) + "?...";
}

bool parse_http_url(const std::string& url, HttpUrl& out, std::string& error) {
    out = HttpUrl{};
    const std::string shown = http_url_for_messages(url);
    std::string scheme = "http://";
    if (lower(url.substr(0, 8)) == "https://") {
        scheme = "https://";
        out.tls = true;
        out.port = 443;
    } else if (lower(url.substr(0, scheme.size())) != scheme) {
        error = "only http:// and https:// addresses can be fetched: " + shown;
        return false;
    }
    std::string rest = url.substr(scheme.size());
    rest = rest.substr(0, rest.find('#'));
    const std::size_t slash = rest.find_first_of("/?");
    std::string authority = rest.substr(0, slash);
    if (slash != std::string::npos) out.path = (rest[slash] == '?' ? "/" : "") + rest.substr(slash);
    // User info is never needed, and "user@evil@host" reads differently
    // to different parsers.
    if (authority.find('@') != std::string::npos) {
        error = "an address with a user name cannot be fetched: " + shown;
        return false;
    }
    const std::size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        std::uint64_t port = 0;
        if (!parse_length(authority.substr(colon + 1), port) || port == 0 || port > 65535) {
            error = "bad port in " + shown;
            return false;
        }
        out.port = static_cast<std::uint16_t>(port);
        authority = authority.substr(0, colon);
    }
    if (authority.empty()) {
        error = "no host in " + shown;
        return false;
    }
    out.host = authority;
    return true;
}

bool http_redirect(const HttpUrl& from, const std::string& location, HttpUrl& to, std::string& error) {
    const std::string where = trim(location);
    const std::string head = lower(where.substr(0, 8));
    if (head.compare(0, 7, "http://") == 0 || head == "https://") {
        if (!parse_http_url(where, to, error)) return false;
    } else if (where.compare(0, 2, "//") == 0) {
        if (!parse_http_url((from.tls ? "https:" : "http:") + where, to, error)) return false;
    } else if (!where.empty() && (where[0] == '/' || where.find(':') == std::string::npos)) {
        // A path on the same server: from its root, or beside this one.
        to = from;
        std::string path = where.substr(0, where.find('#'));
        if (path[0] != '/') {
            const std::string base = from.path.substr(0, from.path.find('?'));
            path = base.substr(0, base.rfind('/') + 1) + path;
        }
        to.path = path;
    } else {
        error = "a redirect to an address that cannot be fetched: " + http_url_for_messages(where);
        return false;
    }
    if (from.tls && !to.tls) {
        error = from.host + " redirects from https to plain http (" + to.host + "); not followed";
        return false;
    }
    return true;
}

namespace {

std::string request_head(const char* method, const HttpUrl& url) {
    std::string host = url.host;
    if (url.port != (url.tls ? 443 : 80)) host += ":" + std::to_string(url.port);
    return std::string(method) + " " + url.path + " HTTP/1.1\r\nHost: " + host +
           "\r\nUser-Agent: RiftWii\r\nAccept: */*\r\nConnection: close\r\n";
}

}  // namespace

std::string http_get_request(const HttpUrl& url) { return request_head("GET", url) + "\r\n"; }

std::string http_post_request(const HttpUrl& url, const std::string& content_type, const std::string& body) {
    return request_head("POST", url) + "Content-Type: " + content_type +
           "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

bool http_response_complete(const std::vector<std::uint8_t>& raw, HttpReadState& reading) {
    if (reading.done) return true;
    if (!reading.decided) {
        // Resume the search a little before where the last one stopped, in
        // case the blank line straddles the two reads.
        const std::size_t end = header_end(raw, reading.scanned > 3 ? reading.scanned - 3 : 0);
        reading.scanned = raw.size();
        if (end == std::string::npos) return false;
        reading.decided = true;
        reading.body_at = end;
        HttpResponse r;
        std::string error;
        if (!parse_headers(raw, end, r, error)) return reading.done = true;  // garbage: nothing more will help
        reading.is_chunked = chunked(r);
        reading.chunk_at = end;
        const auto length = r.headers.find("content-length");
        if (!reading.is_chunked && length != r.headers.end()) {
            // A length that is not a number: parse_http_response says so.
            if (!parse_length(length->second, reading.length)) return reading.done = true;
            reading.has_length = true;
        } else if (!reading.is_chunked) {
            // Without either, the body runs to the close.
            if (r.status == 204 || r.status == 304 || (r.status >= 100 && r.status < 200)) return reading.done = true;
            return false;
        }
    }
    if (reading.is_chunked) {
        const int walked = walk_chunks(raw, reading.chunk_at, nullptr);
        return reading.done = walked != 0;
    }
    if (reading.has_length) return reading.done = raw.size() - reading.body_at >= reading.length;
    return false;
}

bool http_response_complete(const std::vector<std::uint8_t>& raw) {
    HttpReadState reading;
    return http_response_complete(raw, reading);
}

bool parse_http_response(const std::vector<std::uint8_t>& raw, HttpResponse& out, std::string& error) {
    out = HttpResponse{};
    const std::size_t end = header_end(raw);
    if (end == std::string::npos) {
        error = "the response ended inside its headers";
        return false;
    }
    if (!parse_headers(raw, end, out, error)) return false;
    if (chunked(out)) {
        std::size_t at = end;
        const int walked = walk_chunks(raw, at, &out.body);
        if (walked != 1) {
            error = walked < 0 ? "the response's chunked body is malformed" : "the response's chunked body is cut short";
            return false;
        }
        return true;
    }
    out.body.assign(raw.begin() + static_cast<std::ptrdiff_t>(end), raw.end());
    const auto length = out.headers.find("content-length");
    if (length != out.headers.end()) {
        std::uint64_t want = 0;
        if (!parse_length(length->second, want)) {
            error = "the response's Content-Length is not a number";
            return false;
        }
        if (out.body.size() < want) {
            error = "the response is cut short (" + std::to_string(out.body.size()) + " of " +
                    std::to_string(want) + " bytes)";
            return false;
        }
        out.body.resize(static_cast<std::size_t>(want));
    }
    return true;
}

std::string url_encode(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

}  // namespace riftwii
