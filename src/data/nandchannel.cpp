// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2010 dude (USB Loader GX channel identity design)
// SPDX-FileCopyrightText: 2011 Miigotu, Dimok (USB Loader GX channel identity design)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/nandchannel.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "riftwii/bnr.hpp"

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
    "OHBC",  // the Homebrew Channel under this title ID (a tester's vWii listed it as a game, 22 Oct 2026)
    "UFTW",  // RiftWii's own channel
    "CORD",  // the Discross channel (a tester's Wii showed it as a game)
};

// Text a forwarder's program holds to name the app it starts.
constexpr const char* kForwarderMarkers[] = {"/apps/", "boot.dol", "boot.elf"};
// The longest stretch of the path shown in the log.
constexpr std::size_t kMaxShownPath = 60;

bool printable(std::uint8_t c) { return c >= 0x20 && c < 0x7F; }

bool nand_lz_header(const std::uint8_t* data, std::size_t size, std::size_t max_out,
                    std::size_t& at, std::size_t& want, std::string& error) {
    if (data == nullptr || size < 4) {
        error = "the compression header ends early";
        return false;
    }
    if (data[0] != 0x10 && data[0] != 0x11) {
        error = "it is not LZ77-compressed";
        return false;
    }
    at = 4;
    want = static_cast<std::size_t>(data[1]) | (static_cast<std::size_t>(data[2]) << 8) |
           (static_cast<std::size_t>(data[3]) << 16);
    if (want == 0) {
        if (size < 8) {
            error = "the compression header ends early";
            return false;
        }
        want = static_cast<std::size_t>(data[4]) | (static_cast<std::size_t>(data[5]) << 8) |
               (static_cast<std::size_t>(data[6]) << 16) | (static_cast<std::size_t>(data[7]) << 24);
        at = 8;
    }
    if (want == 0 || want > max_out) {
        error = "it unpacks to " + std::to_string(want) + " bytes, not a size a program can be";
        return false;
    }
    return true;
}

bool lz77_nand(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
               std::size_t max_out, std::string& error, std::size_t prefix_bytes = 0) {
    std::size_t at = 0, want = 0;
    if (!nand_lz_header(data, size, max_out, at, want, error)) return false;
    if (prefix_bytes != 0) want = std::min(want, prefix_bytes);
    const std::uint8_t type = data[0];
    std::vector<std::uint8_t> unpacked;
    while (unpacked.size() < want) {
        if (at >= size) {
            error = "the compressed data ends early";
            return false;
        }
        const std::uint8_t flags = data[at++];
        for (int bit = 7; bit >= 0 && unpacked.size() < want; --bit) {
            if ((flags & (1u << bit)) == 0) {
                if (at >= size) {
                    error = "the compressed data ends early";
                    return false;
                }
                unpacked.push_back(data[at++]);
                continue;
            }
            std::size_t length = 0, distance = 0;
            if (type == 0x10) {
                if (at + 1 >= size) {
                    error = "the compressed data ends early";
                    return false;
                }
                length = 3 + (data[at] >> 4);
                distance = (((static_cast<std::size_t>(data[at]) & 0x0F) << 8) | data[at + 1]) + 1;
                at += 2;
            } else {
                if (at + 1 >= size) {
                    error = "the compressed data ends early";
                    return false;
                }
                const std::uint8_t first = data[at];
                const std::uint8_t second = data[at + 1];
                if (first < 0x20) {
                    if (at + 2 >= size) {
                        error = "the compressed data ends early";
                        return false;
                    }
                    const std::uint8_t third = data[at + 2];
                    if (first >= 0x10) {
                        if (at + 3 >= size) {
                            error = "the compressed data ends early";
                            return false;
                        }
                        distance = (((static_cast<std::size_t>(third) & 0x0F) << 8) | data[at + 3]) + 1;
                        length = ((static_cast<std::size_t>(second) << 4) |
                                  ((static_cast<std::size_t>(first) & 0x0F) << 12) |
                                  (static_cast<std::size_t>(third) >> 4)) + 273;
                        at += 4;
                    } else {
                        distance = (((static_cast<std::size_t>(second) & 0x0F) << 8) | third) + 1;
                        length = (((static_cast<std::size_t>(first) & 0x0F) << 4) | (second >> 4)) + 17;
                        at += 3;
                    }
                } else {
                    distance = (((static_cast<std::size_t>(first) & 0x0F) << 8) | second) + 1;
                    length = (first >> 4) + 1;
                    at += 2;
                }
            }
            if (distance > unpacked.size()) {
                error = "a back-reference points before its start";
                return false;
            }
            for (std::size_t i = 0; i < length && unpacked.size() < want; ++i)
                unpacked.push_back(unpacked[unpacked.size() - distance]);
        }
    }
    out.swap(unpacked);
    return true;
}

}  // namespace

bool channel_program_compressed(const std::uint8_t* data, std::size_t size, std::size_t max_out) {
    if (data == nullptr || size < 4) return false;
    if (std::memcmp(data, "LZ77", 4) == 0) {
        data += 4;
        size -= 4;
    }
    std::size_t payload = 0, unpacked = 0;
    std::string error;
    return nand_lz_header(data, size, max_out, payload, unpacked, error) && size > payload;
}

bool unpack_channel_program(std::vector<std::uint8_t>& data, std::string& error, std::size_t max_out) {
    if (data.size() < 4) {
        error = "it is too short to be a program";
        return false;
    }
    std::vector<std::uint8_t> unpacked;
    if (std::memcmp(data.data(), "LZ77", 4) == 0) {
        if (!lz77_nand(data.data() + 4, data.size() - 4, unpacked, max_out, error)) return false;
    } else if (data[0] == 0x10 || data[0] == 0x11) {
        if (!lz77_nand(data.data(), data.size(), unpacked, max_out, error)) return false;
    } else {
        error = "it is not LZ77-compressed";
        return false;
    }
    data.swap(unpacked);
    error.clear();
    return true;
}

bool is_homebrew_code(const std::string& code) {
    for (const char* homebrew : kHomebrewCodes)
        if (code == homebrew) return true;
    return false;
}

bool is_nintendo_channel(std::uint64_t title_id) {
    const std::uint32_t upper = static_cast<std::uint32_t>(title_id >> 32);
    if (upper != kDownloadedChannels && upper != kSystemChannels && upper != kHiddenChannels) return false;
    const std::string code = channel_code(title_id);
    return !code.empty() && code[0] == 'H';
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
    // The first letter of a Shop title's code names what it is.
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
        // Nintendo's own channels (the Shop, Photo, Mii, News...) are not
        // games and stay out.
        case kNintendoChannelLetter: break;
        // Anything else is a custom inject or a channel made by hand
        // (a tester's "TIME"): listed like WiiWare once judge_unlisted_channel
        // finds it is not a homebrew loader.
        default: out.kind = ChannelKind::Other; break;
    }
    if (!out.system.empty()) out.kind = ChannelKind::VirtualConsole;
    return out;
}

std::string channel_left_out_reason(std::uint64_t title_id) {
    if (static_cast<std::uint32_t>(title_id >> 32) != kDownloadedChannels) return "";
    const std::string code = channel_code(title_id);
    if (code.empty()) return "its code is not four capital letters or digits";
    if (is_homebrew_code(code)) return "its code belongs to a known homebrew channel";
    if (code[0] == kNintendoChannelLetter)
        return std::string("its code starts with ") + kNintendoChannelLetter +
               ", which is Nintendo's own channels (Shop, Photo, Mii, News...), not games";
    return "";
}

std::string channel_cheat_code(const std::string& game_id) {
    if (game_id.size() != 6) return "";
    std::uint32_t lower = 0;
    for (int i = 0; i < 4; ++i) lower = (lower << 8) | static_cast<std::uint8_t>(game_id[i]);
    const std::uint64_t as_title = (std::uint64_t(kDownloadedChannels) << 32) | lower;
    // Only Shop codes: a disc's ID (RSBE) would otherwise pass as "other".
    const ChannelKind kind = classify_channel(as_title).kind;
    if (kind != ChannelKind::WiiWare && kind != ChannelKind::VirtualConsole) return "";
    return game_id.substr(0, 4);
}

// Reimplemented from USB Loader GX's Identify_GenerateTik construction
// (NOTICE.md). It supplies identity metadata, not a valid Nintendo signature;
// whether ES accepts it depends on the IOS and is not assumed here.
std::vector<std::uint8_t> make_fake_ticket() {
    constexpr std::size_t kIssuer = 0x140;
    constexpr std::size_t kContentIndexMask = 0x222;
    constexpr std::size_t kMaskBytes = 32;
    std::vector<std::uint8_t> ticket(0x2A4, 0);
    ticket[0] = 0x00;
    ticket[1] = 0x01;
    ticket[2] = 0x00;
    ticket[3] = 0x01;
    const char* const issuer = "Root-CA00000001-XS00000003";
    std::memcpy(ticket.data() + kIssuer, issuer, std::strlen(issuer));
    std::memset(ticket.data() + kContentIndexMask, 0xFF, kMaskBytes);
    return ticket;
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
        const bool compressed = channel_program_compressed(start.head.data(), start.head.size());
        DolHeader dol;
        std::string why;
        std::string format;
        bool parsed = false;
        std::size_t program_bytes = start.size;
        if (compressed) {
            const bool wrapped = std::memcmp(start.head.data(), "LZ77", 4) == 0;
            const std::size_t skip = wrapped ? 4 : 0;
            const std::uint8_t* payload = start.head.data() + skip;
            const std::size_t payload_size = start.head.size() - skip;
            std::size_t header_end = 0;
            std::vector<std::uint8_t> header;
            // Only a parsed DOL header may earn compressed-program priority.
            // The content prefix is small; do not allocate the advertised 24 MiB.
            parsed = nand_lz_header(payload, payload_size, 24u << 20, header_end, program_bytes, why) &&
                     lz77_nand(payload, payload_size, header, 24u << 20, why, 0x100) &&
                     parse_dol_header(header.data(), header.size(), dol, why);
            format = "LZ77";
        } else {
            parsed = parse_channel_program(start.head.data(), start.head.size(), dol, format, why);
        }
        if (!parsed) {
            seen += (seen.empty() ? "" : "; ") + std::to_string(start.index) + ": " + content_kind(start.head) +
                    (why.empty() ? "" : " (" + why + ")");
            continue;
        }
        // A header whose sections run past the file is not this file's.
        if (dol.image_size() > program_bytes) {
            seen += (seen.empty() ? "" : "; ") + std::to_string(start.index) + ": a DOL longer than the file";
            continue;
        }
        int rank = 0;
        if (compressed && start.index == 1) rank = 4;
        else if (compressed) rank = 1;
        else rank = dol_read_leniently(format) ? 2 : 3;
        const bool better = !found || rank > found_rank || (rank == found_rank && start.size > found_size);
        if (!better) continue;
        found = true;
        found_rank = rank;
        found_size = start.size;
        best.index = start.index;
        best.dol = dol;
        best.format = compressed ? std::string("LZ77") : format;
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

bool dol_read_leniently(const std::string& format) {
    static const char kPrefix[] = "DOL read leniently";
    return format.compare(0, sizeof(kPrefix) - 1, kPrefix) == 0;
}

std::string channel_codes_refusal(const ChannelProgram& program) {
    if (!dol_read_leniently(program.format)) return std::string();
    return "content " + std::to_string(program.index) + " was read only leniently (" + program.format +
           "), so it may be a loader that starts the game from elsewhere, and the codes would hook that "
           "loader, not the game";
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
