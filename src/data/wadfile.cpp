// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wadfile.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace riftwii {
namespace {

constexpr std::uint32_t kWadHeaderBytes = 0x20;
constexpr std::uint64_t kWadAlign = 0x40;
// A WAD's parts are a few KB each but its contents; a channel's own
// contents are at most a few hundred MB.
constexpr std::uint64_t kMaxPartBytes = 0x10000;
constexpr std::size_t kContentMapEntryBytes = 0x1C;

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::uint64_t aligned(std::uint64_t value) { return (value + kWadAlign - 1) & ~(kWadAlign - 1); }

}  // namespace

bool parse_wad_header(const std::uint8_t* header, std::size_t size, std::uint64_t file_size, WadLayout& out,
                      std::string& error) {
    if (header == nullptr || size < kWadHeaderBytes || be32(header) != kWadHeaderBytes) {
        error = "it is not a WAD file";
        return false;
    }
    // "Is" for an ordinary title, "ib" for boot2, which is never a channel.
    if (header[4] != 'I' || header[5] != 's') {
        error = "it is not a channel's WAD (type " + std::to_string(header[4]) + "," + std::to_string(header[5]) + ")";
        return false;
    }
    WadLayout layout;
    layout.certs_size = be32(header + 0x08);
    layout.ticket_size = be32(header + 0x10);
    layout.tmd_size = be32(header + 0x14);
    layout.data_size = be32(header + 0x18);
    if (layout.certs_size > kMaxPartBytes || layout.ticket_size < kTicketBytes || layout.ticket_size > kMaxPartBytes ||
        layout.tmd_size < 0x1E4 || layout.tmd_size > kMaxPartBytes) {
        error = "its parts have sizes no WAD has";
        return false;
    }
    layout.certs_offset = aligned(kWadHeaderBytes);
    layout.ticket_offset = aligned(layout.certs_offset + layout.certs_size);
    layout.tmd_offset = aligned(layout.ticket_offset + layout.ticket_size);
    layout.data_offset = aligned(layout.tmd_offset + layout.tmd_size);
    if (layout.data_offset + layout.data_size > file_size) {
        error = "the file is shorter than its header says (cut off while copying?)";
        return false;
    }
    out = layout;
    error.clear();
    return true;
}

bool wad_content_places(const WadLayout& layout, const TmdContents& tmd, std::vector<WadContentPlace>& out,
                        std::string& error) {
    std::vector<WadContentPlace> places;
    std::uint64_t at = layout.data_offset;
    const std::uint64_t end = layout.data_offset + layout.data_size;
    for (const TmdContent& c : tmd.contents) {
        WadContentPlace place;
        place.content = c;
        place.offset = at;
        place.stored = (c.size + 15) & ~std::uint64_t(15);
        // Compared without adding to `at`: a size near 2^64 rounds to itself
        // and `at + stored` wrapped below `end` (BUG-12).
        if (c.size == 0 || place.stored < c.size || at > end || place.stored > end - at) {
            char text[64];
            std::snprintf(text, sizeof text, "content %08x runs past the WAD's data", static_cast<unsigned>(c.id));
            error = text;
            return false;
        }
        places.push_back(place);
        at = aligned(at + place.stored);
    }
    out.swap(places);
    error.clear();
    return true;
}

WadContentDecrypter::WadContentDecrypter(const std::uint8_t title_key[16], const TmdContent& content)
    : aes_(title_key), iv_{}, left_(content.size), expected_(content.sha1) {
    iv_[0] = static_cast<std::uint8_t>(content.index >> 8);
    iv_[1] = static_cast<std::uint8_t>(content.index);
}

std::size_t WadContentDecrypter::decrypt(std::uint8_t* data, std::size_t length) {
    length &= ~std::size_t(15);
    if (length == 0) return 0;
    // The next piece's IV is this piece's last encrypted block.
    std::uint8_t next_iv[16];
    std::memcpy(next_iv, data + length - 16, 16);
    aes_.cbc_decrypt(iv_, data, data, length);
    std::memcpy(iv_, next_iv, 16);
    const std::size_t own = left_ < length ? static_cast<std::size_t>(left_) : length;
    sha_.update(data, own);
    left_ -= own;
    return own;
}

bool WadContentDecrypter::matches() { return left_ == 0 && sha_.finish() == expected_; }

std::vector<SharedContent> parse_content_map(const std::uint8_t* data, std::size_t size) {
    std::vector<SharedContent> entries;
    if (data == nullptr) return entries;
    for (std::size_t at = 0; at + kContentMapEntryBytes <= size; at += kContentMapEntryBytes) {
        SharedContent entry;
        entry.name.assign(reinterpret_cast<const char*>(data + at), 8);
        if (entry.name.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) break;
        std::memcpy(entry.sha1.data(), data + at + 8, 20);
        entries.push_back(entry);
    }
    return entries;
}

std::vector<std::uint8_t> serialize_content_map(const std::vector<SharedContent>& entries) {
    std::vector<std::uint8_t> out;
    for (const SharedContent& entry : entries) {
        char name[9] = {};
        std::snprintf(name, sizeof name, "%.8s", entry.name.c_str());
        out.insert(out.end(), name, name + 8);
        out.insert(out.end(), entry.sha1.begin(), entry.sha1.end());
    }
    return out;
}

const SharedContent* find_shared_content(const std::vector<SharedContent>& entries, const Sha1Digest& sha1) {
    for (const SharedContent& entry : entries)
        if (entry.sha1 == sha1) return &entry;
    return nullptr;
}

std::string next_shared_name(const std::vector<SharedContent>& entries) {
    unsigned long next = 0;
    for (const SharedContent& entry : entries) {
        const unsigned long n = std::strtoul(entry.name.c_str(), nullptr, 16);
        if (n + 1 > next) next = n + 1;
    }
    char name[16];
    std::snprintf(name, sizeof name, "%08lx", next);
    return name;
}

}  // namespace riftwii
