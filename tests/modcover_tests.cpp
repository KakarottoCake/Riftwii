// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A mod's own cover from GameTDB (riftwii/modcover.hpp, #385): the strict
// match, on the cases tried against GameTDB's database of 25 Oct 2026.
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/gametdb.hpp"
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

static void TestBlocks() {
    CustomGame g;
    EXPECT_TRUE(riftwii::parse_wiitdb_custom(
        "<game name=\"Retro Rewind (EN)\"><id>RMCETO</id><type> CUSTOM </type>"
        "<locale lang=\"EN\"><title>Retro Rewind</title></locale></game>", g));
    EXPECT_EQ(g.id, std::string("RMCETO"));
    EXPECT_EQ(g.title, std::string("Retro Rewind"));
    // No English title: its name.
    EXPECT_TRUE(riftwii::parse_wiitdb_custom(
        "<game name=\"Super Smash Bros. Project+\"><id>RSBEPL</id><type>CUSTOM</type></game>", g));
    EXPECT_EQ(g.title, std::string("Super Smash Bros. Project+"));
    // Not a custom game, a short ID, no title, not XML.
    EXPECT_TRUE(!riftwii::parse_wiitdb_custom("<game name=\"Mario Kart Wii\"><id>RMCE01</id></game>", g));
    EXPECT_TRUE(!riftwii::parse_wiitdb_custom("<game name=\"X\"><id>RMCE</id><type>CUSTOM</type></game>", g));
    EXPECT_TRUE(!riftwii::parse_wiitdb_custom("<game name=\"\"><id>RMCETO</id><type>CUSTOM</type></game>", g));
    EXPECT_TRUE(!riftwii::parse_wiitdb_custom("<game", g));

    EXPECT_EQ(riftwii::custom_prefix("RMCE01"), std::string("RMCE"));
    EXPECT_EQ(riftwii::custom_prefix("HAAA"), std::string());  // a channel: no mods listed by it
    EXPECT_EQ(riftwii::custom_prefixes({"RMCE01", "RSBE01", "RMCP01", "HAAA"}).size(), 3u);
}

static void TestFile() {
    const std::vector<CustomGame> games = {{"RMCETO", "Retro Rewind"}, {"RSBEPL", "Super\tSmash\nBros. Project+"},
                                           {"BAD", "Short ID"}, {"RSBERX", ""}};
    const std::string text = riftwii::serialize_custom_games(games);
    const std::vector<CustomGame> back = riftwii::parse_custom_games(text);
    EXPECT_EQ(back.size(), 2u);
    EXPECT_TRUE(back.size() == 2 && back[0].id == "RMCETO" && back[0].title == "Retro Rewind");
    EXPECT_TRUE(back.size() == 2 && back[1].title == "Super Smash Bros. Project+");
    // Damage is skipped, CRLF read.
    const std::vector<CustomGame> odd = riftwii::parse_custom_games("# x\r\nRMCETO\tRetro Rewind\r\nno tab\nrmceto\tlower\n");
    EXPECT_TRUE(odd.size() == 1 && odd[0].title == "Retro Rewind");
    // A title past the bound is cut on a whole character.
    const std::string longer(200, 'a');
    const std::vector<CustomGame> cut = riftwii::parse_custom_games("RMCETO\t" + longer + "\n");
    EXPECT_TRUE(cut.size() == 1 && cut[0].title.size() == riftwii::kMaxCustomTitleBytes);
}

static void TestScanner() {
    riftwii::WiitdbScanner scanner({"RMCE01"}, "EN");
    const std::string xml =
        "<datafile>"
        "<game name=\"Mario Kart Wii\"><id>RMCE01</id><type></type></game>"
        "<game name=\"Retro Rewind\"><id>RMCETO</id><type>CUSTOM</type></game>"
        "<game name=\"Project+\"><id>RSBEPL</id><type>CUSTOM</type></game>"  // not a wanted game's
        "<game name=\"Mario Kart Wii (KOR)\"><id>RMCK01</id><type></type></game>"
        "</datafile>";
    // In pieces, as it is unpacked.
    for (std::size_t at = 0; at < xml.size(); at += 7) scanner.feed(xml.data() + at, std::min<std::size_t>(7, xml.size() - at));
    EXPECT_EQ(scanner.customs().size(), 1u);
    EXPECT_TRUE(!scanner.customs().empty() && scanner.customs()[0].id == "RMCETO");
    EXPECT_EQ(scanner.games().size(), 1u);

    // A mod installed as a game of its own (wanted) is still among the
    // customs, and a type in any case counts: with it and a near namesake both listed, the name matches
    // two entries, so no cover.
    riftwii::WiitdbScanner installed({"RMCE01", "RMCETO"}, "EN");
    const std::string two =
        "<datafile>"
        "<game name=\"Retro Rewind\"><id>RMCETO</id><type>CUSTOM</type></game>"
        "<game name=\"Retro Rewind Plus\"><id>RMCEXX</id><type>Custom</type></game>"
        "</datafile>";
    installed.feed(two.data(), two.size());
    EXPECT_EQ(installed.customs().size(), 2u);
    EXPECT_EQ(riftwii::mod_cover_id(installed.customs(), "RMCE01", "Retro Rewind"), std::string());
    EXPECT_EQ(installed.games().count("RMCETO"), 1u);  // and still its own game's details

    // The tile's own game is itself a custom game whose title matches: with
    // a near namesake, two entries match, so no cover.
    const std::vector<CustomGame> newer = {{"SMNE02", "Newer Super Mario Bros. Wii"},
                                           {"SMNE03", "Newer Super Mario Bros. Wii Holiday Special"}};
    EXPECT_EQ(riftwii::mod_cover_id(newer, "SMNE02", "Newer Super Mario Bros. Wii"), std::string());
    // A name only the other one has still finds it.
    EXPECT_EQ(riftwii::mod_cover_id(newer, "SMNE02", "Holiday Special"), std::string("SMNE03"));
}

int main() {
    TestWords();
    TestMatch();
    TestBlocks();
    TestFile();
    TestScanner();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "modcover tests passed" << std::endl;
    return 0;
}
