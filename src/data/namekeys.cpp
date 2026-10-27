// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/namekeys.hpp"

#include <cctype>
#include <cstring>

namespace riftwii {
namespace {

// The keys that carry a symbol, and the symbol each one types with Shift
// on, place for place. The number row follows a US keyboard where the
// symbol is one names use ('!' on 1, '#' on 3, '&' on 7, '(' and ')' on
// 9 and 0); the bottom row's '-', '\'' and '.' give '$' and the brackets
// side by side, as the parentheses are.
constexpr char kSymbolKeys[] = "1234567890-'.";
constexpr char kSymbols[] = "!?#+:,&/()$[]";
static_assert(sizeof kSymbolKeys == sizeof kSymbols, "one symbol per key");

const char* symbol_slot(char key) {
    if (key == '\0') return nullptr;
    return std::strchr(kSymbolKeys, key);
}

}  // namespace

bool name_key_has_symbol(char key) { return symbol_slot(key) != nullptr; }

char name_key_char(char key, NameShift shift) {
    const unsigned char letter = static_cast<unsigned char>(key);
    if (std::isalpha(letter)) {
        const bool capital = shift != NameShift::Off;
        return static_cast<char>(capital ? std::toupper(letter) : std::tolower(letter));
    }
    const char* slot = symbol_slot(key);
    if (slot && shift == NameShift::On) return kSymbols[slot - kSymbolKeys];
    return key;
}

NameShift name_shift_for_text(const std::string& text) {
    // After an opening bracket too: "Ranger Cross (English)", "[Eng]".
    const bool word_start = text.empty() || text.back() == ' ' || text.back() == '(' || text.back() == '[';
    return word_start ? NameShift::Automatic : NameShift::Off;
}

NameShift name_shift_pressed(NameShift shift) {
    return shift == NameShift::On ? NameShift::Off : NameShift::On;
}

}  // namespace riftwii
