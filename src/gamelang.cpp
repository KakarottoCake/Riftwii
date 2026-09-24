// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gamelang.hpp"

namespace riftwii {
namespace {

const char* const kNames[kGameLanguages] = {"ja", "en", "de", "fr", "es", "it", "nl", "zh-hans", "zh-hant", "ko"};

// SCGetLanguage: extsb. r0,r3 / bne +0x10 / li r0,0 marks the check of
// the setting; the load returned to the game follows a few words later.
constexpr std::uint32_t kPattern[3] = {0x7C600775, 0x40820010, 0x38000000};
constexpr std::uint32_t kLoad = 0x88610008;  // lbz r3,8(r1)
constexpr std::size_t kSearchWords = 64;     // the function is short

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

}  // namespace

const char* game_language_name(int code) {
    return code >= 0 && code < kGameLanguages ? kNames[code] : "console";
}

bool parse_game_language(const std::string& s, int& code) {
    if (s == "console") {
        code = -1;
        return true;
    }
    for (int i = 0; i < kGameLanguages; ++i) {
        if (s == kNames[i]) {
            code = i;
            return true;
        }
    }
    return false;
}

unsigned patch_game_language(std::uint8_t* bytes, std::size_t size, int code) {
    if (code < 0 || code >= kGameLanguages) return 0;
    unsigned patched = 0;
    for (std::size_t at = 0; at + 12 <= size; at += 4) {
        if (be32(bytes + at) != kPattern[0] || be32(bytes + at + 4) != kPattern[1] ||
            be32(bytes + at + 8) != kPattern[2]) {
            continue;
        }
        for (std::size_t w = 3; w < kSearchWords && at + w * 4 + 4 <= size; ++w) {
            if (be32(bytes + at + w * 4) == kLoad) {
                put32(bytes + at + w * 4, 0x38600000u | static_cast<std::uint32_t>(code));  // li r3,code
                ++patched;
                break;
            }
        }
        at += 8;
    }
    return patched;
}

}  // namespace riftwii
