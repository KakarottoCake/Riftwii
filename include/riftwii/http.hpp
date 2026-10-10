// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// HTTP/1.1 GETs, for what RiftWii fetches from the internet (game titles
// from GameTDB, cheat files, the update check), and the POST that sends a
// problem report: the request, and the response as read from the
// connection. https:// goes through the Wii's
// TLS client (wii/tls.cpp); the socket loop is in wii/online.cpp.
namespace riftwii {

struct HttpUrl {
    std::string host;
    std::uint16_t port = 80;
    std::string path = "/";  // with its query
    bool tls = false;        // https
};
// "http://host[:port][/path]" or "https://...". Fails for any other scheme,
// for user info ("user@host") and for a port that is not a number. A
// fragment ("#...") is left out.
bool parse_http_url(const std::string& url, HttpUrl& out, std::string& error);

// `url` without its query or fragment, for messages and the log: a query
// may hold a key (RiiTag's "&key=").
std::string http_url_for_messages(const std::string& url);

// Where a redirect from `from` to `location` (absolute, or a path on the
// same server) goes. Fails when it would leave https for plain http.
bool http_redirect(const HttpUrl& from, const std::string& location, HttpUrl& to, std::string& error);

// The request's bytes: GET, Host, a User-Agent, Connection: close.
std::string http_get_request(const HttpUrl& url);
// A POST of `body` (Content-Type `content_type`), body included.
std::string http_post_request(const HttpUrl& url, const std::string& content_type, const std::string& body);

struct HttpResponse {
    int status = 0;
    std::map<std::string, std::string> headers;  // names lowercased
    std::vector<std::uint8_t> body;              // de-chunked
};
// Whether `raw` holds a whole response already: headers and a body of
// Content-Length bytes, or a chunked body up to its last chunk. A
// response with neither ends when the server closes the connection.
// `reading` carries what earlier calls on the same, growing `raw`
// learned, so reading a response in pieces costs time in its length,
// not its square; the one-argument form starts afresh.
struct HttpReadState {
    std::size_t body_at = 0;      // where the body starts, 0 = headers not all here
    bool decided = false;         // the headers were read
    bool done = false;
    bool is_chunked = false;
    std::uint64_t length = 0;     // Content-Length, when given
    bool has_length = false;
    std::size_t chunk_at = 0;     // chunked: the next chunk's size line
    std::size_t scanned = 0;      // bytes already searched for the headers' end
};
bool http_response_complete(const std::vector<std::uint8_t>& raw, HttpReadState& reading);
bool http_response_complete(const std::vector<std::uint8_t>& raw);
// Parses a response read to its end (or to the connection's close).
bool parse_http_response(const std::vector<std::uint8_t>& raw, HttpResponse& out, std::string& error);

// "%XX" for everything but unreserved characters, for query values.
std::string url_encode(const std::string& text);

}  // namespace riftwii
