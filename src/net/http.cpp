// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/http.hpp"
#include "riftwii/checkedalloc.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace riftwii {
namespace {

// BearSSL error numbers from bearssl_x509.h.
constexpr int kX509InvalidValue = 33;   // BR_ERR_X509_INVALID_VALUE
constexpr int kX509Unsupported = 49;    // BR_ERR_X509_UNSUPPORTED
constexpr int kX509WrongKeyType = 51;   // BR_ERR_X509_WRONG_KEY_TYPE
constexpr int kX509BadSignature = 52;   // BR_ERR_X509_BAD_SIGNATURE
constexpr int kX509TimeUnknown = 53;    // BR_ERR_X509_TIME_UNKNOWN
constexpr int kX509Expired = 54;        // BR_ERR_X509_EXPIRED
constexpr int kX509DnMismatch = 55;     // BR_ERR_X509_DN_MISMATCH
constexpr int kX509BadServerName = 56;  // BR_ERR_X509_BAD_SERVER_NAME
constexpr int kX509NotCa = 58;          // BR_ERR_X509_NOT_CA
constexpr int kX509WeakPublicKey = 60;  // BR_ERR_X509_WEAK_PUBLIC_KEY
constexpr int kX509NotTrusted = 62;     // BR_ERR_X509_NOT_TRUSTED
constexpr int kX509RangeEnd = 63;       // "X.509 error codes are in the 32..63 range" (bearssl_x509.h)

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

bool field_name_byte(std::uint8_t c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
    switch (c) {
    case '!': case '#': case '$': case '%': case '&': case '\'': case '*':
    case '+': case '-': case '.': case '^': case '_': case '`': case '|': case '~':
        return true;
    default:
        return false;
    }
}

// Trailers are field lines, not more chunk data. They may be discarded,
// but RFC 9112 section 7.1 still requires their framing and final empty line.
int walk_trailers(const std::vector<std::uint8_t>& raw, std::size_t& at) {
    for (;;) {
        std::size_t line = at;
        while (line + 1 < raw.size() && !(raw[line] == '\r' && raw[line + 1] == '\n')) {
            if (++line - at > kMaxChunkLine) return -1;
        }
        if (line + 1 >= raw.size()) return 0;
        if (line == at) return 1;

        std::size_t colon = at;
        while (colon < line && field_name_byte(raw[colon])) ++colon;
        if (colon == at || colon == line || raw[colon] != ':') return -1;
        for (std::size_t i = colon + 1; i < line; ++i) {
            if ((raw[i] < 0x20 && raw[i] != '\t') || raw[i] == 0x7F) return -1;
        }
        at = line + 2;
    }
}

// Walks a chunked body from the size line at `at`, appending the data to
// `out` when given: 1 when the final empty line was reached, 0 when more must
// arrive first (`at` then names the chunk to continue from), -1 when
// the body is malformed. `trailer_at` resumes validated trailer lines.
int walk_chunks(const std::vector<std::uint8_t>& raw, std::size_t& at, std::vector<std::uint8_t>* out,
                std::size_t* trailer_at = nullptr) {
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
            // Keep `at` on the zero size line so later reads stay in trailer
            // mode. Resume past validated fields rather than rescanning them.
            std::size_t next_trailer = data;
            if (trailer_at && *trailer_at != 0) next_trailer = *trailer_at;
            const int walked = walk_trailers(raw, next_trailer);
            if (trailer_at) *trailer_at = next_trailer;
            return walked;
        }
        if (raw.size() - data < size + 2) return 0;  // the chunk and its CRLF are not all here yet
        const std::size_t data_end = data + static_cast<std::size_t>(size);
        if (raw[data_end] != '\r' || raw[data_end + 1] != '\n') return -1;
        if (out) {
            out->insert(out->end(), raw.begin() + static_cast<std::ptrdiff_t>(data),
                        raw.begin() + static_cast<std::ptrdiff_t>(data_end));
        }
        at = data_end + 2;
    }
}

}  // namespace

std::string http_url_for_messages(const std::string& url) {
    const std::size_t cut = url.find_first_of("?#");
    return cut == std::string::npos ? url : url.substr(0, cut) + "?...";
}

std::string tls_failure_reason(int bearssl_code, std::time_t wii_now) {
    if (bearssl_code < kX509InvalidValue || bearssl_code > kX509RangeEnd) return "";
    if (bearssl_code == kX509TimeUnknown || bearssl_code == kX509Expired) {
        char date[16];
        // The date shown is the Wii's clock as set in Wii Settings.
        std::tm clock_time = {};
        std::tm* clock_ok = ::gmtime_r(&wii_now, &clock_time);
        if (clock_ok != nullptr && std::strftime(date, sizeof(date), "%Y-%m-%d", &clock_time) != 0)
            return "the certificate is outside its valid dates; the Wii's clock says " +
                std::string(date) +
                "; if that date is wrong, set it in Wii Settings";
        return "the certificate is outside its valid dates; if the Wii's clock is wrong, "
            "set it in Wii Settings";
    }
    if (bearssl_code == kX509BadSignature || bearssl_code == kX509DnMismatch ||
            bearssl_code == kX509NotCa)
        return "the server's certificate chain does not check out; something on the network may be "
            "intercepting the connection";
    if (bearssl_code == kX509NotTrusted)
        return "the certificate's issuer is not in RiftWii's list of trusted authorities; the "
            "list may be out of date, or something on the network is intercepting the connection";
    if (bearssl_code == kX509BadServerName)
        return "the certificate is for a different server; something on the network may be "
            "redirecting the connection";
    if (bearssl_code == kX509Unsupported || bearssl_code == kX509WrongKeyType ||
            bearssl_code == kX509WeakPublicKey)
        return "the server's certificate is of a kind RiftWii cannot check";
    return "the certificate was not accepted";
}

namespace {

// A host or path goes into the request as it is: a control character or a
// space in it would end the request line early or start a header of its
// own (a redirect's Location carrying a bare LF did, SEC-06).
bool fits_request_line(const std::string& text) {
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u <= 0x20 || u == 0x7F) return false;
    }
    return true;
}

}  // namespace

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
    if (!fits_request_line(authority) || !fits_request_line(out.path)) {
        error = "an address with spaces or control characters cannot be fetched: " + shown;
        return false;
    }
    out.host = authority;
    return true;
}

bool http_redirect(const HttpUrl& from, const std::string& location, HttpUrl& to, std::string& error) {
    const std::string where = trim(location);
    const std::string head = lower(where.substr(0, 8));
    // Only the first path segment can carry a scheme. Colons after a
    // slash, query marker or fragment marker belong to the relative URL.
    const std::size_t colon = where.find(':');
    const std::size_t relative_marker = where.find_first_of("/?#");
    const bool has_scheme = colon != std::string::npos && colon < relative_marker;
    if (head.compare(0, 7, "http://") == 0 || head == "https://") {
        if (!parse_http_url(where, to, error)) return false;
    } else if (where.compare(0, 2, "//") == 0) {
        if (!parse_http_url((from.tls ? "https:" : "http:") + where, to, error)) return false;
    } else if (!where.empty() && !has_scheme) {
        // A path on the same server: from its root, or beside this one.
        to = from;
        std::string path = where.substr(0, where.find('#'));
        // Without a new path, a query replaces only the old query and a
        // fragment keeps the whole request target (fragments are not sent).
        if (path.empty()) {
            path = from.path;
        } else if (path[0] == '?') {
            path = from.path.substr(0, from.path.find('?')) + path;
        } else if (path[0] != '/') {
            const std::string base = from.path.substr(0, from.path.find('?'));
            path = base.substr(0, base.rfind('/') + 1) + path;
        }
        if (!fits_request_line(path)) {
            error = "a redirect to a path with spaces or control characters: not followed";
            return false;
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

// A host on the player's own network or the Wii itself, written as such:
// "localhost", or an IPv4 address in 0/8, 10/8, 127/8, 169.254/16,
// 172.16/12 or 192.168/16. A name that only resolves to one is not seen.
bool local_host(const std::string& name) {
    const std::string host = lower(name);
    if (host == "localhost") return true;
    unsigned parts[4];
    std::size_t at = 0;
    for (int i = 0; i < 4; ++i) {
        if (at >= host.size() || host[at] < '0' || host[at] > '9') return false;
        unsigned value = 0;
        std::size_t digits = 0;
        while (at < host.size() && host[at] >= '0' && host[at] <= '9' && digits < 4) {
            value = value * 10 + static_cast<unsigned>(host[at] - '0');
            ++at;
            ++digits;
        }
        if (value > 255) return false;
        parts[i] = value;
        if (i < 3) {
            if (at >= host.size() || host[at] != '.') return false;
            ++at;
        }
    }
    if (at != host.size()) return false;
    return http_local_ipv4(parts[0] << 24 | parts[1] << 16 | parts[2] << 8 | parts[3]);
}

}  // namespace

bool http_local_ipv4(std::uint32_t ip) {
    const unsigned a = ip >> 24, b = (ip >> 16) & 0xFF;
    return a == 0 || a == 10 || a == 127 || (a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) ||
           (a == 192 && b == 168);
}

bool http_redirect_allowed(HttpRedirects policy, const HttpUrl& from, const HttpUrl& to) {
    switch (policy) {
    // Anywhere, but not from the internet into the player's own network:
    // a server out there must not make the Wii fetch from the router or a
    // PC at home (CAV-04, SEC-06). A local server may still send it on.
    case HttpRedirects::Follow: return local_host(from.host) || !local_host(to.host);
    case HttpRedirects::Never: return false;
    case HttpRedirects::SameHost: break;
    case HttpRedirects::GitHubRelease: {
        const std::string host = lower(to.host);
        return to.tls && to.port == 443 &&
               (host == "github.com" || host == "release-assets.githubusercontent.com" ||
                host == "objects.githubusercontent.com");
    }
    }
    // Host names are not case-sensitive; parse_http_url keeps them as written.
    return lower(from.host) == lower(to.host) && from.port == to.port && from.tls == to.tls;
}

namespace {

std::string request_head(const char* method, const HttpUrl& url, const std::string& user_agent) {
    std::string host = url.host;
    if (url.port != (url.tls ? 443 : 80)) host += ":" + std::to_string(url.port);
    return std::string(method) + " " + url.path + " HTTP/1.1\r\nHost: " + host +
           "\r\nUser-Agent: " + user_agent + "\r\nAccept: */*\r\nConnection: close\r\n";
}

}  // namespace

std::string http_get_request(const HttpUrl& url, const std::string& user_agent) {
    return request_head("GET", url, user_agent) + "\r\n";
}

std::string http_head_request(const HttpUrl& url, const std::string& user_agent) {
    return request_head("HEAD", url, user_agent) + "\r\n";
}

std::string http_post_request(const HttpUrl& url, const std::string& content_type, const std::string& body) {
    return request_head("POST", url, "RiftWii") + "Content-Type: " + content_type +
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
        if (reading.head) return reading.done = true;  // a HEAD's answer has no body
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
        const int walked = walk_chunks(raw, reading.chunk_at, nullptr, &reading.trailer_at);
        return reading.done = walked != 0;
    }
    if (reading.has_length) return reading.done = raw.size() - reading.body_at >= reading.length;
    return false;
}

bool http_response_complete(const std::vector<std::uint8_t>& raw) {
    HttpReadState reading;
    return http_response_complete(raw, reading);
}

bool parse_http_response(const std::vector<std::uint8_t>& raw, HttpResponse& out, std::string& error, bool head) {
    out = HttpResponse{};
    const std::size_t end = header_end(raw);
    if (end == std::string::npos) {
        error = "the response ended inside its headers";
        return false;
    }
    if (!parse_headers(raw, end, out, error)) return false;
    if (head) return true;
    // The body is at most what is left after the headers: room for that
    // first, so the copy below cannot fail halfway.
    if (!try_reserve(out.body, raw.size() - end)) {
        error = "out of memory for the response";
        return false;
    }
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

namespace {

bool url_unreserved(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

}  // namespace

std::size_t url_encoded_size(const std::string& text) {
    std::size_t size = 0;
    for (unsigned char c : text) size += url_unreserved(c) ? 1 : 3;
    return size;
}

void url_encode_append(const std::string& text, std::string& out) {
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : text) {
        if (url_unreserved(c)) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
}

std::string url_encode(const std::string& text) {
    std::string out;
    url_encode_append(text, out);
    return out;
}

}  // namespace riftwii
