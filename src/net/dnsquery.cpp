// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/dnsquery.hpp"

#include <algorithm>

namespace riftwii {
namespace {

// Where a connection's DNS servers sit inside its 0x91C bytes.
constexpr std::size_t kFirstDnsOffset = 16;
constexpr std::size_t kSecondDnsOffset = 20;

// A DNS message starts with a 12-byte header: ID, flags, and the number of
// questions, answers, name servers and extra records.
constexpr std::size_t kHeaderSize = 12;
constexpr std::uint16_t kFlagResponse = 0x8000;
constexpr std::uint16_t kFlagRecursionDesired = 0x0100;
constexpr unsigned kOpcodeShift = 11;
constexpr unsigned kOpcodeMask = 0xF;
constexpr unsigned kRcodeMask = 0xF;

constexpr std::uint16_t kTypeA = 1;
constexpr std::uint16_t kTypeCname = 5;
constexpr std::uint16_t kClassIn = 1;

constexpr std::size_t kMaxLabel = 63;
constexpr std::size_t kMaxName = 253;
// Pointers in a name and records in an answer are both bounded, so a
// hostile message cannot keep the parser busy.
constexpr int kMaxNamePointers = 16;
constexpr unsigned kMaxAnswers = 32;
constexpr std::size_t kMaxAliases = 8;

// Big-endian 16-bit value at `p`.
std::uint16_t read16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

// Big-endian 32-bit value at `p`.
std::uint32_t read32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

// ASCII lowercase of `c`.
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

// Host names compare without case and without a final dot.
std::string normalized(const std::string& host) {
    std::string name;
    name.reserve(host.size());
    for (char c : host) name += lower(c);
    if (!name.empty() && name.back() == '.') name.pop_back();
    return name;
}

// Whether `c` (already lowercase) may appear in a host name label.
bool name_character(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// Reads the name that starts at `pos`, as lowercase dotted text, and sets
// `next` to the first byte after it where it was written (a pointer ends a
// name in two bytes, wherever it points). A pointer may only point back, to
// a name that came earlier in the message.
bool read_name(const std::uint8_t* data, std::size_t size, std::size_t pos, std::string& name, std::size_t& next) {
    name.clear();
    std::size_t cursor = pos;
    bool jumped = false;
    int pointers = 0;
    for (;;) {
        if (cursor >= size) return false;
        const std::uint8_t length = data[cursor];
        if (length == 0) {
            if (!jumped) next = cursor + 1;
            return true;
        }
        if ((length & 0xC0) == 0xC0) {
            if (cursor + 1 >= size) return false;
            const std::size_t target = (std::size_t(length & 0x3F) << 8) | data[cursor + 1];
            if (!jumped) next = cursor + 2;
            jumped = true;
            if (++pointers > kMaxNamePointers || target >= cursor) return false;
            cursor = target;
            continue;
        }
        if ((length & 0xC0) != 0 || cursor + 1 + length > size) return false;
        if (name.size() + length + 1 > kMaxName + 1) return false;
        if (!name.empty()) name += '.';
        for (std::size_t i = 0; i < length; ++i) name += lower(static_cast<char>(data[cursor + 1 + i]));
        cursor += 1 + length;
    }
}

// An address a DNS server can sensibly have: not 0.x.x.x, loopback or
// multicast and above (a settings file with the field left empty holds 0s).
bool usable_server(std::uint32_t ip) {
    const std::uint32_t first = ip >> 24;
    return first != 0 && first != 127 && first < 224;
}

}  // namespace

std::vector<std::uint32_t> saved_dns_servers(const std::uint8_t* config, std::size_t size) {
    std::vector<std::uint32_t> servers;
    if (config == nullptr || size < kNetConfigSize) return servers;
    for (std::size_t index = 0; index < kNetConnectionCount; ++index) {
        const std::uint8_t* connection = config + kNetConfigHeaderSize + index * kNetConnectionSize;
        if ((connection[0] & kNetFlagSelected) == 0) continue;
        // Servers from the router are the ones IOS already asks.
        if ((connection[0] & kNetFlagDnsFromDhcp) != 0) return servers;
        for (std::size_t offset : {kFirstDnsOffset, kSecondDnsOffset}) {
            const std::uint32_t ip = read32(connection + offset);
            if (!usable_server(ip)) continue;
            if (std::find(servers.begin(), servers.end(), ip) == servers.end()) servers.push_back(ip);
        }
        return servers;
    }
    return servers;
}

std::size_t build_dns_query(const std::string& host, std::uint16_t id, std::uint8_t* out, std::size_t capacity) {
    const std::string name = normalized(host);
    if (name.empty() || name.size() > kMaxName) return 0;

    // The name is sent as labels, each one a length byte and its text, and
    // ends with a zero byte; then the type and class, two bytes each.
    const std::size_t needed = kHeaderSize + 1 + name.size() + 1 + 4;
    if (out == nullptr || capacity < needed) return 0;

    std::size_t at = 0;
    out[at++] = static_cast<std::uint8_t>(id >> 8);
    out[at++] = static_cast<std::uint8_t>(id & 0xFF);
    out[at++] = static_cast<std::uint8_t>(kFlagRecursionDesired >> 8);
    out[at++] = static_cast<std::uint8_t>(kFlagRecursionDesired & 0xFF);
    out[at++] = 0;  // one question
    out[at++] = 1;
    for (int i = 0; i < 6; ++i) out[at++] = 0;  // no answers, name servers or extra records

    std::size_t label_start = 0;
    while (label_start <= name.size()) {
        std::size_t label_end = name.find('.', label_start);
        if (label_end == std::string::npos) label_end = name.size();
        const std::size_t length = label_end - label_start;
        if (length == 0 || length > kMaxLabel) return 0;
        out[at++] = static_cast<std::uint8_t>(length);
        for (std::size_t i = label_start; i < label_end; ++i) {
            if (!name_character(name[i])) return 0;
            out[at++] = static_cast<std::uint8_t>(name[i]);
        }
        label_start = label_end + 1;
    }
    out[at++] = 0;
    out[at++] = static_cast<std::uint8_t>(kTypeA >> 8);
    out[at++] = static_cast<std::uint8_t>(kTypeA & 0xFF);
    out[at++] = static_cast<std::uint8_t>(kClassIn >> 8);
    out[at++] = static_cast<std::uint8_t>(kClassIn & 0xFF);
    return at;
}

DnsReply parse_dns_reply(const std::uint8_t* data, std::size_t size, const std::string& host, std::uint16_t id,
                         std::uint32_t& ip) {
    if (data == nullptr || size < kHeaderSize || size > kDnsMaxMessage) return DnsReply::NotAnswer;
    if (read16(data) != id) return DnsReply::NotAnswer;
    const std::uint16_t flags = read16(data + 2);
    if ((flags & kFlagResponse) == 0) return DnsReply::NotAnswer;
    if (((flags >> kOpcodeShift) & kOpcodeMask) != 0) return DnsReply::NotAnswer;
    const unsigned rcode = flags & kRcodeMask;
    const std::uint16_t questions = read16(data + 4);
    const std::uint16_t answers = read16(data + 6);
    if (questions != 1) return DnsReply::NotAnswer;

    // The question comes back first; it must be the one that was asked.
    const std::string wanted = normalized(host);
    std::string name;
    std::size_t pos = 0;
    if (!read_name(data, size, kHeaderSize, name, pos) || name != wanted) return DnsReply::NotAnswer;
    if (pos + 4 > size) return DnsReply::NotAnswer;
    if (read16(data + pos) != kTypeA || read16(data + pos + 2) != kClassIn) return DnsReply::NotAnswer;
    pos += 4;

    // The server answered; any code but "no error" means it has no address
    // for this name (not found, refused, its own failure).
    if (rcode != 0) return DnsReply::NoAddress;

    // An alias (CNAME) names the host that really has the address, so the
    // answers that count are those for the asked name and for its aliases.
    std::vector<std::string> names;
    names.push_back(wanted);
    const unsigned count = std::min<unsigned>(answers, kMaxAnswers);
    for (unsigned record = 0; record < count; ++record) {
        std::string owner;
        if (!read_name(data, size, pos, owner, pos)) return DnsReply::NotAnswer;
        // type, class, time to live (4 bytes) and the length of the data
        if (pos + 10 > size) return DnsReply::NotAnswer;
        const std::uint16_t type = read16(data + pos);
        const std::uint16_t record_class = read16(data + pos + 2);
        const std::size_t length = read16(data + pos + 8);
        const std::size_t data_at = pos + 10;
        if (data_at + length > size) return DnsReply::NotAnswer;
        pos = data_at + length;

        if (record_class != kClassIn) continue;
        if (std::find(names.begin(), names.end(), owner) == names.end()) continue;
        if (type == kTypeA && length == 4) {
            ip = read32(data + data_at);
            return DnsReply::Address;
        }
        if (type == kTypeCname && names.size() < kMaxAliases) {
            std::string target;
            std::size_t ignored = 0;
            if (read_name(data, size, data_at, target, ignored)) names.push_back(target);
        }
    }
    return DnsReply::NoAddress;
}

}  // namespace riftwii
