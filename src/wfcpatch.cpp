// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wfcpatch.hpp"

#include <cstring>

namespace riftwii {
namespace {

constexpr char kNintendo[] = "nintendowifi.net";
constexpr std::size_t kNintendoLen = sizeof(kNintendo) - 1;

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}
unsigned opcode(std::uint32_t inst) { return inst >> 26; }
unsigned immediate(std::uint32_t inst) { return inst & 0xFFFF; }
unsigned load_target(std::uint32_t inst) { return (inst >> 21) & 0x1F; }
unsigned compare_target(std::uint32_t inst) { return (inst >> 16) & 0x1F; }

// The string's length from `at`, never past the buffer.
std::size_t string_length(const std::uint8_t* bytes, std::size_t size, std::size_t at) {
    std::size_t n = 0;
    while (at + n < size && bytes[at + n] != 0) ++n;
    return n;
}

}  // namespace

const char* to_string(WfcServer s) {
    switch (s) {
        case WfcServer::Wiimmfi: return "wiimmfi";
        case WfcServer::WiiLink: return "wiilink";
        case WfcServer::AltWfc: return "altwfc";
        case WfcServer::Custom: return "custom";
        default: return "off";
    }
}

bool parse_wfc_server(const std::string& s, WfcServer& out) {
    for (WfcServer v : {WfcServer::Off, WfcServer::Wiimmfi, WfcServer::WiiLink, WfcServer::AltWfc, WfcServer::Custom}) {
        if (s == to_string(v)) {
            out = v;
            return true;
        }
    }
    return false;
}

std::string wfc_domain(WfcServer s, const std::string& custom) {
    switch (s) {
        case WfcServer::Wiimmfi: return "wiimmfi.de";
        case WfcServer::AltWfc: return "zwei.moe";
        case WfcServer::Custom: return custom;
        default: return "";
    }
}

bool valid_wfc_domain(const std::string& domain) {
    if (domain.size() < 4 || domain.size() > kNintendoLen || domain.find('.') == std::string::npos) return false;
    for (char c : domain) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        if (!ok) return false;
    }
    return domain.front() != '.' && domain.back() != '.';
}

unsigned patch_https_to_http(std::uint8_t* bytes, std::size_t size) {
    unsigned count = 0;
    for (std::size_t at = 0; at + 9 <= size; ++at) {
        if (std::memcmp(bytes + at, "https://", 8) != 0 || bytes[at + 8] == 0) continue;
        const std::size_t len = string_length(bytes, size, at);
        // Drop the 's': the rest moves one byte left, the end gets a zero.
        std::memmove(bytes + at + 4, bytes + at + 5, len - 5);
        bytes[at + len - 1] = 0;
        at += len;
        ++count;
    }
    return count;
}

unsigned patch_wfc_domain(std::uint8_t* bytes, std::size_t size, const std::string& domain) {
    if (domain.empty() || domain.size() > kNintendoLen) return 0;
    unsigned count = 0;
    for (std::size_t at = 0; at + kNintendoLen <= size; ++at) {
        if (std::memcmp(bytes + at, kNintendo, kNintendoLen) != 0) continue;
        const std::size_t len = string_length(bytes, size, at);
        std::memcpy(bytes + at, domain.data(), domain.size());
        std::memmove(bytes + at + domain.size(), bytes + at + kNintendoLen, len - kNintendoLen);
        for (std::size_t i = len - (kNintendoLen - domain.size()); i < len; ++i) bytes[at + i] = 0;
        at += len;
        ++count;
    }
    return count;
}

int patch_wiimmfi_generic(std::uint8_t* bytes, std::size_t size) {
    // Leseratte's patch as USB Loader GX ships it (version 3). The User-Agent
    // mark tells Wiimmfi which patcher and version patched the game.
    static const char kGt2[] = "<GT2> RECV-0x%02x <- [--------:-----] [pid=%u]";
    static const std::uint8_t kGt2Locator[8] = {0x38, 0x61, 0x00, 0x08, 0x38, 0xA0, 0x00, 0x14};
    static const std::uint8_t kP2pV1[22] = {32, 32, 21, 21, 21, 21, 20, 20, 31, 40, 21, 20, 20, 31, 31, 10, 20, 36, 21, 44, 36, 16};
    static const std::uint8_t kP2pV2[22] = {32, 32, 21, 21, 20, 21, 20, 21, 31, 40, 21, 20, 20, 31, 31, 10, 20, 36, 21, 44, 36, 16};
    static const std::uint8_t kMasterV1[22] = {21, 21, 21, 21, 40, 20, 20, 20, 20, 31, 31, 14, 31, 20, 21, 44, 21, 36, 36, 18, 11, 16};
    static const std::uint8_t kMasterV2[22] = {21, 21, 21, 21, 40, 20, 20, 20, 20, 31, 31, 14, 31, 20, 21, 36, 21, 44, 36, 18, 11, 16};

    int gt2 = 0;
    for (std::size_t at = 0; at + sizeof(kGt2) - 1 <= size; ++at) {
        if (std::memcmp(bytes + at, kGt2, sizeof(kGt2) - 1) == 0) ++gt2;
    }
    if (gt2 > 1) return 1;

    const auto word = [&](std::size_t at) { return be32(bytes + at); };
    int p2p = 0, master = 0;
    for (std::size_t at = 0; at + 20 <= size; ++at) {
        if (std::memcmp(bytes + at, "User-Agent\0\0RVL SDK/", 20) == 0) {
            std::memcpy(bytes + at + 12, gt2 ? "G-3-1\0" : "G-3-0\0", 6);
        }
        // The code checked below reaches 0x60 + 40 + 0x58 bytes past here.
        if (!gt2 || (at & 3) != 0 || at + 0x100 > size || std::memcmp(bytes + at, kGt2Locator, 8) != 0) continue;
        bool p2p_v1 = true, p2p_v2 = true;
        for (std::size_t i = 0; i < 22; ++i) {
            const unsigned op = opcode(word(at + 12 + i * 4));
            p2p_v1 = p2p_v1 && op == kP2pV1[i];
            p2p_v2 = p2p_v2 && op == kP2pV2[i];
        }
        std::size_t chain = 0;
        for (std::size_t dynamic = 0; dynamic < 40 && chain == 0; dynamic += 4) {
            bool found = true;
            for (std::size_t i = 0; i < 22 && found; ++i) {
                const unsigned op = opcode(word(at + 12 + dynamic + i * 4));
                found = op == kMasterV1[i] || op == kMasterV2[i];
            }
            if (found) chain = at + 12 + dynamic;
        }
        std::uint8_t* c = bytes + at;
        if (p2p_v1 || p2p_v2) {
            if (immediate(word(at + 0x0c)) == 0x0c && immediate(word(at + 0x10)) == 0x18 &&
                immediate(word(at + 0x30)) == 0x12 && immediate(word(at + 0x48)) == 0x5a &&
                immediate(word(at + 0x50)) == 0x0c && immediate(word(at + 0x58)) == 0x12 &&
                immediate(word(at + 0x5c)) == 0x18 && immediate(word(at + 0x60)) == 0x18) {
                unsigned loaded = load_target(word(at + 0x14));
                const unsigned compared = compare_target(word(at + 0x48));
                if (p2p_v1) {
                    put32(c + 0x14, 0x88010011 | (compared << 21));
                    put32(c + 0x18, 0x28000080 | (compared << 16));
                    put32(c + 0x24, 0x41810064);
                    put32(c + 0x28, 0x60000000);
                    put32(c + 0x2c, 0x60000000);
                    put32(c + 0x34, 0x3C005A00 | (compared << 21));
                    put32(c + 0x48, 0x7C000000 | (compared << 16) | (loaded << 11));
                    ++p2p;
                }
                if (p2p_v2) {
                    loaded = 12;
                    put32(c + 0x14, 0x88010011 | (compared << 21));
                    put32(c + 0x18, 0x28000080 | (compared << 16));
                    put32(c + 0x1c, 0x41810070);
                    put32(c + 0x24, word(at + 0x28));
                    put32(c + 0x28, 0x8001000c | (loaded << 21));
                    put32(c + 0x2c, 0x3C005A00 | (compared << 21));
                    put32(c + 0x34, 0x7c000000 | (compared << 16) | (loaded << 11));
                    put32(c + 0x48, 0x60000000);
                    ++p2p;
                }
            }
        } else if (chain != 0) {
            if (immediate(word(chain + 0x10)) == 0x12 && immediate(word(chain + 0x2c)) == 0x04 &&
                immediate(word(chain + 0x48)) == 0x18 && immediate(word(chain + 0x50)) == 0x00 &&
                immediate(word(chain + 0x54)) == 0x18) {
                int version = 0;
                if (immediate(word(chain + 0x3c)) == 0x12 && immediate(word(chain + 0x44)) == 0x0c) version = 1;
                else if (immediate(word(chain + 0x3c)) == 0x0c && immediate(word(chain + 0x44)) == 0x12) version = 2;
                std::uint8_t* m = bytes + chain;
                if (version == 2) put32(m + 0x3c, word(chain + 0x44));  // the other instruction order
                if (version != 0) {
                    const unsigned ry = compare_target(word(chain));
                    const unsigned rx = load_target(word(chain));
                    put32(m + 0x00, 0x38000004 | (rx << 21));
                    put32(m + 0x04, 0x7c00042c | (ry << 21) | (3 << 16) | (rx << 11));
                    put32(m + 0x14, 0x9000000c | (ry << 21) | (1 << 16));
                    put32(m + 0x18, 0x88000011 | (ry << 21) | (1 << 16));
                    put32(m + 0x28, 0x28000080 | (ry << 16));
                    put32(m + 0x38, 0x60000000);
                    put32(m + 0x44, 0x41810014);
                    ++master;
                }
            }
        }
    }
    if (gt2 && (master == 0 || p2p == 0)) return 2;
    return 0;
}

}  // namespace riftwii
