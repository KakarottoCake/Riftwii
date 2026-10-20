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
    "CORD",  // the Discross channel (a tester's Wii showed it as a game)
};

// Text a forwarder's program holds to name the app it starts.
constexpr const char* kForwarderMarkers[] = {"/apps/", "boot.dol", "boot.elf"};
// The longest stretch of the path shown in the log.
constexpr std::size_t kMaxShownPath = 60;

bool printable(std::uint8_t c) { return c >= 0x20 && c < 0x7F; }

}  // namespace

bool is_homebrew_code(const std::string& code) {
    for (const char* homebrew : kHomebrewCodes)
        if (code == homebrew) return true;
    return false;
}

std::string forwarder_app_path(const std::uint8_t* bytes, std::size_t size) {
    if (bytes == nullptr) return "";
    for (const char* marker : kForwarderMarkers) {
        const std::size_t length = std::strlen(marker);
        for (std::size_t at = 0; at + length <= size; ++at) {
            if (std::memcmp(bytes + at, marker, length) != 0) continue;
            // Widen to the whole run of printable characters around it, so
            // the log shows "sd:/apps/Nintendont/boot.dol", not the marker.
            std::size_t begin = at;
            while (begin > 0 && at - begin < kMaxShownPath / 2 && printable(bytes[begin - 1])) --begin;
            std::size_t end = at + length;
            while (end < size && end - begin < kMaxShownPath && printable(bytes[end])) ++end;
            return std::string(reinterpret_cast<const char*>(bytes + begin), end - begin);
        }
    }
    return "";
}

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
    if (is_homebrew_code(code)) return out;
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

std::uint32_t channel_extras_floor(const DolHeader& program, std::uint32_t loader_end) {
    // Only MEM1 counts: the blobs' code is in MEM1, and RiftWii's loader
    // refuses a program with parts in MEM2.
    constexpr std::uint64_t kMem1Start = 0x80000000;
    constexpr std::uint64_t kMem1End = 0x81800000;
    std::uint64_t floor = loader_end;
    const auto raise_past = [&](std::uint32_t address, std::uint32_t size) {
        if (size == 0 || address < kMem1Start || address >= kMem1End) return;
        const std::uint64_t end = static_cast<std::uint64_t>(address) + size;
        if (end > floor) floor = end;
    };
    for (const DolSection& section : program.sections) {
        if (section.used()) raise_past(section.address, section.size);
    }
    raise_past(program.bss_address, program.bss_size);
    const std::uint64_t aligned = (floor + 31) & ~std::uint64_t{31};
    return aligned > kMem1End ? static_cast<std::uint32_t>(kMem1End) : static_cast<std::uint32_t>(aligned);
}

std::string nand_content_path(std::uint64_t title_id, std::uint32_t content_id) {
    char path[64];
    std::snprintf(path, sizeof path, "/title/%08x/%08x/content/%08x.app", static_cast<unsigned>(title_id >> 32),
                  static_cast<unsigned>(title_id & 0xFFFFFFFFu), static_cast<unsigned>(content_id));
    return path;
}

ChannelVerdict judge_unlisted_channel(const std::string& code, const TmdContents& tmd,
                                      const std::vector<ContentStart>& starts) {
    ChannelVerdict verdict;
    verdict.left_out = true;
    if (is_homebrew_code(code)) {
        verdict.rule = 1;
        verdict.reason = "its code belongs to a known homebrew channel";
        return verdict;
    }
    const TmdContent* boot = tmd_content_by_index(tmd, tmd.boot_index);
    const ContentStart* boot_start = nullptr;
    for (const ContentStart& start : starts)
        if (start.index == tmd.boot_index && !start.head.empty()) boot_start = &start;
    if (boot == nullptr || (boot->type & 0x8000) != 0 || boot_start == nullptr) {
        verdict.rule = 2;
        if (boot != nullptr) verdict.unread_content_id = boot->id;
        verdict.reason = "its program is not on the Wii (or could not be read), so it cannot start";
        return verdict;
    }

    // The program the loader would start; a loader's own first stage is not
    // the one that says where the channel puts itself.
    ChannelProgram program;
    std::string error;
    if (choose_channel_program(tmd, starts, program, error) && program.format == "DOL") {
        for (const DolSection& section : program.dol.sections) {
            if (!section.used() || section.address < kHomebrewLoadAddress) continue;
            char text[96];
            std::snprintf(text, sizeof text, "a homebrew loader (its program loads at 0x%08x)",
                          static_cast<unsigned>(section.address));
            verdict.rule = 3;
            verdict.reason = text;
            return verdict;
        }
    }

    // An inject carries a whole emulator and a game; only a small program
    // can be a forwarder.
    if (boot->size != 0 && boot->size <= kForwarderMaxBytes) {
        const std::string path = forwarder_app_path(boot_start->head.data(), boot_start->head.size());
        if (!path.empty()) {
            verdict.rule = 4;
            verdict.reason = "a homebrew forwarder (its program starts " + path + ")";
            return verdict;
        }
    }
    verdict.left_out = false;
    return verdict;
}

}  // namespace riftwii
