// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wadfile.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "riftwii/checkedalloc.hpp"

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
        if (!try_push_back(places, place)) {
            error = "not enough memory for the WAD's content list";
            return false;
        }
        at = aligned(at + place.stored);
    }
    out.swap(places);
    error.clear();
    return true;
}

bool read_wad_metadata(const ByteSource& source, WadMetadata& out, std::string& error) {
    std::uint8_t header[0x20];
    WadMetadata result;
    if (source.size() >= 0x7FFFFFFF || !source.read(0, header, sizeof header)) {
        error = "the WAD header could not be read, or the file is too large";
        return false;
    }
    if (!parse_wad_header(header, sizeof header, source.size(), result.layout, error)) return false;
    const WadLayout& layout = result.layout;
    if (!try_resize(result.ticket, static_cast<std::size_t>(layout.ticket_size)) ||
        !try_resize(result.tmd_bytes, static_cast<std::size_t>(layout.tmd_size))) {
        error = "not enough memory for the WAD's metadata";
        return false;
    }
    if (!source.read(layout.ticket_offset, result.ticket.data(), result.ticket.size()) ||
        !source.read(layout.tmd_offset, result.tmd_bytes.data(), result.tmd_bytes.size())) {
        error = "the WAD's ticket or TMD could not be read";
        return false;
    }
    // Both structures use RSA-2048 signed headers. Checking the shape is
    // separate from signature verification; homemade VC injects are valid.
    if (be32(result.ticket.data()) != 0x10001 || be32(result.tmd_bytes.data()) != 0x10001 ||
        result.ticket[0x1BC] != 0 || result.tmd_bytes[0x180] != 0 ||
        std::memcmp(result.ticket.data() + 0x1DC, result.tmd_bytes.data() + 0x18C, 8) != 0) {
        error = "the WAD's ticket and TMD do not describe the same supported title";
        return false;
    }
    if (!parse_tmd_contents(result.tmd_bytes.data(), result.tmd_bytes.size(), result.tmd, error)) return false;
    if ((result.tmd.title_id >> 32) != kDownloadedChannels || channel_code(result.tmd.title_id).empty() ||
        result.tmd.contents.empty() || result.tmd.contents.size() > 512 ||
        tmd_content_by_index(result.tmd, result.tmd.boot_index) == nullptr) {
        error = "the WAD does not describe a channel with a program";
        return false;
    }
    for (std::size_t i = 0; i < result.tmd.contents.size(); ++i) {
        const TmdContent& content = result.tmd.contents[i];
        if (content.type != 1 && content.type != 0x4001 && content.type != 0x8001) {
            error = "the WAD has an unsupported content type";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (content.id == result.tmd.contents[j].id || content.index == result.tmd.contents[j].index) {
                error = "the WAD repeats a content ID or index";
                return false;
            }
        }
    }
    if (!wad_content_places(layout, result.tmd, result.places, error)) return false;
    Sha1 hash;
    hash.update(header, sizeof header);
    hash.update(result.ticket.data(), result.ticket.size());
    hash.update(result.tmd_bytes.data(), result.tmd_bytes.size());
    std::uint8_t size_bytes[8];
    for (unsigned i = 0; i < 8; ++i) size_bytes[i] = static_cast<std::uint8_t>(source.size() >> (56 - i * 8));
    hash.update(size_bytes, sizeof size_bytes);
    result.fingerprint = hash.finish();
    out = std::move(result);
    return true;
}

bool read_wad_banner(const ByteSource& source, const WadMetadata& metadata,
                     const std::uint8_t common_key[16], std::vector<std::uint8_t>& out, std::string& error) {
    std::uint8_t title_key[16] = {};
    const bool decrypted = decrypt_title_key(metadata.ticket.data(), metadata.ticket.size(), common_key, title_key, error);
    const bool read = decrypted && read_wad_banner_with_title_key(source, metadata, title_key, out, error);
    clear_wad_key(title_key, sizeof title_key);
    return read;
}

void clear_wad_key(std::uint8_t* bytes, std::size_t size) {
    volatile std::uint8_t* destination = bytes;
    for (std::size_t i = 0; i < size; ++i) destination[i] = 0;
}

namespace {

// The key sources in order; the first whose key passes `check` wins.
bool select_checked_key(const WadTitleKeyReader& read_key, std::uint8_t title_key[16], WadKeySource& selected,
                        const std::function<bool(const std::uint8_t*, std::string&)>& check,
                        const char* mismatch, std::string& error) {
    clear_wad_key(title_key, 16);
    bool had_candidate = false;
    for (WadKeySource candidate : {WadKeySource::Otp, WadKeySource::Ios, WadKeySource::BootMiiKeys}) {
        error.clear();
        if (read_key(candidate, title_key)) {
            had_candidate = true;
            if (check(title_key, error)) {
                selected = candidate;
                return true;
            }
        }
        clear_wad_key(title_key, 16);
    }
    error = had_candidate ? mismatch : "no title key could be read from this Wii, IOS or sd:/keys.bin";
    return false;
}

}  // namespace

bool select_wad_title_key(const ByteSource& source, const WadMetadata& metadata,
                           const WadTitleKeyReader& read_key, std::uint8_t title_key[16],
                           WadKeySource& selected, std::vector<std::uint8_t>& banner, std::string& error) {
    banner.clear();
    return select_checked_key(read_key, title_key, selected,
        [&](const std::uint8_t* key, std::string& why) {
            return read_wad_banner_with_title_key(source, metadata, key, banner, why);
        }, "the WAD's banner did not match any available key or could not be read", error);
}

bool verify_wad_title_key(const ByteSource& source, const WadMetadata& metadata,
                          const std::uint8_t title_key[16], std::string& error) {
    // Any content proves the key through its TMD hash; the smallest one is
    // the least to read. A channel WAD may have no banner, or a large one.
    const WadContentPlace* smallest = nullptr;
    for (const WadContentPlace& place : metadata.places) {
        if (place.content.size > (4u << 20)) continue;
        if (!smallest || place.content.size < smallest->content.size) smallest = &place;
    }
    if (!smallest) {
        error = "the WAD has no content small enough to check its key";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    if (!try_resize(bytes, static_cast<std::size_t>(smallest->stored))) {
        error = "not enough memory to check the WAD's key";
        return false;
    }
    if (!source.read(smallest->offset, bytes.data(), bytes.size())) {
        error = "the WAD could not be read";
        return false;
    }
    WadContentDecrypter decrypter(title_key, smallest->content);
    decrypter.decrypt(bytes.data(), bytes.size());
    if (!decrypter.matches()) {
        error = "the WAD is damaged or uses another key";
        return false;
    }
    return true;
}

bool select_wad_install_key(const ByteSource& source, const WadMetadata& metadata,
                            const WadTitleKeyReader& read_key, std::uint8_t title_key[16],
                            WadKeySource& selected, std::string& error) {
    return select_checked_key(read_key, title_key, selected,
        [&](const std::uint8_t* key, std::string& why) {
            return verify_wad_title_key(source, metadata, key, why);
        }, "the WAD did not match any available key or could not be read", error);
}

bool read_wad_banner_with_title_key(const ByteSource& source, const WadMetadata& metadata,
                                    const std::uint8_t title_key[16], std::vector<std::uint8_t>& out,
                                    std::string& error) {
    for (const WadContentPlace& place : metadata.places) {
        if (place.content.index != 0) continue;
        if (place.content.size > (4u << 20)) break;
        WadContentDecrypter decrypter(title_key, place.content);
        std::vector<std::uint8_t> bytes;
        if (!try_resize(bytes, static_cast<std::size_t>(place.stored))) {
            error = "not enough memory for the WAD's banner";
            return false;
        }
        if (!source.read(place.offset, bytes.data(), bytes.size())) {
            error = "the WAD's banner could not be read";
            return false;
        }
        const std::size_t own = decrypter.decrypt(bytes.data(), bytes.size());
        if (!decrypter.matches()) {
            error = "the WAD's banner is damaged or uses another key";
            return false;
        }
        bytes.resize(own);
        out.swap(bytes);
        return true;
    }
    error = "the WAD has no banner of a supported size";
    return false;
}

bool encode_wad_cache(const WadCacheRecord& record, std::vector<std::uint8_t>& out) {
    if (record.banner_head.size() > 0x700 || record.banner_size > (4u << 20) ||
        record.banner_head.size() > record.banner_size) return false;
    std::vector<std::uint8_t> bytes;
    if (!try_resize(bytes, 52 + record.banner_head.size() + 20)) return false;
    std::memcpy(bytes.data(), "RWWADC01", 8);
    std::memcpy(bytes.data() + 8, record.fingerprint.data(), 20);
    for (unsigned i = 0; i < 8; ++i) {
        bytes[28 + i] = static_cast<std::uint8_t>(record.modified >> (56 - i * 8));
        bytes[36 + i] = static_cast<std::uint8_t>(record.title_id >> (56 - i * 8));
    }
    for (unsigned i = 0; i < 4; ++i) {
        bytes[44 + i] = static_cast<std::uint8_t>(record.banner_size >> (24 - i * 8));
        bytes[48 + i] = static_cast<std::uint8_t>(record.banner_head.size() >> (24 - i * 8));
    }
    std::copy(record.banner_head.begin(), record.banner_head.end(), bytes.begin() + 52);
    const Sha1Digest hash = sha1(bytes.data(), bytes.size() - 20);
    std::copy(hash.begin(), hash.end(), bytes.end() - 20);
    out.swap(bytes);
    return true;
}

bool decode_wad_cache(const std::uint8_t* data, std::size_t size, WadCacheRecord& out) {
    if (data == nullptr || size < 72 || size > 72 + 0x700 || std::memcmp(data, "RWWADC01", 8) != 0) return false;
    const std::uint32_t head_size = be32(data + 48);
    if (size != 72 + head_size) return false;
    const Sha1Digest hash = sha1(data, size - 20);
    if (std::memcmp(hash.data(), data + size - 20, 20) != 0) return false;
    WadCacheRecord record;
    std::copy_n(data + 8, 20, record.fingerprint.begin());
    for (unsigned i = 0; i < 8; ++i) {
        record.modified = (record.modified << 8) | data[28 + i];
        record.title_id = (record.title_id << 8) | data[36 + i];
    }
    record.banner_size = be32(data + 44);
    if ((record.title_id >> 32) != kDownloadedChannels || record.banner_size == 0 ||
        record.banner_size > (4u << 20) || head_size > record.banner_size ||
        !try_assign_range(record.banner_head, data + 52, data + 52 + head_size)) return false;
    out = std::move(record);
    return true;
}

bool wad_needs_preparation(const Sha1Digest& source, const Sha1Digest* prepared,
                            bool contents_present, bool same_installed_tmd) {
    if (!contents_present) return true;
    if (prepared != nullptr) return *prepared != source;
    return !same_installed_tmd;
}

bool add_wad_title_uid(std::vector<std::uint8_t>& bytes, std::uint64_t title_id, std::string& error) {
    constexpr std::size_t kMaxUidBytes = 12 * 8192;
    if (bytes.size() % 12 != 0 || bytes.size() > kMaxUidBytes) {
        error = "the emulated NAND's title ownership list is damaged";
        return false;
    }
    // A new NAND starts with the Wii Menu, as described by WiiBrew's
    // /sys/uid.sys format. No console data is needed to make that entry.
    if (bytes.empty()) {
        if (!try_resize(bytes, 12)) {
            error = "not enough memory for the title ownership list";
            return false;
        }
        bytes[3] = 1;
        bytes[7] = 2;
        bytes[10] = 0x10;
    }
    bool found = false;
    std::uint32_t highest = 0x1000;
    for (std::size_t at = 0; at < bytes.size(); at += 12) {
        const std::uint64_t title = (std::uint64_t(be32(bytes.data() + at)) << 32) | be32(bytes.data() + at + 4);
        const std::uint32_t uid = be32(bytes.data() + at + 8);
        found = found || title == title_id;
        highest = std::max(highest, uid);
    }
    if (found) return true;
    if (highest == UINT32_MAX || bytes.size() == kMaxUidBytes) {
        error = "the emulated NAND's title ownership list is full";
        return false;
    }
    const std::size_t at = bytes.size();
    if (!try_resize(bytes, at + 12)) {
        error = "not enough memory for the title ownership list";
        return false;
    }
    for (unsigned i = 0; i < 8; ++i) bytes[at + i] = static_cast<std::uint8_t>(title_id >> (56 - i * 8));
    for (unsigned i = 0; i < 4; ++i) bytes[at + 8 + i] = static_cast<std::uint8_t>((highest + 1) >> (24 - i * 8));
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
    if (size > (1u << 20) || !try_reserve(entries, size / kContentMapEntryBytes)) return entries;
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
    if (entries.size() > (1u << 20) / kContentMapEntryBytes ||
        !try_reserve(out, entries.size() * kContentMapEntryBytes)) return out;
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
