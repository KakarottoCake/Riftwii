// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/dnsquery.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace riftwii;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    ++failures;
}

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& b, unsigned v) {
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void put32(Bytes& b, std::uint32_t v) {
    put16(b, v >> 16);
    put16(b, v & 0xFFFF);
}

void put_name(Bytes& b, const std::string& name) {
    std::size_t start = 0;
    while (start < name.size()) {
        std::size_t end = name.find('.', start);
        if (end == std::string::npos) end = name.size();
        b.push_back(static_cast<std::uint8_t>(end - start));
        for (std::size_t i = start; i < end; ++i) b.push_back(static_cast<std::uint8_t>(name[i]));
        start = end + 1;
    }
    b.push_back(0);
}

// A reply header with `answers` records, then the question for `host`.
Bytes reply_start(std::uint16_t id, unsigned flags, unsigned answers, const std::string& host) {
    Bytes b;
    put16(b, id);
    put16(b, flags);
    put16(b, 1);
    put16(b, answers);
    put16(b, 0);
    put16(b, 0);
    put_name(b, host);
    put16(b, 1);  // A
    put16(b, 1);  // IN
    return b;
}

// A record: the name as a pointer to the question's (byte 12) or in full.
void put_record_header(Bytes& b, bool pointer, const std::string& owner, unsigned type, unsigned length) {
    if (pointer) {
        put16(b, 0xC00C);
    } else {
        put_name(b, owner);
    }
    put16(b, type);
    put16(b, 1);
    put32(b, 300);
    put16(b, length);
}

constexpr unsigned kResponse = 0x8180;  // an answer, recursion wanted and available, no error

DnsReply parse(const Bytes& b, const std::string& host, std::uint16_t id, std::uint32_t& ip) {
    return parse_dns_reply(b.data(), b.size(), host, id, ip);
}

void test_query() {
    std::uint8_t out[kDnsMaxMessage];
    const std::size_t size = build_dns_query("Example.COM.", 0x1234, out, sizeof(out));
    const std::uint8_t expected[] = {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                     7,    'e',  'x',  'a',  'm',  'p',  'l',  'e',  3,    'c',  'o',  'm',
                                     0,    0x00, 0x01, 0x00, 0x01};
    expect(size == sizeof(expected), "The question has the wrong length");
    expect(size == sizeof(expected) && std::memcmp(out, expected, size) == 0,
           "The question is not a plain recursive A question for the lowercase name");

    expect(build_dns_query("", 1, out, sizeof(out)) == 0, "An empty name became a question");
    expect(build_dns_query("a..b", 1, out, sizeof(out)) == 0, "An empty label became a question");
    expect(build_dns_query(std::string(64, 'a') + ".com", 1, out, sizeof(out)) == 0, "A 64-byte label became a question");
    expect(build_dns_query(std::string(63, 'a') + ".com", 1, out, sizeof(out)) > 0, "A 63-byte label was refused");
    expect(build_dns_query("bad name.com", 1, out, sizeof(out)) == 0, "A name with a space became a question");
    expect(build_dns_query("example.com", 1, out, 20) == 0, "A question was written past the buffer");
    std::string too_long;
    for (int i = 0; i < 30; ++i) too_long += "abcdefgh.";
    expect(build_dns_query(too_long + "com", 1, out, sizeof(out)) == 0, "A name over 253 bytes became a question");
}

void test_plain_answer() {
    Bytes b = reply_start(0x4242, kResponse, 1, "example.com");
    put_record_header(b, true, "", 1, 4);
    put32(b, 0x5DB8D822);  // 93.184.216.34
    std::uint32_t ip = 0;
    expect(parse(b, "example.com", 0x4242, ip) == DnsReply::Address, "A plain answer was not read");
    expect(ip == 0x5DB8D822, "A plain answer gave the wrong address");
    ip = 0;
    expect(parse(b, "EXAMPLE.com.", 0x4242, ip) == DnsReply::Address && ip == 0x5DB8D822,
           "The question's name was compared with case or its final dot");
}

void test_alias_chain() {
    // example.com is an alias of cdn.example.net, which has the address
    // (the owner of that last record is written out, not a pointer).
    Bytes b = reply_start(7, kResponse, 2, "example.com");
    Bytes target;
    put_name(target, "cdn.example.net");
    put_record_header(b, true, "", 5, static_cast<unsigned>(target.size()));
    b.insert(b.end(), target.begin(), target.end());
    put_record_header(b, false, "CDN.example.net", 1, 4);
    put32(b, 0x0A000001);
    std::uint32_t ip = 0;
    expect(parse(b, "example.com", 7, ip) == DnsReply::Address && ip == 0x0A000001, "An alias was not followed");

    // An address for an unrelated name is not the answer.
    Bytes other = reply_start(7, kResponse, 1, "example.com");
    put_record_header(other, false, "evil.example.org", 1, 4);
    put32(other, 0x01020304);
    expect(parse(other, "example.com", 7, ip) == DnsReply::NoAddress, "An address for another name was taken");
}

void test_no_address() {
    std::uint32_t ip = 0;
    Bytes nx = reply_start(9, 0x8183, 0, "nothing.example");  // name error
    expect(parse(nx, "nothing.example", 9, ip) == DnsReply::NoAddress, "A name error was not NoAddress");
    Bytes servfail = reply_start(9, 0x8182, 0, "nothing.example");
    expect(parse(servfail, "nothing.example", 9, ip) == DnsReply::NoAddress, "A server failure was not NoAddress");
    Bytes empty = reply_start(9, kResponse, 0, "nothing.example");
    expect(parse(empty, "nothing.example", 9, ip) == DnsReply::NoAddress, "An empty answer was not NoAddress");
    // Only an AAAA record: no IPv4 address.
    Bytes v6 = reply_start(9, kResponse, 1, "nothing.example");
    put_record_header(v6, true, "", 28, 16);
    for (int i = 0; i < 16; ++i) v6.push_back(static_cast<std::uint8_t>(i));
    expect(parse(v6, "nothing.example", 9, ip) == DnsReply::NoAddress, "An IPv6-only answer gave an address");
}

void test_not_an_answer() {
    std::uint32_t ip = 0;
    Bytes good = reply_start(0x1111, kResponse, 1, "example.com");
    put_record_header(good, true, "", 1, 4);
    put32(good, 0x01020304);
    expect(parse(good, "example.com", 0x2222, ip) == DnsReply::NotAnswer, "Another transaction's answer was accepted");
    expect(parse(good, "other.example.com", 0x1111, ip) == DnsReply::NotAnswer, "An answer to another question was accepted");

    Bytes query = good;
    query[2] = 0x01;  // the response bit cleared: a question, not an answer
    query[3] = 0x00;
    expect(parse(query, "example.com", 0x1111, ip) == DnsReply::NotAnswer, "A question was read as an answer");

    for (std::size_t cut = 0; cut < good.size(); ++cut) {
        const DnsReply reply = parse_dns_reply(good.data(), cut, "example.com", 0x1111, ip);
        expect(reply != DnsReply::Address, "A cut-short answer gave an address");
    }
    expect(parse_dns_reply(nullptr, 0, "example.com", 1, ip) == DnsReply::NotAnswer, "No data was an answer");

    // A name pointer that points at itself, and one that points forward.
    Bytes loop = reply_start(5, kResponse, 1, "example.com");
    const std::size_t at = loop.size();
    loop.push_back(0xC0);
    loop.push_back(static_cast<std::uint8_t>(at));
    put16(loop, 1);
    put16(loop, 1);
    put32(loop, 1);
    put16(loop, 4);
    put32(loop, 0x01020304);
    expect(parse(loop, "example.com", 5, ip) == DnsReply::NotAnswer, "A looping name pointer was followed");

    // Data length beyond the message.
    Bytes lie = reply_start(5, kResponse, 1, "example.com");
    put_record_header(lie, true, "", 1, 200);
    put32(lie, 0x01020304);
    expect(parse(lie, "example.com", 5, ip) == DnsReply::NotAnswer, "A record past the message's end was read");

    // More than 512 bytes cannot be a plain UDP answer.
    Bytes big(kDnsMaxMessage + 1, 0);
    expect(parse(big, "example.com", 0, ip) == DnsReply::NotAnswer, "An over-long message was read");
}

Bytes config_with(std::uint8_t flags_0, std::uint8_t flags_1, std::uint8_t flags_2) {
    Bytes config(kNetConfigSize, 0);
    const std::uint8_t flags[3] = {flags_0, flags_1, flags_2};
    for (std::size_t i = 0; i < kNetConnectionCount; ++i) {
        const std::size_t base = kNetConfigHeaderSize + i * kNetConnectionSize;
        config[base] = flags[i];
        // Each connection's servers: 10.0.<i>.53 and 10.0.<i>.54.
        const std::uint32_t first = 0x0A000035u + (static_cast<std::uint32_t>(i) << 8);
        for (int byte = 0; byte < 4; ++byte) {
            config[base + 16 + byte] = static_cast<std::uint8_t>(first >> (24 - 8 * byte));
            config[base + 20 + byte] = static_cast<std::uint8_t>((first + 1) >> (24 - 8 * byte));
        }
    }
    return config;
}

void test_saved_servers() {
    // The second connection is the selected one, with its DNS typed in.
    Bytes config = config_with(0x01, 0x81, 0x01);
    std::vector<std::uint32_t> servers = saved_dns_servers(config.data(), config.size());
    expect(servers.size() == 2, "The selected connection's two servers were not found");
    if (servers.size() == 2) {
        expect(servers[0] == 0x0A000135u && servers[1] == 0x0A000136u,
               "The servers came from another connection or in the wrong order");
    }

    // DNS from the router: nothing saved to ask.
    config = config_with(0x01, 0x83, 0x01);
    expect(saved_dns_servers(config.data(), config.size()).empty(), "A DNS-from-router connection gave servers");

    // No connection selected.
    config = config_with(0x01, 0x01, 0x01);
    expect(saved_dns_servers(config.data(), config.size()).empty(), "Servers came from an unselected connection");

    // One server typed in, the other left empty, or the same twice.
    config = config_with(0x80, 0x00, 0x00);
    const std::size_t second = kNetConfigHeaderSize + 20;
    std::memset(&config[second], 0, 4);
    servers = saved_dns_servers(config.data(), config.size());
    expect(servers.size() == 1 && servers[0] == 0x0A000035u, "An empty second server was kept");
    std::memcpy(&config[second], &config[second - 4], 4);
    servers = saved_dns_servers(config.data(), config.size());
    expect(servers.size() == 1, "The same server twice was asked twice");
    // Unusable addresses: loopback and 255.255.255.255.
    config[second - 4] = 127;
    config[second] = 255;
    config[second + 1] = 255;
    config[second + 2] = 255;
    config[second + 3] = 255;
    expect(saved_dns_servers(config.data(), config.size()).empty(), "Loopback or broadcast was a DNS server");

    // A file that is not a whole settings file.
    config = config_with(0x80, 0, 0);
    expect(saved_dns_servers(config.data(), config.size() - 1).empty(), "A short settings file gave servers");
    expect(saved_dns_servers(nullptr, 0).empty(), "No settings file gave servers");
}

}  // namespace

int main() {
    test_query();
    test_plain_answer();
    test_alias_chain();
    test_no_address();
    test_not_an_answer();
    test_saved_servers();
    return failures ? 1 : 0;
}
