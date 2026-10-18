// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/nandchannel.hpp"

#include <cstdio>
#include <cstring>

namespace riftwii {
namespace {

// A TMD as ES_GetStoredTMD returns it: a 4-byte signature type, an RSA-2048
// signature and padding (0x140 bytes in all), then the TMD body. The
// offsets below are from the start of the signed blob.
constexpr std::size_t kTmdIosOffset = 0x184;
constexpr std::size_t kTmdTitleIdOffset = 0x18C;
constexpr std::size_t kTmdGroupIdOffset = 0x198;
constexpr std::size_t kTmdContentCountOffset = 0x1DE;
constexpr std::size_t kTmdBootIndexOffset = 0x1E0;
constexpr std::size_t kTmdContentsOffset = 0x1E4;
constexpr std::size_t kTmdContentBytes = 36;

std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }
std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint64_t be64(const std::uint8_t* p) { return (std::uint64_t(be32(p)) << 32) | be32(p + 4); }

// Channels whose code starts with a Virtual Console letter but that are
// homebrew: they have their own place to start from, and are not games.
constexpr const char* kHomebrewCodes[] = {
    "LULZ",  // the Homebrew Channel
    "JODI",  // the Homebrew Channel, older
    "UFTW",  // RiftWii's own channel
};

}  // namespace

std::string channel_code(std::uint64_t title_id) {
    std::string code;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const char c = static_cast<char>((title_id >> shift) & 0xFF);
        const bool letter = c >= 'A' && c <= 'Z';
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !digit) return "";
        code += c;
    }
    return code;
}

ChannelClass classify_channel(std::uint64_t title_id) {
    ChannelClass out;
    if (static_cast<std::uint32_t>(title_id >> 32) != kDownloadedChannels) return out;
    const std::string code = channel_code(title_id);
    if (code.empty()) return out;
    for (const char* homebrew : kHomebrewCodes)
        if (code == homebrew) return out;
    // The first letter of a Shop title's code names what it is. H is
    // Nintendo's own channels (the Shop, Photo, Mii, News...), which are
    // not games and stay out.
    switch (code[0]) {
        case 'W': out.kind = ChannelKind::WiiWare; break;
        case 'F': out.system = "NES"; break;
        case 'J': out.system = "Super NES"; break;
        case 'N': out.system = "Nintendo 64"; break;
        case 'L': out.system = "Master System"; break;
        case 'M': out.system = "Mega Drive"; break;
        case 'P': out.system = "TurboGrafx-16"; break;
        case 'Q': out.system = "TurboGrafx-CD"; break;
        case 'E': out.system = "Neo Geo"; break;
        case 'C': out.system = "Commodore 64"; break;
        case 'X': out.system = "MSX"; break;
        default: break;
    }
    if (!out.system.empty()) out.kind = ChannelKind::VirtualConsole;
    return out;
}

std::string channel_cheat_code(const std::string& game_id) {
    if (game_id.size() != 6) return "";
    std::uint32_t lower = 0;
    for (int i = 0; i < 4; ++i) lower = (lower << 8) | static_cast<std::uint8_t>(game_id[i]);
    const std::uint64_t as_title = (std::uint64_t(kDownloadedChannels) << 32) | lower;
    if (classify_channel(as_title).kind == ChannelKind::None) return "";
    return game_id.substr(0, 4);
}

bool parse_tmd_contents(const std::uint8_t* tmd, std::size_t size, TmdContents& out, std::string& error) {
    if (tmd == nullptr || size < kTmdContentsOffset) {
        error = "the TMD is too small";
        return false;
    }
    const std::uint16_t count = be16(tmd + kTmdContentCountOffset);
    if (size < kTmdContentsOffset + std::size_t(count) * kTmdContentBytes) {
        error = "the TMD is smaller than its " + std::to_string(count) + " content record(s)";
        return false;
    }
    TmdContents result;
    result.title_id = be64(tmd + kTmdTitleIdOffset);
    // sys_version is 0x00000001'000000NN for IOS NN.
    const std::uint64_t ios = be64(tmd + kTmdIosOffset);
    if ((ios >> 32) == 1 && (ios & 0xFFFFFFFFu) < 256) result.required_ios = static_cast<std::uint32_t>(ios & 0xFF);
    result.boot_index = be16(tmd + kTmdBootIndexOffset);
    for (int i = 0; i < 2; ++i) {
        const char c = static_cast<char>(tmd[kTmdGroupIdOffset + i]);
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!ok) {
            result.maker.clear();
            break;
        }
        result.maker += c;
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::uint8_t* record = tmd + kTmdContentsOffset + std::size_t(i) * kTmdContentBytes;
        TmdContent c;
        c.id = be32(record);
        c.index = be16(record + 4);
        c.type = be16(record + 6);
        c.size = be64(record + 8);
        std::memcpy(c.sha1.data(), record + 16, c.sha1.size());
        result.contents.push_back(c);
    }
    out = std::move(result);
    error.clear();
    return true;
}

std::string channel_game_id(const std::string& code, const std::string& maker) {
    return code + (maker.size() == 2 ? maker : std::string("01"));
}

const TmdContent* tmd_content_by_index(const TmdContents& tmd, std::uint16_t index) {
    for (const TmdContent& c : tmd.contents)
        if (c.index == index) return &c;
    return nullptr;
}

namespace {

// What a content's first bytes say it is, when it is not a DOL.
std::string content_kind(const std::vector<std::uint8_t>& head) {
    if (head.size() < 4) return "unreadable";
    if (head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F') return "an ELF program";
    if (head[0] == 'L' && head[1] == 'Z' && head[2] == '7' && head[3] == '7') return "LZ77-compressed (LZ77 header)";
    if (head[0] == 0x10 || head[0] == 0x11) return "LZ77-compressed, it seems (type " + std::to_string(head[0]) + ")";
    if (head[0] == 0x55 && head[1] == 0xAA && head[2] == 0x38 && head[3] == 0x2D) return "a U8 archive";
    char text[48];
    std::snprintf(text, sizeof text, "starts %02x %02x %02x %02x %02x %02x %02x %02x", head[0], head[1], head[2],
                  head[3], head.size() > 4 ? head[4] : 0, head.size() > 5 ? head[5] : 0,
                  head.size() > 6 ? head[6] : 0, head.size() > 7 ? head[7] : 0);
    return text;
}

}  // namespace

bool choose_channel_program(const TmdContents& tmd, const std::vector<ContentStart>& starts, ChannelProgram& out,
                            std::string& error) {
    bool found = false;
    std::uint64_t found_size = 0;
    int found_rank = 0;
    ChannelProgram best;
    // What each content was instead, for the log: a WiiWare game whose
    // program RiftWii could not find (Fluidity) left no clue which form
    // its program takes.
    std::string seen;
    for (const ContentStart& start : starts) {
        const TmdContent* record = tmd_content_by_index(tmd, start.index);
        const bool shared = record && (record->type & 0x8000) != 0;
        if (!record || shared || start.index == 0) continue;
        DolHeader dol;
        std::string why;
        std::string format;
        if (!parse_channel_program(start.head.data(), start.head.size(), dol, format, why)) {
            seen += (seen.empty() ? "" : "; ") + std::to_string(start.index) + ": " + content_kind(start.head) +
                    (why.empty() ? "" : " (" + why + ")");
            continue;
        }
        // A header whose sections run past the file is not this file's.
        if (dol.image_size() > start.size) {
            seen += (seen.empty() ? "" : "; ") + std::to_string(start.index) + ": a DOL longer than the file";
            continue;
        }
        // A DOL or ELF read as it is wins over one read leniently (a big
        // data file could pass that by chance); then the bigger one.
        const int rank = format.compare(0, 18, "DOL read leniently") == 0 ? 0 : 1;
        const bool better = !found || rank > found_rank || (rank == found_rank && start.size > found_size);
        if (!better) continue;
        found = true;
        found_rank = rank;
        found_size = start.size;
        best.index = start.index;
        best.dol = dol;
        best.format = format;
    }
    if (!found) {
        error = "none of its contents is a program RiftWii can load";
        if (!seen.empty()) error += " (contents " + seen + ")";
        return false;
    }
    best.past_loader = best.index != tmd.boot_index;
    out = best;
    error.clear();
    return true;
}

std::string nand_content_path(std::uint64_t title_id, std::uint32_t content_id) {
    char path[64];
    std::snprintf(path, sizeof path, "/title/%08x/%08x/content/%08x.app", static_cast<unsigned>(title_id >> 32),
                  static_cast<unsigned>(title_id & 0xFFFFFFFFu), static_cast<unsigned>(content_id));
    return path;
}

}  // namespace riftwii
