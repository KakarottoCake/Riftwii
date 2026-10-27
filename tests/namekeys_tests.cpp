// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The tile name keyboard's Shift (riftwii/namekeys.hpp): what each key
// types in each Shift, and how Shift moves as a name is typed.
#include <iostream>
#include <string>

#include "riftwii/namekeys.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using riftwii::NameShift;
using riftwii::name_key_char;

// The keys as the keyboard lays them out, row by row.
static const char* const kRows[] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM-'."};

static std::string RowTyped(const char* row, NameShift shift) {
    std::string out;
    for (const char* key = row; *key; ++key) out += name_key_char(*key, shift);
    return out;
}

static void TestEachShift() {
    // Off: lower case and the keys' own marks.
    EXPECT_EQ(RowTyped(kRows[0], NameShift::Off), "1234567890");
    EXPECT_EQ(RowTyped(kRows[1], NameShift::Off), "qwertyuiop");
    EXPECT_EQ(RowTyped(kRows[2], NameShift::Off), "asdfghjkl");
    EXPECT_EQ(RowTyped(kRows[3], NameShift::Off), "zxcvbnm-'.");
    // Automatic: capitals, and still the digits and marks, so a number
    // after a space types as a number.
    EXPECT_EQ(RowTyped(kRows[0], NameShift::Automatic), "1234567890");
    EXPECT_EQ(RowTyped(kRows[1], NameShift::Automatic), "QWERTYUIOP");
    EXPECT_EQ(RowTyped(kRows[3], NameShift::Automatic), "ZXCVBNM-'.");
    // On: capitals and the symbols.
    EXPECT_EQ(RowTyped(kRows[0], NameShift::On), "!?#+:,&/()");
    EXPECT_EQ(RowTyped(kRows[2], NameShift::On), "ASDFGHJKL");
    EXPECT_EQ(RowTyped(kRows[3], NameShift::On), "ZXCVBNM$[]");
}

static void TestSymbolKeys() {
    for (char key : std::string("1234567890-'.")) EXPECT_TRUE(riftwii::name_key_has_symbol(key));
    for (char key : std::string("AZaz ")) EXPECT_TRUE(!riftwii::name_key_has_symbol(key));
    EXPECT_TRUE(!riftwii::name_key_has_symbol('\0'));
    // A character no key has comes back as it is.
    EXPECT_EQ(name_key_char(' ', NameShift::On), ' ');
    EXPECT_EQ(name_key_char('!', NameShift::On), '!');
}

static void TestShiftKey() {
    EXPECT_EQ(riftwii::name_shift_pressed(NameShift::Off), NameShift::On);
    // From the automatic Shift one press gives the symbols, so a name can
    // start with "(" at once; lower case there takes a second press.
    EXPECT_EQ(riftwii::name_shift_pressed(NameShift::Automatic), NameShift::On);
    EXPECT_EQ(riftwii::name_shift_pressed(NameShift::On), NameShift::Off);
}

static void TestShiftForText() {
    EXPECT_EQ(riftwii::name_shift_for_text(""), NameShift::Automatic);
    EXPECT_EQ(riftwii::name_shift_for_text("Retro Rewind "), NameShift::Automatic);
    EXPECT_EQ(riftwii::name_shift_for_text("Retro"), NameShift::Off);
    EXPECT_EQ(riftwii::name_shift_for_text("Project+"), NameShift::Off);
    EXPECT_EQ(riftwii::name_shift_for_text("Ranger Cross ("), NameShift::Automatic);
    EXPECT_EQ(riftwii::name_shift_for_text("Chibi-Robo! ["), NameShift::Automatic);
    EXPECT_EQ(riftwii::name_shift_for_text("Ranger Cross (English)"), NameShift::Off);
    EXPECT_EQ(riftwii::name_shift_for_text("Chibi-"), NameShift::Off);
}

// Typing a whole name key by key, as the keyboard does: each key types in
// the Shift of the moment, then the Shift follows the text.
static std::string Type(const std::string& presses) {
    std::string text;
    NameShift shift = riftwii::name_shift_for_text(text);
    for (char press : presses) {
        if (press == '^') {  // the Shift key
            shift = riftwii::name_shift_pressed(shift);
            continue;
        }
        text += name_key_char(press, shift);
        shift = riftwii::name_shift_for_text(text);
    }
    return text;
}

static void TestWholeNames() {
    // Capitals come by themselves at each word's start; digits stay digits.
    EXPECT_EQ(Type("RETRO REWIND 6.0"), "Retro Rewind 6.0");
    EXPECT_EQ(Type("2 PLAYER"), "2 Player");
    // A pressed Shift gives one symbol, then the keys are back to normal.
    EXPECT_EQ(Type("PROJECT^4"), "Project+");
    EXPECT_EQ(Type("RANGER CROSS ^9ENGLISH^0"), "Ranger Cross (English)");
    EXPECT_EQ(Type("KAMEN RIDER^5 SUPER"), "Kamen Rider: Super");
    // A capital by itself after an opening bracket; after '-' it takes a press.
    EXPECT_EQ(Type("CHIBI-^ROBO^1 ^'ENG^."), "Chibi-Robo! [Eng]");
    EXPECT_EQ(Type("CHIBI-ROBO"), "Chibi-robo");
    EXPECT_EQ(Type("MODS ^3^2"), "Mods #?");
    // Lower case at the start: two presses (automatic, on, off).
    EXPECT_EQ(Type("^^MODS"), "mods");
    // A Shift pressed for a capital mid-word still works as before.
    EXPECT_EQ(Type("SMASH^B"), "SmashB");
}

int main() {
    TestEachShift();
    TestSymbolKeys();
    TestShiftKey();
    TestShiftForText();
    TestWholeNames();
    if (g_failures == 0) std::cout << "namekeys: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
