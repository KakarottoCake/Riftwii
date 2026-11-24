// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <iostream>
#include <string>

#include "riftwii/http.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using riftwii::HttpChunkDecoder;
using Result = HttpChunkDecoder::Result;

namespace {

// Feeds `wire` `step` bytes at a time, as a socket might deliver it, keeping
// what the decoder did not use in front of the next bytes.
Result decode(const std::string& wire, std::size_t step, std::string& body, std::uint64_t max_body = 1u << 20) {
    HttpChunkDecoder decoder(max_body);
    std::string pending;
    body.clear();
    Result result = Result::More;
    for (std::size_t at = 0; at < wire.size() && result == Result::More; at += step) {
        pending += wire.substr(at, step);
        std::size_t used = 0;
        result = decoder.feed(reinterpret_cast<const std::uint8_t*>(pending.data()), pending.size(), used,
                              [&](const std::uint8_t* data, std::size_t size) {
                                  body.append(reinterpret_cast<const char*>(data), size);
                                  return true;
                              });
        pending.erase(0, used);
    }
    return result;
}

void test_whole_bodies() {
    const std::string wire = "5\r\nhello\r\n7;name=value\r\n, world\r\n0\r\n\r\n";
    for (std::size_t step : {1u, 2u, 3u, 7u, 64u}) {
        std::string body;
        EXPECT_TRUE(decode(wire, step, body) == Result::Done);
        EXPECT_EQ(body, std::string("hello, world"));
    }
    // Upper-case hex, and trailer fields before the end.
    std::string body;
    EXPECT_TRUE(decode("A\r\n0123456789\r\n0\r\nX-Checksum: 1\r\n\r\n", 4, body) == Result::Done);
    EXPECT_EQ(body, std::string("0123456789"));
}

void test_unfinished_bodies_ask_for_more() {
    std::string body;
    EXPECT_TRUE(decode("5\r\nhel", 2, body) == Result::More);
    EXPECT_TRUE(decode("5\r\nhello\r\n", 3, body) == Result::More);
    EXPECT_TRUE(decode("5\r\nhello\r\n0\r\n", 3, body) == Result::More);
}

void test_hostile_bodies() {
    std::string body;
    EXPECT_TRUE(decode("5\r\nhelloXX0\r\n\r\n", 1, body) == Result::Bad);     // no CRLF after the data
    EXPECT_TRUE(decode("zz\r\nhi\r\n0\r\n\r\n", 1, body) == Result::Bad);     // not hex
    EXPECT_TRUE(decode("\r\nhi\r\n0\r\n\r\n", 1, body) == Result::Bad);       // no size at all
    EXPECT_TRUE(decode("1000000000000000\r\n", 1, body) == Result::Bad);     // 16 hex digits
    EXPECT_TRUE(decode(std::string(2000, '1'), 100, body) == Result::Bad);   // a size line that never ends
    EXPECT_TRUE(decode("10\r\n0123456789abcdef\r\n0\r\n\r\n", 5, body, 8) == Result::Bad);  // past the cap
}

void test_sink_can_stop() {
    HttpChunkDecoder decoder(1024);
    const std::string wire = "3\r\nabc\r\n0\r\n\r\n";
    std::size_t used = 0;
    const Result result = decoder.feed(reinterpret_cast<const std::uint8_t*>(wire.data()), wire.size(), used,
                                       [](const std::uint8_t*, std::size_t) { return false; });
    EXPECT_TRUE(result == Result::Bad);
    EXPECT_TRUE(decoder.stopped());
}

}  // namespace

int main() {
    test_whole_bodies();
    test_unfinished_bodies_ask_for_more();
    test_hostile_bodies();
    test_sink_can_stop();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "httpchunk tests passed" << std::endl;
    return 0;
}
