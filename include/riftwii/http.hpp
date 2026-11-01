// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <vector>

// HTTP/1.1 GETs, for what RiftWii fetches from the internet (game titles
// from GameTDB, cheat files, the update check), and the POST that sends a
// problem report: the request, and the response as read from the
// connection. https:// goes through the Wii's
// TLS client (wii/online/tls.cpp); the socket loop is in wii/online/online.cpp.
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

// Which redirects a GET follows. Follow: any http_redirect allows (most
// downloads: GitHub's releases redirect to its file servers). SameHost:
// only to the same server (host, port and scheme). Never: none; a 3xx is
// an answer like a 404, for a download that must come from the server
// asked and from the address asked (a GameCube banner: a redirect to
// another game's banner would show the wrong game). GitHubRelease: over
// https only, to github.com or the servers GitHub keeps release files on
// (release-assets.githubusercontent.com, objects.githubusercontent.com),
// for RiftWii's own updates (SEC-03).
enum class HttpRedirects { Follow, SameHost, Never, GitHubRelease };
// An IPv4 address (host order) on the player's own network or the Wii
// itself: 0/8, 10/8, 127/8, 169.254/16, 172.16/12, 192.168/16. A redirect
// from the internet must not lead there, named or by a name that resolves
// there (CAV-04).
bool http_local_ipv4(std::uint32_t ip);
// Whether `policy` lets a GET go on from `from` to `to`.
bool http_redirect_allowed(HttpRedirects policy, const HttpUrl& from, const HttpUrl& to);

// The request's bytes: GET, Host, a User-Agent, Connection: close. The
// User-Agent is "RiftWii" unless the caller names more (GameHacking.org asks
// to be told which program and version is asking); it is RiftWii's own
// text, never from the network.
std::string http_get_request(const HttpUrl& url, const std::string& user_agent = "RiftWii");
// The same as a HEAD request: the answer's status and headers, no body.
std::string http_head_request(const HttpUrl& url, const std::string& user_agent = "RiftWii");
// A POST of `body` (Content-Type `content_type`), body included.
std::string http_post_request(const HttpUrl& url, const std::string& content_type, const std::string& body);

struct HttpResponse {
    int status = 0;
    std::map<std::string, std::string> headers;  // names lowercased
    std::vector<std::uint8_t> body;              // de-chunked
};
// Whether `raw` holds a whole response already: headers and a body of
// Content-Length bytes, or a chunked body through its trailers' final empty
// line. A response with neither ends when the server closes the connection.
// Also stops on malformed framing: parse_http_response reports the error.
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
    std::size_t trailer_at = 0;   // chunked: the next trailer line, 0 = not reached
    std::size_t scanned = 0;      // bytes already searched for the headers' end
    bool head = false;            // set by the caller: a HEAD's answer ends with its headers
};
bool http_response_complete(const std::vector<std::uint8_t>& raw, HttpReadState& reading);
bool http_response_complete(const std::vector<std::uint8_t>& raw);
// Parses a response read to its end (or to the connection's close).
// `head`: the answer to a HEAD request, whose Content-Length describes a
// body that is never sent; only its status and headers are read.
bool parse_http_response(const std::vector<std::uint8_t>& raw, HttpResponse& out, std::string& error,
                         bool head = false);

// Why a TLS handshake failed, from BearSSL's error number and the Wii's
// clock at verification: empty when the certificate was not the problem.
std::string tls_failure_reason(int bearssl_code, std::time_t wii_now);

// "%XX" for everything but unreserved characters, for query values.
std::string url_encode(const std::string& text);
// The same appended to `out`, and its length beforehand, so a big text's
// room can be checked before it is encoded (wii/online/reportsend.cpp).
void url_encode_append(const std::string& text, std::string& out);
std::size_t url_encoded_size(const std::string& text);

}  // namespace riftwii
