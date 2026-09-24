// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Plain HTTP/1.1 GETs, for what RiftWii fetches from the internet (game
// titles from GameTDB, cheat files): the request, and the response as
// read from the socket. No TLS: the Wii side has none, so only http://
// addresses are used. The socket loop is wii/nethttp.cpp.
namespace riftwii {

struct HttpUrl {
    std::string host;
    std::uint16_t port = 80;
    std::string path = "/";  // with its query
};
// "http://host[:port][/path]". Fails for any other scheme.
bool parse_http_url(const std::string& url, HttpUrl& out, std::string& error);

// The request's bytes: GET, Host, a User-Agent, Connection: close.
std::string http_get_request(const HttpUrl& url);

struct HttpResponse {
    int status = 0;
    std::map<std::string, std::string> headers;  // names lowercased
    std::vector<std::uint8_t> body;              // de-chunked
};
// Whether `raw` holds a whole response already: headers and a body of
// Content-Length bytes, or a chunked body up to its last chunk. A
// response with neither ends when the server closes the connection.
bool http_response_complete(const std::vector<std::uint8_t>& raw);
// Parses a response read to its end (or to the connection's close).
bool parse_http_response(const std::vector<std::uint8_t>& raw, HttpResponse& out, std::string& error);

// "%XX" for everything but unreserved characters, for query values.
std::string url_encode(const std::string& text);

}  // namespace riftwii
