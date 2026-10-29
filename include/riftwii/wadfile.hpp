// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/nandchannel.hpp"
#include "riftwii/overlay.hpp"
#include "riftwii/sha1.hpp"
#include "riftwii/wiicrypt.hpp"

// .wad files (a channel as the Wii Shop delivered it, for installing into
// an emulated NAND on the SD card), from the public pages
// https://wiibrew.org/wiki/WAD_files, https://wiibrew.org/wiki/Ticket and
// https://wiibrew.org/wiki/Title_metadata. Plain data, host-tested;
// wii/storage/wadinstall.cpp writes the files.
//
// A WAD is a 0x20-byte header, then the certificate chain, the ticket,
// the TMD and the contents, each starting on a 0x40-byte boundary. Each
// content is encrypted with the title key (AES-128-CBC, the IV its index
// as two big-endian bytes then zeros) and padded to 16 bytes; the TMD
// holds its size and the SHA-1 of its plain bytes. The title key is in
// the ticket, encrypted with the console's common key, which RiftWii
// reads from the player's own Wii (wii/menu/otpkey.hpp) and never keeps.
namespace riftwii {

struct WadLayout {
    std::uint64_t certs_offset = 0, certs_size = 0;
    std::uint64_t ticket_offset = 0, ticket_size = 0;
    std::uint64_t tmd_offset = 0, tmd_size = 0;
    std::uint64_t data_offset = 0, data_size = 0;
};

// The first 0x20 bytes of a WAD, checked against the file's size. False
// with `error` for anything that is not an installable WAD.
bool parse_wad_header(const std::uint8_t* header, std::size_t size, std::uint64_t file_size, WadLayout& out,
                      std::string& error);

// Where one content's encrypted bytes lie in the file.
struct WadContentPlace {
    TmdContent content;
    std::uint64_t offset = 0;
    std::uint64_t stored = 0;  // its size rounded up to 16 bytes
};

// Every content of the TMD, in its order, as the WAD stores them; false
// when they run past the WAD's data.
bool wad_content_places(const WadLayout& layout, const TmdContents& tmd, std::vector<WadContentPlace>& out,
                        std::string& error);

// Only the small metadata sections are read. The ticket and TMD must name
// the same channel, with unique content IDs/indexes and a boot content.
struct WadMetadata {
    WadLayout layout;
    TmdContents tmd;
    std::vector<std::uint8_t> ticket;
    std::vector<std::uint8_t> tmd_bytes;
    std::vector<WadContentPlace> places;
    Sha1Digest fingerprint{};  // header, ticket, TMD and total file size
};
bool read_wad_metadata(const ByteSource& source, WadMetadata& out, std::string& error);
bool read_wad_banner(const ByteSource& source, const WadMetadata& metadata,
                     const std::uint8_t common_key[16], std::vector<std::uint8_t>& out, std::string& error);

// The small SD cache record holds the banner's name header; the complete
// banner (including its icon) lives beside it and is read only when wanted.
struct WadCacheRecord {
    Sha1Digest fingerprint{};
    std::uint64_t modified = 0;  // SD timestamp; USB uses the metadata fingerprint
    std::uint64_t title_id = 0;
    std::uint32_t banner_size = 0;
    std::vector<std::uint8_t> banner_head;
};
bool encode_wad_cache(const WadCacheRecord& record, std::vector<std::uint8_t>& out);
bool decode_wad_cache(const std::uint8_t* data, std::size_t size, WadCacheRecord& out);

// A prepared title is reused only while its source stamp and installed
// contents agree. Without a preparation stamp, an identical installed TMD
// and all of its contents suffice (the old manual installer remains valid).
bool wad_needs_preparation(const Sha1Digest& source, const Sha1Digest* prepared,
                            bool contents_present, bool same_installed_tmd);

// /sys/uid.sys holds big-endian title-ID/user-ID pairs (12 bytes each).
// Keep existing owners, adding this title only when missing.
bool add_wad_title_uid(std::vector<std::uint8_t>& bytes, std::uint64_t title_id, std::string& error);

// One content, decrypted as it is read: whole 16-byte blocks in order,
// in pieces of any size. Checks its SHA-1 against the TMD's at the end.
class WadContentDecrypter {
public:
    WadContentDecrypter(const std::uint8_t title_key[16], const TmdContent& content);
    // Decrypts `length` bytes (a multiple of 16) in place and returns how
    // many of them are the content's own (the padding after its end is not).
    std::size_t decrypt(std::uint8_t* data, std::size_t length);
    // Whether the plain bytes seen match the TMD's hash; call once at the end.
    bool matches();

private:
    Aes128 aes_;
    std::uint8_t iv_[16];
    std::uint64_t left_;
    Sha1 sha_;
    Sha1Digest expected_;
};

// /shared1/content.map: the Wii's shared contents, each named by its
// eight hex digits ("00000003.app" in /shared1) and known by its SHA-1.
struct SharedContent {
    std::string name;  // "00000003"
    Sha1Digest sha1{};
};
std::vector<SharedContent> parse_content_map(const std::uint8_t* data, std::size_t size);
std::vector<std::uint8_t> serialize_content_map(const std::vector<SharedContent>& entries);
// The entry with that hash, or nullptr.
const SharedContent* find_shared_content(const std::vector<SharedContent>& entries, const Sha1Digest& sha1);
// The name the next new shared content takes: one past the highest.
std::string next_shared_name(const std::vector<SharedContent>& entries);

}  // namespace riftwii
