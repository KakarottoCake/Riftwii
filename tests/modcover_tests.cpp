// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A mod's own cover from GameTDB (riftwii/modcover.hpp, #385): the strict
// match, on the cases tried against GameTDB's database of 25 Oct 2026.
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/modcover.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using riftwii::CustomGame;

static void TestWords() {
    EXPECT_EQ(riftwii::mod_title_words("Super Smash Bros. Project+"), std::string("super smash bros project+"));
    EXPECT_EQ(riftwii::mod_title_words("  Retro  Rewind (v6.1) "), std::string("retro rewind v6 1"));
    EXPECT_TRUE(riftwii::title_has_mod_name("Super Smash Bros. Project+", "Project+"));
    EXPECT_TRUE(!riftwii::title_has_mod_name("Super Smash Bros. Project M", "Project+"));
    EXPECT_TRUE(!riftwii::title_has_mod_name("Super Smash Bros. Project+", "Project"));  // a whole word, '+' and all
    EXPECT_TRUE(riftwii::title_has_mod_name("Super Smash REX", "rex"));
    EXPECT_TRUE(!riftwii::title_has_mod_name("Super Smash REXtra", "REX"));  // not part of a word
    EXPECT_TRUE(!riftwii::title_has_mod_name("Anything", ""));
}

static void TestMatch() {
    const std::vector<CustomGame> kart = {
        {"RMCE01", "Mario Kart Wii"},
        {"RMCETO", "Retro Rewind"},
        {"RMCECT", "CTGP Revolution"},
        {"RMCEG2", "CTGP Revolution"},
        {"RMCEX1", "CTGP Revolution Deluxe"},
        {"RMCEW1", "Wiimm's Mario Kart Fun 2024-01"},
        {"RMCPTO", "Retro Rewind"},  // another region: not RMCE
    };
    EXPECT_EQ(riftwii::mod_cover_id(kart, "RMCE01", "Retro Rewind"), std::string("RMCETO"));
    EXPECT_EQ(riftwii::mod_cover_id(kart, "RMCP01", "Retro Rewind"), std::string("RMCPTO"));
    // Several match: no cover, never a guess.
    EXPECT_EQ(riftwii::mod_cover_id(kart, "RMCE01", "CTGP Revolution"), std::string());
    // None matches.
    EXPECT_EQ(riftwii::mod_cover_id(kart, "RMCE01", "Mario Kart Fun 2025"), std::string());
    // The stock game itself is no mod's cover.
    EXPECT_EQ(riftwii::mod_cover_id(kart, "RMCE01", "Mario Kart Wii"), std::string());

    const std::vector<CustomGame> brawl = {
        {"RSBEPL", "Super Smash Bros. Project+"},
        {"RSBERX", "Super Smash REX"},
        {"RSBEPM", "Super Smash Bros. Project M"},
    };
    EXPECT_EQ(riftwii::mod_cover_id(brawl, "RSBE01", "Project+"), std::string("RSBEPL"));
    EXPECT_EQ(riftwii::mod_cover_id(brawl, "RSBE01", "REX"), std::string("RSBERX"));
    EXPECT_EQ(riftwii::mod_cover_id(brawl, "RSBE01", "Project M"), std::string("RSBEPM"));
    // A short or odd stock ID finds nothing.
    EXPECT_EQ(riftwii::mod_cover_id(brawl, "RSB", "REX"), std::string());
}

int main() {
    TestWords();
    TestMatch();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "modcover tests passed" << std::endl;
    return 0;
}
