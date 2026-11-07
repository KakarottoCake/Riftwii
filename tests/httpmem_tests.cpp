// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "httpmem.hpp"
#include "netsock.hpp"
#include "tls.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

std::uint64_t netsock_test_time = 0;

namespace {

std::string response;
std::size_t received = 0;
std::size_t read_size = 1;
std::vector<std::string> replies;
std::size_t reply_index = 0;
void* tls_memory = nullptr;
std::size_t tls_memory_bytes = 0;
int failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    ++failures;
}

void check_body(const std::string& headers, const std::string& wire_body, const std::string& body,
                std::size_t limit, bool succeeds, bool stops_early = false) {
    response = "HTTP/1.1 200 OK\r\n" + headers + "\r\n" + wire_body;
    received = 0;
    // Keep ample header room, so the body's own limit must stop the read.
    std::vector<std::uint8_t> buffer(response.size() + 8192);
    riftwii::wii::MemoryGet get;
    get.buffer = buffer.data();
    get.capacity = buffer.size();
    get.max_body = limit;
    std::string error;
    const bool ok = riftwii::wii::HttpGetInMemory("http://example.com/tag.png", get, error);
    expect(ok == succeeds, "Download did not respect its body limit");
    if (ok) {
        const std::string downloaded(reinterpret_cast<const char*>(buffer.data() + get.body_offset), get.body_length);
        expect(downloaded == body, "Download returned the wrong body");
    } else {
        expect(error.find("bytes RiftWii takes") != std::string::npos, "Download failed for the wrong reason");
    }
    if (stops_early) expect(received < response.size(), "Oversized body was read to the end before rejection");
}

void test_body_limits() {
    for (std::size_t piece : {std::size_t(1), std::size_t(7), std::size_t(4096)}) {
        read_size = piece;
        check_body("Content-Length: 4\r\n", "test", "test", 4, true);
        check_body("Content-Length: 5\r\n", "tests", "", 4, false);
        check_body("", "test", "test", 4, true);
        check_body("", "tests", "", 4, false);
        check_body("Transfer-Encoding: chunked\r\n", "2\r\nte\r\n2\r\nst\r\n0\r\n\r\n", "test", 4, true);
        check_body("Transfer-Encoding: chunked\r\n", "2\r\nte\r\n3\r\nsts\r\n0\r\n\r\n", "", 4, false);
        check_body("Transfer-Encoding: chunked\r\n", "4;extension=yes\r\ntest\r\n0\r\n\r\n", "test", 4, true);
        check_body("Transfer-Encoding: chunked\r\n", "4 \t; extension=yes\r\ntest\r\n0\r\n\r\n", "test", 4, true);
        check_body("Transfer-Encoding: chunked\r\n", "5\r\ntests\r\n0\r\n\r\n", "tests", 0, true);
    }
    read_size = 7;
    check_body("Transfer-Encoding: chunked\r\n", "1000\r\n" + std::string(4096, 'x') + "\r\n0\r\n\r\n", "", 4,
               false, true);
    check_body("Transfer-Encoding: chunked\r\n", "2\r\nte\r\n1000\r\n" + std::string(4096, 'x') + "\r\n0\r\n\r\n",
               "", 4, false, true);
    // Transfer-Encoding determines the body framing when both are present.
    check_body("Transfer-Encoding: chunked\r\nContent-Length: 0\r\n", "4\r\ntest\r\n0\r\n\r\n", "test", 4, true);
    check_body("Transfer-Encoding: chunked\r\nContent-Length: 999\r\n", "4\r\ntest\r\n0\r\n\r\n", "test", 4, true);
}

void test_release_redirects() {
    using namespace riftwii::wii;
    using riftwii::HttpRedirects;
    const std::string url = "https://github.com/KakarottoCake/Riftwii/releases/download/v2611-54/riftwii-update.pack";
    const auto download = [&](const std::string& location, HttpRedirects policy, bool succeeds) {
        replies = {"HTTP/1.1 302 Found\r\nLocation: " + location + "\r\nContent-Length: 0\r\n\r\n",
                   "HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\npack"};
        reply_index = 0;
        read_size = 7;
        std::vector<std::uint8_t> buffer(1024 + 64, 0xA5);
        MemoryGet get;
        get.buffer = buffer.data();
        get.capacity = 1024;
        get.max_body = 4;
        get.tls_block = buffer.data() + get.capacity;
        get.tls_bytes = 64;
        std::string error;
        const bool ok = HttpGetInMemory(url, get, error, 15000, nullptr, policy);
        expect(ok == succeeds, "Release download did not respect the redirect policy");
        expect(reply_index == (succeeds ? 2u : 1u), "A refused redirect was connected to");
        expect(tls_memory == get.tls_block && tls_memory_bytes == get.tls_bytes,
               "Download did not pass the reserved TLS block to the connection");
        expect(std::all_of(buffer.begin() + get.capacity, buffer.end(), [](std::uint8_t b) { return b == 0xA5; }),
               "Response bytes overwrote the reserved TLS region");
        if (ok) {
            expect(get.body_length == 4 && std::memcmp(buffer.data() + get.body_offset, "pack", 4) == 0,
                   "Redirected release body was not read into the caller's buffer");
        }
        replies.clear();
    };
    download("https://release-assets.githubusercontent.com/download", HttpRedirects::GitHubRelease, true);
    download("https://objects.githubusercontent.com/download", HttpRedirects::GitHubRelease, true);
    download("https://release-assets.githubusercontent.com/download", HttpRedirects::SameHost, false);
    download("https://example.com/download", HttpRedirects::GitHubRelease, false);
    download("http://release-assets.githubusercontent.com/download", HttpRedirects::GitHubRelease, false);
}

void test_response_storage() {
    using namespace riftwii::wii;
    const auto refused = [&](const std::string& wire, std::size_t capacity, const char* reason) {
        response = wire;
        read_size = 4096;
        std::vector<std::uint8_t> buffer(capacity + 32, 0xA5);
        MemoryGet get;
        get.buffer = buffer.data();
        get.capacity = capacity;
        get.max_body = 4;
        std::string error;
        expect(!HttpGetInMemory("http://example.com/pack", get, error), "Invalid response was accepted");
        expect(error.find(reason) != std::string::npos, "Invalid response failed for the wrong reason");
        expect(std::all_of(buffer.begin() + capacity, buffer.end(), [](std::uint8_t b) { return b == 0xA5; }),
               "HTTP receive wrote beyond the reserved response buffer");
    };
    refused("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\npack", 16, "room for");
    refused("HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\npack", 256, "Content-Length");
    refused("HTTP/1.1 200 OK\r\nX-Large: " + std::string(65536, 'x') + "\r\n\r\npack", 73728, "headers longer");
    refused("HTTP/1.1 302 Found\r\nLocation: /" + std::string(4096, 'x') + "\r\n\r\n", 8192, "redirect longer");
}

}  // namespace

namespace riftwii::wii {

bool NetStart(std::string&, bool) { return true; }

bool ResolveServer(const std::string&, std::uint16_t port, NetServer& server, std::string&) {
    server.ip = 0x08080808;
    server.port = port;
    return true;
}

std::string NetServer::label() const { return "8.8.8.8"; }
SocketTransport::~SocketTransport() = default;
bool SocketTransport::connect(const NetServer&, int, std::string&) {
    if (!replies.empty()) response = replies.at(reply_index++);
    received = 0;
    return true;
}
bool SocketTransport::send(const void*, std::size_t) { return true; }
bool SocketTransport::receive(void*, std::size_t) { return false; }

bool SocketTransport::receive_some(void* data, std::size_t max, std::size_t& got) {
    got = std::min({max, read_size, response.size() - received});
    std::memcpy(data, response.data() + received, got);
    received += got;
    return true;
}

// TLS uses the same mock wire as HTTP, while recording caller-owned state.
// No test depends on BearSSL or live network access.
struct TlsStream::State {};
void TlsStream::StateRelease::operator()(State* state) const { delete state; }
TlsStream::TlsStream() = default;
TlsStream::~TlsStream() = default;
void TlsStream::use_memory(void* memory, std::size_t bytes) {
    tls_memory = memory;
    tls_memory_bytes = bytes;
}
void TlsStream::set_deadline(std::uint64_t) {}
bool TlsStream::open(SocketTransport&, const std::string&, std::string&) { return true; }
bool TlsStream::timed_out() const { return false; }
bool TlsStream::send(const void*, std::size_t) { return true; }
bool TlsStream::receive_some(void* data, std::size_t max, std::size_t& got) {
    got = std::min({max, read_size, response.size() - received});
    std::memcpy(data, response.data() + received, got);
    received += got;
    return true;
}
int TlsStream::last_error() const { return 0; }

}  // namespace riftwii::wii

int main() {
    test_body_limits();
    test_release_redirects();
    test_response_storage();
    return failures ? 1 : 0;
}
