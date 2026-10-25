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
bool SocketTransport::connect(const NetServer&, int, std::string&) { return true; }
bool SocketTransport::send(const void*, std::size_t) { return true; }
bool SocketTransport::receive(void*, std::size_t) { return false; }

bool SocketTransport::receive_some(void* data, std::size_t max, std::size_t& got) {
    got = std::min({max, read_size, response.size() - received});
    std::memcpy(data, response.data() + received, got);
    received += got;
    return true;
}

// The tests use plain HTTP; these definitions keep the production exchange's
// TLS branch linked without depending on BearSSL or live network access.
struct TlsStream::State {};
void TlsStream::StateRelease::operator()(State* state) const { delete state; }
TlsStream::TlsStream() = default;
TlsStream::~TlsStream() = default;
void TlsStream::use_memory(void*, std::size_t) {}
void TlsStream::set_deadline(std::uint64_t) {}
bool TlsStream::open(SocketTransport&, const std::string&, std::string&) { return false; }
bool TlsStream::timed_out() const { return false; }
bool TlsStream::send(const void*, std::size_t) { return false; }
bool TlsStream::receive_some(void*, std::size_t, std::size_t&) { return false; }
int TlsStream::last_error() const { return 0; }

}  // namespace riftwii::wii

int main() {
    test_body_limits();
    return failures ? 1 : 0;
}
