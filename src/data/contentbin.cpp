// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2020 DarkMatterCore (content.bin layout, as wad2bin and YABDM read and write it)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/contentbin.hpp"

#include <cstdio>

namespace riftwii {
namespace {

constexpr std::uint64_t kAlign = 0x40;
// The "Bk" header's own size field, its type and version: 00000070 'B' 'k' 0001.
constexpr std::uint8_t kBkMagic[8] = {0x00, 0x00, 0x00, 0x70, 'B', 'k', 0x00, 0x01};
// Part F: an ECDSA signature (0x40) and two ECC certificates (0x180 each).
constexpr std::uint64_t kCertAreaBytes = 0x340;
// A TMD with a few hundred content records is under 16 KB.
constexpr std::uint64_t kMaxTmdBytes = 0x10000;
// keys.bin: the text header, then the OTP, whose console ID is at 0x24
// and PRNG key at 0x68.
constexpr std::size_t kKeysOtpOffset = 0x100;
constexpr std::size_t kOtpConsoleId = 0x24;
constexpr std::size_t kOtpPrngKey = 0x68;
constexpr std::size_t kOtpBytes = 0x80;

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

std::uint64_t aligned(std::uint64_t value) { return (value + kAlign - 1) & ~(kAlign - 1); }

bool matches_magic(const std::uint8_t* p) {
    for (std::size_t i = 0; i < sizeof kBkMagic; ++i)
        if (p[i] != kBkMagic[i]) return false;
    return true;
}

}  // namespace

std::string content_bin_path(const std::string& code) { return "/private/wii/title/" + code + "/content.bin"; }

bool find_content_bin_layout(const std::uint8_t* start, std::size_t size, std::uint64_t file_size,
                             ContentBinLayout& out, std::string& error) {
    if (start == nullptr || file_size < kContentBinPartABytes + kContentBinBkBytes) {
        error = "it is too small to be a channel moved to the SD card";
        return false;
    }
    // The header's magic alone could turn up inside the encrypted icon;
    // its backup size, which runs to the file's end, makes it sure.
    for (std::uint64_t at = kContentBinPartABytes; at + kContentBinBkBytes <= size; at += kAlign) {
        const std::uint8_t* bk = start + at;
        if (!matches_magic(bk)) continue;
        const std::uint64_t backup_size = be32(bk + 0x1C);
        if (backup_size != file_size - at) continue;
        ContentBinLayout layout;
        layout.bk_offset = at;
        layout.console_id = be32(bk + 0x08);
        layout.tmd_size = be32(bk + 0x14);
        layout.data_size = be32(bk + 0x18);
        for (std::size_t i = 0; i < layout.included.size(); ++i) layout.included[i] = bk[0x20 + i];
        layout.tmd_offset = at + kContentBinBkBytes;
        layout.data_offset = aligned(layout.tmd_offset + layout.tmd_size);
        if (layout.tmd_size < 0x1E4 || layout.tmd_size > kMaxTmdBytes) {
            error = "its TMD has a size no TMD has";
            return false;
        }
        if (layout.data_offset > file_size || layout.data_size > file_size - layout.data_offset ||
            file_size - layout.data_offset - layout.data_size < kCertAreaBytes) {
            error = "the file is shorter than its header says (cut off while copying?)";
            return false;
        }
        out = layout;
        error.clear();
        return true;
    }
    error = "its \"Bk\" header was not found (not a channel the Wii Menu moved to the SD card)";
    return false;
}

bool content_bin_includes(const ContentBinLayout& layout, std::size_t position) {
    if (position / 8 >= layout.included.size()) return false;
    return (layout.included[position / 8] >> (position % 8)) & 1;
}

bool content_bin_places(const ContentBinLayout& layout, const TmdContents& tmd, std::vector<WadContentPlace>& out,
                        std::string& error) {
    std::vector<WadContentPlace> places;
    std::uint64_t at = layout.data_offset;
    const std::uint64_t end = layout.data_offset + layout.data_size;
    for (std::size_t position = 0; position < tmd.contents.size(); ++position) {
        if (!content_bin_includes(layout, position)) continue;
        const TmdContent& c = tmd.contents[position];
        WadContentPlace place;
        place.content = c;
        place.offset = at;
        place.stored = (c.size + 15) & ~std::uint64_t(15);
        // Compared without adding to `at`, so a size near 2^64 cannot wrap.
        if (c.size == 0 || place.stored < c.size || at > end || place.stored > end - at) {
            char text[80];
            std::snprintf(text, sizeof text, "content %08x runs past the file's content data",
                          static_cast<unsigned>(c.id));
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

bool content_bin_matches_tmd(const TmdContents& moved, const TmdContents& installed, std::string& error) {
    if (moved.title_id != installed.title_id || moved.contents.size() != installed.contents.size()) {
        error = "its TMD is not the one the Wii has for this channel (an older or other copy of it)";
        return false;
    }
    for (std::size_t i = 0; i < moved.contents.size(); ++i) {
        const TmdContent& a = moved.contents[i];
        const TmdContent& b = installed.contents[i];
        if (a.id != b.id || a.index != b.index || a.type != b.type || a.size != b.size || a.sha1 != b.sha1) {
            char text[120];
            std::snprintf(text, sizeof text,
                          "its content %08x is not the one the Wii's TMD has (an older or other copy of the channel)",
                          static_cast<unsigned>(a.id));
            error = text;
            return false;
        }
    }
    error.clear();
    return true;
}

bool parse_staged_content_path(const std::string& path, std::uint64_t& title_id) {
    unsigned upper = 0, lower = 0, id = 0;
    if (std::sscanf(path.c_str(), "/title/%8x/%8x/content/%8x.app", &upper, &lower, &id) != 3) return false;
    bool channel_type = false;
    for (const std::uint32_t type : kChannelTitleTypes) channel_type = channel_type || type == upper;
    const std::uint64_t title = (std::uint64_t(upper) << 32) | lower;
    // Only the exact path RiftWii writes: nothing after ".app", no "+",
    // spaces or short fields that sscanf lets through.
    if (!channel_type || nand_content_path(title, id) != path) return false;
    title_id = title;
    return true;
}

bool parse_bootmii_keys(const std::uint8_t* data, std::size_t size, BackupKey& out) {
    if (data == nullptr || size < kKeysOtpOffset + kOtpBytes) return false;
    const std::uint8_t* otp = data + kKeysOtpOffset;
    BackupKey key;
    key.console_id = be32(otp + kOtpConsoleId);
    std::uint8_t any = 0;
    for (std::size_t i = 0; i < sizeof key.prng; ++i) {
        key.prng[i] = otp[kOtpPrngKey + i];
        any |= key.prng[i];
    }
    // A zeroed OTP is a dump that went wrong, not a key.
    if (any == 0 || key.console_id == 0) return false;
    out = key;
    return true;
}

}  // namespace riftwii
