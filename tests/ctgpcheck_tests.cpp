// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// CTGP-R's update check and download (riftwii/ctgpcheck.hpp), against a
// scripted server built from synthetic bytes.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpcheck.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

void put32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    out[at] = std::uint8_t(value >> 24);
    out[at + 1] = std::uint8_t(value >> 16);
    out[at + 2] = std::uint8_t(value >> 8);
    out[at + 3] = std::uint8_t(value);
}

// A reply of the given kind with made-up signature bytes.
std::vector<std::uint8_t> make_reply(bool update, std::uint32_t update_bytes) {
    const std::uint32_t length = update ? 0x52 : 0x56;
    std::vector<std::uint8_t> r(0x38 + length, 0xEE);
    put32(r, 0, length);
    put32(r, 4, 0x2F);
    put32(r, 0x4C, 1800000000u);
    r[0x50] = 5;
    r[0x51] = 1;
    put32(r, 0x52, update ? update_bytes : 0x1234);
    put32(r, 0x56, update ? 0x2F : 0x56);
    return r;
}

// A server that answers from a script and records what it was sent. It
// hands out at most `chunk` bytes per receive, to test reassembly.
struct ScriptedServer {
    std::vector<std::uint8_t> script;
    std::size_t at = 0;
    std::size_t chunk = 7;
    std::vector<std::uint8_t> sent;

    CtgpCheckIo io() {
        CtgpCheckIo io;
        io.send = [this](const std::uint8_t* d, std::size_t n) {
            sent.insert(sent.end(), d, d + n);
            return true;
        };
        io.receive_some = [this](std::uint8_t* out, std::size_t max, std::size_t& got) {
            got = std::min({max, chunk, script.size() - at});
            std::memcpy(out, script.data() + at, got);
            at += got;
            return true;
        };
        return io;
    }
};

std::vector<CtgpPackage> sample_records() {
    std::vector<CtgpPackage> disk = {{"ZETA", 0x01000002, 0x0A000000}, {"ALPHA", 0x00040250, 0x0A000000},
                                     {"SD", 0x00030010, 0x08000000}};
    std::vector<CtgpPackage> hash = {{"HASH", 0x01031374, 0x0B000000}, {"RMC", 0x01000000, 0x09000000},
                                     {"ALPHA", 0x99999999, 0x0A000000}};
    return ctgp_check_records(disk, hash);
}

void test_records() {
    const auto records = sample_records();
    EXPECT_EQ(records.size(), 5u);
    EXPECT_EQ(records[0].name, "ALPHA");
    EXPECT_EQ(records[0].version, 0x00040250u);  // the package disk's, not the duplicate
    EXPECT_EQ(records[1].name, "HASH");
    EXPECT_EQ(records[2].name, "RMC");
    EXPECT_EQ(records[3].name, "SD");
    EXPECT_EQ(records[4].name, "ZETA");
}

void test_request() {
    std::array<std::uint8_t, 6> mac = {1, 2, 3, 4, 5, 6};
    auto records = sample_records();
    records[1].flags = 0x0B00FFFF;  // only the type byte goes out
    std::vector<std::uint8_t> out;
    std::string error;
    EXPECT_TRUE(ctgp_check_request(mac, records, out, error));
    EXPECT_EQ(out.size(), (records.size() + 9) * 16);
    EXPECT_TRUE(std::memcmp(out.data(), "CTG0", 4) == 0);
    EXPECT_EQ(be32(out.data() + 4), kCtgpCheckClientVersion);
    EXPECT_EQ(out[8], 0);
    EXPECT_EQ(out[9], records.size());
    EXPECT_TRUE(std::memcmp(out.data() + 10, mac.data(), 6) == 0);
    // The 0x80-byte block is all zeros.
    for (std::size_t i = 0x10; i < 0x90; ++i) EXPECT_EQ(out[i], 0);
    EXPECT_TRUE(std::memcmp(out.data() + 0x90, "ALPHA\0\0\0", 8) == 0);
    EXPECT_EQ(be32(out.data() + 0x98), 0x00040250u);
    EXPECT_EQ(be32(out.data() + 0x9C), 0x0A000000u);
    EXPECT_TRUE(std::memcmp(out.data() + 0xA0, "HASH\0\0\0\0", 8) == 0);
    EXPECT_EQ(be32(out.data() + 0xAC), 0x0B000000u);

    EXPECT_TRUE(!ctgp_check_request(mac, {}, out, error));
}

void test_reply() {
    CtgpCheckReply reply;
    std::string error;
    auto r = make_reply(true, 12345678);
    EXPECT_TRUE(ctgp_check_parse_reply(r.data(), r.size(), reply, error));
    EXPECT_TRUE(reply.update_waiting);
    EXPECT_EQ(reply.update_bytes, 12345678u);
    EXPECT_EQ(reply.status, 5);
    EXPECT_EQ(reply.timestamp, 1800000000u);

    r = make_reply(false, 0);
    EXPECT_TRUE(ctgp_check_parse_reply(r.data(), r.size(), reply, error));
    EXPECT_TRUE(!reply.update_waiting);

    // Wrong length word, truncated, too-long signature, an oversized update.
    r = make_reply(true, 100);
    put32(r, 0, 0x60);
    EXPECT_TRUE(!ctgp_check_parse_reply(r.data(), r.size(), reply, error));
    r = make_reply(true, 100);
    EXPECT_TRUE(!ctgp_check_parse_reply(r.data(), r.size() - 1, reply, error));
    r = make_reply(true, 100);
    put32(r, 4, 0x80);
    EXPECT_TRUE(!ctgp_check_parse_reply(r.data(), r.size(), reply, error));
    r = make_reply(true, 100);
    put32(r, 0x56, 0x40);
    EXPECT_TRUE(!ctgp_check_parse_reply(r.data(), r.size(), reply, error));
    r = make_reply(true, kCtgpCheckMaxUpdateBytes + 1);
    EXPECT_TRUE(!ctgp_check_parse_reply(r.data(), r.size(), reply, error));
}

void test_exchange_and_download() {
    std::vector<std::uint8_t> payload(100000);
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = std::uint8_t(i * 7 + (i >> 9));

    ScriptedServer server;
    server.script = make_reply(true, std::uint32_t(payload.size()));
    server.script.insert(server.script.end(), payload.begin(), payload.end());
    server.chunk = 4093;
    const CtgpCheckIo io = server.io();

    std::vector<std::uint8_t> request = {'C', 'T', 'G', '0'};
    CtgpCheckReply reply;
    std::string error;
    EXPECT_TRUE(ctgp_check_exchange(io, request, reply, error));
    EXPECT_TRUE(reply.update_waiting);
    EXPECT_EQ(reply.update_bytes, payload.size());

    std::vector<std::uint8_t> saved;
    std::uint64_t last = 0;
    EXPECT_TRUE(ctgp_download(
        io, reply.update_bytes,
        [&](const std::uint8_t* d, std::size_t n) {
            EXPECT_TRUE(n <= 1000);
            saved.insert(saved.end(), d, d + n);
            return true;
        },
        [&](std::uint64_t done, std::uint64_t total) {
            EXPECT_EQ(total, payload.size());
            last = done;
            return true;
        },
        error, 1000));
    EXPECT_EQ(saved, payload);
    EXPECT_EQ(last, payload.size());
    // Sent: the request, the download request, the goodbye.
    EXPECT_EQ(server.sent.size(), 4u + 8u + 8u);
    EXPECT_EQ(be32(server.sent.data() + 4), 0u);
    EXPECT_EQ(be32(server.sent.data() + 8), payload.size());
    EXPECT_EQ(be32(server.sent.data() + 12), 0u);
    EXPECT_EQ(be32(server.sent.data() + 16), 0u);
}

void test_download_failures() {
    std::string error;
    // The server closes before the end.
    ScriptedServer server;
    server.script.assign(500, 0xAB);
    std::vector<std::uint8_t> saved;
    const auto keep = [&](const std::uint8_t* d, std::size_t n) {
        saved.insert(saved.end(), d, d + n);
        return true;
    };
    EXPECT_TRUE(!ctgp_download(server.io(), 1000, keep, CtgpDownloadProgress(), error));
    EXPECT_EQ(saved.size(), 500u);

    // The sink fails (card full).
    ScriptedServer full;
    full.script.assign(500, 0xAB);
    EXPECT_TRUE(!ctgp_download(full.io(), 500, [](const std::uint8_t*, std::size_t) { return false; },
                               CtgpDownloadProgress(), error));

    // Cancelled from the progress callback.
    ScriptedServer cancel;
    cancel.script.assign(500, 0xAB);
    EXPECT_TRUE(!ctgp_download(cancel.io(), 500, keep, [](std::uint64_t, std::uint64_t) { return false; }, error));

    // Out-of-range sizes are refused before anything is sent.
    ScriptedServer none;
    EXPECT_TRUE(!ctgp_download(none.io(), 0, keep, CtgpDownloadProgress(), error));
    EXPECT_TRUE(!ctgp_download(none.io(), kCtgpCheckMaxUpdateBytes + 1, keep, CtgpDownloadProgress(), error));
    EXPECT_TRUE(none.sent.empty());

    // A reply cut short.
    ScriptedServer cut;
    cut.script = make_reply(true, 10);
    cut.script.resize(20);
    CtgpCheckReply reply;
    EXPECT_TRUE(!ctgp_check_exchange(cut.io(), {'C'}, reply, error));
}

}  // namespace

int main() {
    test_records();
    test_request();
    test_reply();
    test_exchange_and_download();
    test_download_failures();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "ctgpcheck: all tests passed" << std::endl;
    return 0;
}
