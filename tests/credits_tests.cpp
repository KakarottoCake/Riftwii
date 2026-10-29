// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "credits.hpp"
#include "i18n.hpp"
#include "stash.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;
std::string attribution;

#define EXPECT(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

std::vector<std::string> Wrapped(const std::string& text, std::size_t width) {
    attribution = text;
    auto lines = riftwii::wii::CreditsLines(width);
    // The absent stash appends a fixed license-download message after the credits.
    lines.pop_back();
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

bool CompleteUtf8(const std::string& text) {
    unsigned remaining = 0;
    for (const unsigned char byte : text) {
        if (remaining) {
            if ((byte & 0xc0) != 0x80) return false;
            --remaining;
        } else if (byte < 0x80) {
            continue;
        } else if ((byte & 0xe0) == 0xc0) {
            remaining = 1;
        } else if ((byte & 0xf0) == 0xe0) {
            remaining = 2;
        } else if ((byte & 0xf8) == 0xf0) {
            remaining = 3;
        } else {
            return false;
        }
    }
    return remaining == 0;
}

void TranslatedAttribution() {
    const std::string japanese =
        "エミュレータのカバー：libretro-thumbnailsの参加者、MobyGames、Fandom。予備のカバー："
        "SteamGridDBの参加者。元の画像はゲームの開発者と発売元のものです。";
    const auto lines = Wrapped(japanese, 60);
    std::string joined;
    for (const auto& line : lines) {
        EXPECT(line.size() <= 60);
        EXPECT(CompleteUtf8(line));
        joined += line;
    }
    EXPECT(joined == japanese);
}

void CharacterBoundaries() {
    for (const std::string& character : {std::string("é"), std::string("字"), std::string("😀")}) {
        for (std::size_t prefix = 57; prefix < 60; ++prefix) {
            const std::string text = std::string(prefix, 'a') + character + "tail";
            const auto lines = Wrapped(text, 60);
            std::string joined;
            for (const auto& line : lines) {
                EXPECT(line.size() <= 60);
                EXPECT(CompleteUtf8(line));
                joined += line;
            }
            EXPECT(joined == text);
        }
        // Narrow widths must keep a character whole and finish the paragraph.
        EXPECT(Wrapped(character + character, 1) == (std::vector<std::string>{character, character}));
        EXPECT(Wrapped(character, 0) == (std::vector<std::string>{character}));
    }
}

}  // namespace

namespace riftwii::wii {

const char* tr(const char* english) {
    // Isolate the translated attribution while still exercising CreditsLines.
    return std::string(english).rfind("Emulator covers:", 0) == 0 ? attribution.c_str() : "";
}

namespace stash {
bool Find(const char*, const u8*&, u32&) { return false; }
}  // namespace stash

}  // namespace riftwii::wii

int main() {
    TranslatedAttribution();
    CharacterBoundaries();
    EXPECT(Wrapped("one two three", 7) == (std::vector<std::string>{"one two", "three"}));
    EXPECT(Wrapped("longword", 4) == (std::vector<std::string>{"long", "word"}));
    return failures ? 1 : 0;
}
