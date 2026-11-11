// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Looking up a host name without IOS's resolver.
//
// The Wii's own lookup (IOS, through net_gethostbyname) is all RiftWii used.
// When a connection in the Wii's Internet settings has DNS servers typed in
// by hand, that lookup can fail even though the same servers answer a
// phone or a PC. So the menu keeps those servers' addresses from the
// settings file in the NAND and, only after IOS has failed, asks them
// itself, with a plain UDP question (RFC 1035) for an IPv4 address.
//
// This file has no sockets and no NAND: it builds the question, reads the
// answer and reads the servers out of the settings file, so the host tests
// can run it. wii/online/netsock.cpp does the sending.
namespace riftwii {

// ---- The Wii's network settings file --------------------------------------
//
// /shared2/sys/net/02/config.dat: an 8-byte header, then three saved
// connections of 0x91C bytes each (0x1B5C in all). The first byte of a
// connection holds its flags, and its bytes 16 to 19 and 20 to 23 are the
// first and second DNS server, as four bytes each in network order.
constexpr const char* kNetConfigPath = "/shared2/sys/net/02/config.dat";
constexpr std::size_t kNetConfigHeaderSize = 8;
constexpr std::size_t kNetConnectionSize = 0x91C;
constexpr std::size_t kNetConnectionCount = 3;
constexpr std::size_t kNetConfigSize = kNetConfigHeaderSize + kNetConnectionCount * kNetConnectionSize;

// The connection's flag bits.
constexpr std::uint8_t kNetFlagDnsFromDhcp = 0x02;  // the DNS servers come from the router, none are typed in
constexpr std::uint8_t kNetFlagSelected = 0x80;     // the connection the Wii uses

// The DNS servers typed in for the connection the Wii uses, in host order
// and in the order the settings list them (at most two). Empty when no
// connection is selected, when that connection takes its DNS servers from
// the router (IOS already uses those), when none is filled in, or when
// `size` is smaller than a whole settings file.
std::vector<std::uint32_t> saved_dns_servers(const std::uint8_t* config, std::size_t size);

// ---- The DNS question and answer -------------------------------------------

// What a datagram from a DNS server turned out to be.
enum class DnsReply {
    Address,    // it answers the question: the address is filled in
    NoAddress,  // it answers, but the host has no IPv4 address there
    NotAnswer,  // not the answer to this question (another ID, malformed, cut short): keep waiting
};

// Largest message sent or read. Plain UDP DNS stops at 512 bytes, and no
// EDNS is asked for, so an answer never needs more.
constexpr std::size_t kDnsMaxMessage = 512;

// The question "what is the IPv4 address of `host`?" with transaction `id`
// and recursion asked for. Returns its length, or 0 when `host` is not a
// plain host name (empty, a label empty or over 63 bytes, over 253 bytes in
// all, or a character other than letters, digits, '-' and '_') or the
// question does not fit in `capacity`.
std::size_t build_dns_query(const std::string& host, std::uint16_t id, std::uint8_t* out, std::size_t capacity);

// Reads `size` bytes from a DNS server as the answer to the question built
// for `host` and `id`. Follows CNAME records (a host that is another
// name's alias) and takes the first IPv4 address of the final name. Every
// length and every name pointer in the message is checked, since the
// message comes from the network.
DnsReply parse_dns_reply(const std::uint8_t* data, std::size_t size, const std::string& host, std::uint16_t id,
                         std::uint32_t& ip);

}  // namespace riftwii
