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

// Where the headers end (the index after the blank line), or npos.
std::size_t header_end(const std::vector<std::uint8_t>& raw) {
    for (std::size_t i = 0; i + 3 < raw.size(); ++i) {
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

// De-chunks from `at`. `done` says whether the last chunk was seen.
bool dechunk(const std::vector<std::uint8_t>& raw, std::size_t at, std::vector<std::uint8_t>& out, bool& done) {
    done = false;
    while (at < raw.size()) {
        std::size_t line = at;
        while (line + 1 < raw.size() && !(raw[line] == '\r' && raw[line + 1] == '\n')) ++line;
        if (line + 1 >= raw.size()) return true;  // the size line is not all here yet
        const std::string size_text(raw.begin() + static_cast<std::ptrdiff_t>(at),
                                    raw.begin() + static_cast<std::ptrdiff_t>(line));
        char* end = nullptr;
        const unsigned long size = std::strtoul(size_text.c_str(), &end, 16);
        if (end == size_text.c_str()) return false;
        at = line + 2;
        if (size == 0) {
            done = true;
            return true;
        }
        if (raw.size() - at < size) {
            out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.end());
            return true;
        }
        out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(at),
                   raw.begin() + static_cast<std::ptrdiff_t>(at + size));
        at += size + 2;  // the chunk's CRLF
    }
    return true;
}

}  // namespace

bool parse_http_url(const std::string& url, HttpUrl& out, std::string& error) {
    out = HttpUrl{};
    std::string scheme = "http://";
    if (lower(url.substr(0, 8)) == "https://") {
        scheme = "https://";
        out.tls = true;
        out.port = 443;
    } else if (lower(url.substr(0, scheme.size())) != scheme) {
        error = "only http:// and https:// addresses can be fetched: " + url;
        return false;
    }
    const std::string rest = url.substr(scheme.size());
    const std::size_t slash = rest.find('/');
    std::string authority = rest.substr(0, slash);
    if (slash != std::string::npos) out.path = rest.substr(slash);
    const std::size_t colon = authority.find(':');
    if (colon != std::string::npos) {
        const int port = std::atoi(authority.c_str() + colon + 1);
        if (port <= 0 || port > 65535) {
            error = "bad port in " + url;
            return false;
        }
        out.port = static_cast<std::uint16_t>(port);
        authority = authority.substr(0, colon);
    }
    if (authority.empty()) {
        error = "no host in " + url;
        return false;
    }
    out.host = authority;
    return true;
}

std::string http_get_request(const HttpUrl& url) {
    std::string host = url.host;
    if (url.port != (url.tls ? 443 : 80)) host += ":" + std::to_string(url.port);
    return "GET " + url.path + " HTTP/1.1\r\nHost: " + host +
           "\r\nUser-Agent: RiftWii\r\nAccept: */*\r\nConnection: close\r\n\r\n";
}

bool http_response_complete(const std::vector<std::uint8_t>& raw) {
    const std::size_t end = header_end(raw);
    if (end == std::string::npos) return false;
    HttpResponse r;
    std::string error;
    if (!parse_headers(raw, end, r, error)) return true;  // garbage: nothing more will help
    if (chunked(r)) {
        std::vector<std::uint8_t> body;
        bool done = false;
        return !dechunk(raw, end, body, done) || done;
    }
    const auto length = r.headers.find("content-length");
    if (length == r.headers.end()) return r.status == 204 || r.status == 304 || (r.status >= 100 && r.status < 200);
    return raw.size() - end >= std::strtoull(length->second.c_str(), nullptr, 10);
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
        bool done = false;
        if (!dechunk(raw, end, out.body, done) || !done) {
            error = "the response's chunked body is cut short";
            return false;
        }
        return true;
    }
    out.body.assign(raw.begin() + static_cast<std::ptrdiff_t>(end), raw.end());
    const auto length = out.headers.find("content-length");
    if (length != out.headers.end()) {
        const unsigned long long want = std::strtoull(length->second.c_str(), nullptr, 10);
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
