// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Game details from GameTDB (riftwii/gametdb.hpp): picking the wanted
// <game> blocks out of wiitdb.xml fed in pieces, reading one, the limits on
// network data, and the cache file round trip.
#include <iostream>
#include <string>

#include "riftwii/gametdb.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

// Shaped like a wiitdb.xml entry.
static const char* kKart =
    "<game name=\"Mario Kart Wii (USA) (EN,FR,ES)\">\n"
    "\t<id>RMCE01</id>\n"
    "\t<type/>\n"
    "\t<region>NTSC-U</region>\n"
    "\t<languages>EN,FR,ES</languages>\n"
    "\t<locale lang=\"EN\">\n"
    "\t\t<title>Mario Kart Wii</title>\n"
    "\t\t<synopsis>Race with friends.\nUse the Wii Wheel.</synopsis>\n"
    "\t</locale>\n"
    "\t<locale lang=\"FR\">\n"
    "\t\t<title>Mario Kart Wii</title>\n"
    "\t\t<synopsis>Courez entre amis.</synopsis>\n"
    "\t</locale>\n"
    "\t<developer>Nintendo EAD</developer>\n"
    "\t<publisher>Nintendo</publisher>\n"
    "\t<date year=\"2008\" month=\"4\" day=\"27\"/>\n"
    "\t<genre>racing,kart racing</genre>\n"
    "\t<rating type=\"ESRB\" value=\"E\"><descriptor>comic mischief</descriptor></rating>\n"
    "\t<wi-fi players=\"12\"><feature>online</feature><feature>score</feature></wi-fi>\n"
    "\t<input players=\"4\">\n"
    "\t\t<control type=\"wiimote\" required=\"true\"/>\n"
    "\t\t<control type=\"wheel\" required=\"false\"/>\n"
    "\t\t<control type=\"gamecube\" required=\"false\"/>\n"
    "\t</input>\n"
    "</game>\n";

static const char* kOther =
    "<game name=\"Other\"><id>RSBE01</id><locale lang=\"EN\"><synopsis>Fight.</synopsis></locale>"
    "<input players=\"4\"/></game>\n";

static std::string sample() {
    return std::string("<?xml version=\"1.0\"?>\n<datafile>\n<WiiTDB version=\"1\" games=\"3\"/>\n") + kOther + kKart +
           "</datafile>\n";
}

static void test_parse_one() {
    GameInfo g;
    EXPECT_TRUE(parse_wiitdb_game(kKart, "EN", g));
    EXPECT_EQ(g.id, "RMCE01");
    EXPECT_EQ(g.developer, "Nintendo EAD");
    EXPECT_EQ(g.publisher, "Nintendo");
    EXPECT_EQ(g.genres.size(), 2u);
    EXPECT_EQ(g.genres[1], "kart racing");
    EXPECT_EQ(g.year, 2008);
    EXPECT_EQ(g.month, 4);
    EXPECT_EQ(g.day, 27);
    EXPECT_EQ(g.rating_system, "ESRB");
    EXPECT_EQ(g.rating, "E");
    EXPECT_EQ(g.players, 4);
    EXPECT_EQ(g.online_players, 12);
    EXPECT_EQ(g.online_features.size(), 2u);
    EXPECT_EQ(g.controls.size(), 3u);
    EXPECT_TRUE(g.controls[0].required);
    EXPECT_FALSE(g.controls[1].required);
    EXPECT_EQ(g.synopsis, "Race with friends.\nUse the Wii Wheel.");

    GameInfo french;
    EXPECT_TRUE(parse_wiitdb_game(kKart, "FR", french));
    EXPECT_EQ(french.synopsis, "Courez entre amis.");
    GameInfo japanese;  // none in Japanese: the English one
    EXPECT_TRUE(parse_wiitdb_game(kKart, "JA", japanese));
    EXPECT_EQ(japanese.synopsis, "Race with friends.\nUse the Wii Wheel.");
}

static void test_bad_blocks() {
    GameInfo g;
    EXPECT_FALSE(parse_wiitdb_game("<game><id>../../x</id></game>", "EN", g));
    EXPECT_FALSE(parse_wiitdb_game("<game><id>rmce01</id></game>", "EN", g));
    EXPECT_FALSE(parse_wiitdb_game("<game><id>RMCE01</id>", "EN", g));
    EXPECT_FALSE(parse_wiitdb_game("<notgame/>", "EN", g));
    // Values out of range are held to it, long text is cut.
    const std::string wild = "<game><id>RXXE01</id><date year=\"99999\" month=\"-3\"/><input players=\"100000\"/>"
                             "<developer>" + std::string(5000, 'a') + "</developer></game>";
    EXPECT_TRUE(parse_wiitdb_game(wild, "EN", g));
    EXPECT_EQ(g.year, 2100);
    EXPECT_EQ(g.month, 0);
    EXPECT_EQ(g.players, 99);
    EXPECT_EQ(g.developer.size(), 200u);
    // A cut never splits a UTF-8 character ("é" is two bytes).
    std::string accents;
    for (int i = 0; i < 150; ++i) accents += "\xC3\xA9";
    EXPECT_TRUE(parse_wiitdb_game("<game><id>RXXE01</id><developer>" + accents + "</developer></game>", "EN", g));
    EXPECT_EQ(g.developer.size(), 200u);
    EXPECT_EQ(static_cast<unsigned char>(g.developer[198]), 0xC3u);
}

static void test_scanner_pieces() {
    const std::string xml = sample();
    // Every piece size, down to a byte at a time.
    for (std::size_t piece : {std::size_t(1), std::size_t(3), std::size_t(7), std::size_t(64), xml.size()}) {
        WiitdbScanner scanner({"RMCE01"}, "EN");
        for (std::size_t at = 0; at < xml.size(); at += piece)
            scanner.feed(xml.data() + at, std::min(piece, xml.size() - at));
        EXPECT_EQ(scanner.blocks(), 2u);
        EXPECT_EQ(scanner.games().size(), 1u);
        EXPECT_TRUE(scanner.games().count("RMCE01") == 1);
        if (scanner.games().count("RMCE01")) EXPECT_EQ(scanner.games().at("RMCE01").players, 4);
    }
}

static void test_scanner_oversized() {
    WiitdbScanner scanner({"RMCE01", "RBIG01"}, "EN");
    std::string big = "<game name=\"big\"><id>RBIG01</id><synopsis>";
    big += std::string(WiitdbScanner::kMaxBlock + 100, 'x');
    big += "</synopsis></game>\n";
    scanner.feed(big.data(), big.size());
    scanner.feed(kKart, std::string(kKart).size());
    EXPECT_EQ(scanner.games().size(), 1u);
    EXPECT_TRUE(scanner.games().count("RMCE01") == 1);
    EXPECT_EQ(scanner.skipped(), 1u);
}

static void test_cache_round_trip() {
    std::map<std::string, GameInfo> games;
    GameInfo g;
    EXPECT_TRUE(parse_wiitdb_game(kKart, "EN", g));
    g.publisher = "Back\\slash";
    games[g.id] = g;
    const std::string text = serialize_game_infos(games);
    std::map<std::string, GameInfo> back;
    EXPECT_TRUE(parse_game_infos(text, back));
    EXPECT_EQ(back.size(), 1u);
    const GameInfo& r = back["RMCE01"];
    EXPECT_EQ(r.id, "RMCE01");
    EXPECT_EQ(r.publisher, "Back\\slash");
    EXPECT_EQ(r.synopsis, g.synopsis);
    EXPECT_EQ(r.genres.size(), 2u);
    EXPECT_EQ(r.year, 2008);
    EXPECT_EQ(r.month, 4);
    EXPECT_EQ(r.day, 27);
    EXPECT_EQ(r.rating_system, "ESRB");
    EXPECT_EQ(r.rating, "E");
    EXPECT_EQ(r.players, 4);
    EXPECT_EQ(r.online_players, 12);
    EXPECT_EQ(r.controls.size(), 3u);
    if (r.controls.size() == 3) {
        EXPECT_EQ(r.controls[0].type, "wiimote");
        EXPECT_TRUE(r.controls[0].required);
        EXPECT_FALSE(r.controls[2].required);
    }
    // A damaged cache: bad IDs and stray lines are passed over.
    std::map<std::string, GameInfo> damaged;
    EXPECT_TRUE(parse_game_infos("players=4\n[../x]\nplayers=2\n[RSBE01]\nplayers=abc\nnonsense\n", damaged));
    EXPECT_EQ(damaged.size(), 1u);
    EXPECT_EQ(damaged["RSBE01"].players, 0);
}

static void test_locale() {
    EXPECT_EQ(gametdb_locale("en"), "EN");
    EXPECT_EQ(gametdb_locale("ja"), "JA");
    EXPECT_EQ(gametdb_locale("ko"), "KO");
    EXPECT_EQ(gametdb_locale("??"), "EN");
}

static void test_text() {
    EXPECT_EQ(plain_punctuation("There\xE2\x80\x99s \xE2\x80\x9Cmore\xE2\x80\x9D\xE2\x80\xA6"), "There's \"more\"...");
    EXPECT_EQ(plain_punctuation("\xE2\x80\xA2 a\xC2\xA0" "b"), "- a b");

    std::vector<std::string> lines = wrap_columns("one two three four", 9);
    EXPECT_EQ(lines.size(), 3u);
    if (lines.size() == 3) {
        EXPECT_EQ(lines[0], "one two");
        EXPECT_EQ(lines[1], "three");  // "three four" is 10
        EXPECT_EQ(lines[2], "four");
    }
    lines = wrap_columns("one two three four", 10);
    EXPECT_EQ(lines.size(), 2u);
    if (lines.size() == 2) EXPECT_EQ(lines[1], "three four");
    // A word longer than a line is cut.
    lines = wrap_columns("abcdefghij", 4);
    EXPECT_EQ(lines.size(), 3u);
    if (lines.size() == 3) EXPECT_EQ(lines[2], "ij");
    // Paragraphs: blank lines run together into one, none at the ends.
    lines = wrap_columns("\nfirst\n \n\n\nsecond\n\n", 20);
    EXPECT_EQ(lines.size(), 3u);
    if (lines.size() == 3) {
        EXPECT_EQ(lines[0], "first");
        EXPECT_TRUE(lines[1].empty());
        EXPECT_EQ(lines[2], "second");
    }
    // Japanese: two columns a character, cut between characters.
    const std::string kana = "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86\xE3\x81\x88\xE3\x81\x8A";  // five kana
    lines = wrap_columns(kana, 4);
    EXPECT_EQ(lines.size(), 3u);
    if (lines.size() == 3) {
        EXPECT_EQ(lines[0], "\xE3\x81\x82\xE3\x81\x84");
        EXPECT_EQ(lines[2], "\xE3\x81\x8A");
    }
}

int main() {
    test_text();
    test_parse_one();
    test_bad_blocks();
    test_scanner_pieces();
    test_scanner_oversized();
    test_cache_round_trip();
    test_locale();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "gametdb tests passed" << std::endl;
    return 0;
}
