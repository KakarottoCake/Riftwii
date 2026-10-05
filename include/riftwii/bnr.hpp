// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/overlay.hpp"

// A game's opening.bnr: the banner and icon the Wii Menu shows for it.
// Layouts from the public format pages: https://wiibrew.org/wiki/Opening.bnr,
// https://wiibrew.org/wiki/U8_archive and https://wiibrew.org/wiki/LZ77.
//
// On a disc the file starts with 0x40 zero bytes, then the IMET header
// (the channel's names in ten languages), then at 0x600 a U8 archive of
// meta/icon.bin (the small picture on the Wii Menu's grid, animated
// before the channel is opened), meta/banner.bin (the full banner) and
// meta/sound.bin. icon.bin and banner.bin are each an IMD5 header over a
// U8 archive, usually LZ77-compressed, holding arc/blyt/*.brlyt (the
// layout), arc/anim/*.brlan (its animations) and arc/timg/*.tpl (textures).
namespace riftwii {

// Data after an "LZ77" magic: a little-endian word (type 0x10 in its low
// byte, the unpacked size above), then flag bytes, most significant bit
// first: 0 a literal byte, 1 a two-byte reference (length - 3 in the top
// four bits, then 12 bits of distance - 1). False on anything else, or
// when the unpacked size is over `max_out`.
bool lz77_decompress(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out, std::string& error,
                     std::size_t max_out = 16u << 20);

// A U8 archive, read in place: `data` must outlive it. Paths are absolute
// ("/arc/blyt/icon.brlyt"); the "." directory banners put at the top is
// left out of them.
class U8Archive {
public:
    static bool parse(const std::uint8_t* data, std::size_t size, U8Archive& out, std::string& error);
    // A file by path, ASCII case ignored.
    bool find(const std::string& path, const std::uint8_t*& data, std::size_t& size) const;
    // Every file's path, in archive order.
    std::vector<std::string> files() const;
    // The files directly or deeper under `dir` whose names end in `suffix`.
    std::vector<std::string> files_in(const std::string& dir, const std::string& suffix) const;

private:
    struct File {
        std::string path;
        std::uint32_t offset = 0, size = 0;
    };
    const std::uint8_t* base_ = nullptr;
    std::vector<File> files_;
};

// icon.bin or banner.bin: the U8 archive inside its IMD5 header,
// unpacked when it is LZ77.
bool unpack_banner_bin(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out, std::string& error);

// The ten languages of IMET's names, in their order.
enum BannerLanguage { kBnrJapanese, kBnrEnglish, kBnrGerman, kBnrFrench, kBnrSpanish, kBnrItalian, kBnrDutch,
                      kBnrChineseSimplified, kBnrChineseTraditional, kBnrKorean, kBnrLanguages };

struct OpeningBanner {
    std::array<std::string, kBnrLanguages> names;  // UTF-8; may hold a newline between two lines
    std::vector<std::uint8_t> icon;                // unpacked U8 archives
    std::vector<std::uint8_t> banner;
    std::vector<std::uint8_t> sound;               // as stored (IMD5 + BNS/WAV/AIFF)
};

// `want_banner` false skips unpacking the large banner (a grid needs only
// the icons).
bool parse_opening_bnr(const std::uint8_t* data, std::size_t size, OpeningBanner& out, std::string& error,
                       bool want_banner = true);

// A file from a Wii partition's decrypted data, found through its FST.
bool read_partition_file(const ByteSource& data, const std::string& path, std::vector<std::uint8_t>& out,
                         std::string& error, std::size_t max_size = 8u << 20);

}  // namespace riftwii
