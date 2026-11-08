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
    std::size_t written = 0;
    const std::string text = riftwii::serialize_custom_games(games, &written);
    EXPECT_EQ(written, 2u);  // the short ID and the empty title left out
    bool damaged = true;
    const std::vector<CustomGame> back = riftwii::parse_custom_games(text, &damaged);
    EXPECT_TRUE(!damaged);  // what the writer makes is never damaged
    EXPECT_EQ(back.size(), 2u);
    EXPECT_TRUE(back.size() == 2 && back[0].id == "RMCETO" && back[0].title == "Retro Rewind");
    EXPECT_TRUE(back.size() == 2 && back[1].title == "Super Smash Bros. Project+");
    // Damage is skipped but reported, CRLF read.
    const std::vector<CustomGame> odd =
        riftwii::parse_custom_games("# x\r\nRMCETO\tRetro Rewind\r\nno tab\nrmceto\tlower\n", &damaged);
    EXPECT_TRUE(odd.size() == 1 && odd[0].title == "Retro Rewind");
    EXPECT_TRUE(damaged);
    for (const char* bad : {"no tab\n", "rmceto\tlower ID\n", "RMCETO\t\n"}) {
        damaged = false;
        riftwii::parse_custom_games(std::string("RMCETO\tRetro Rewind\n") + bad, &damaged);
        EXPECT_TRUE(damaged);
    }
    damaged = true;
    riftwii::parse_custom_games("# only a comment\n\r\n\n", &damaged);
    EXPECT_TRUE(!damaged);
    // As many as the writer keeps is whole; one more is damage, and still
    // only the bound is read.
    std::string full;
    for (std::size_t i = 0; i < riftwii::kMaxCustomGames; ++i) full += "RMCETO\tRetro Rewind\n";
    damaged = true;
    EXPECT_EQ(riftwii::parse_custom_games(full, &damaged).size(), riftwii::kMaxCustomGames);
    EXPECT_TRUE(!damaged);
    full += "RMCETO\tRetro Rewind\n";
    EXPECT_EQ(riftwii::parse_custom_games(full, &damaged).size(), riftwii::kMaxCustomGames);
    EXPECT_TRUE(damaged);
    EXPECT_EQ(riftwii::parse_custom_games(full).size(), riftwii::kMaxCustomGames);
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

// A mod tile's own 3D cover and full box: which names the stores take,
// which GameTDB entry a download asks for, and when one is wanted.
static void TestTileArt() {
    // The stores take games, homebrew apps and mod tiles, nothing else.
    EXPECT_TRUE(riftwii::valid_art_name("RSBE01"));
    EXPECT_TRUE(riftwii::valid_art_name("9XGX"));
    EXPECT_TRUE(riftwii::valid_art_name("RSBE01@project-v3-2-1"));
    EXPECT_TRUE(!riftwii::valid_art_name("RSBE01@"));
    EXPECT_TRUE(!riftwii::valid_art_name("RSBE01@Project"));       // a slug is lowercase
    EXPECT_TRUE(!riftwii::valid_art_name("RSBE01@../../boot"));    // never out of its folder
    EXPECT_TRUE(!riftwii::valid_art_name("rsbe01"));
    EXPECT_TRUE(!riftwii::valid_art_name("RSBE0"));
    EXPECT_TRUE(!riftwii::valid_art_name(""));
    EXPECT_TRUE(riftwii::is_tile_art_name("RMCE01@retro-rewind-v6-13-1"));
    EXPECT_TRUE(!riftwii::is_tile_art_name("RMCE01"));

    // A game asks for its own; a tile for its mod's entry, or for nothing.
    EXPECT_EQ(riftwii::art_download_id("RSBE01", ""), std::string("RSBE01"));
    EXPECT_EQ(riftwii::art_download_id("RSBE01", "RSBEPL"), std::string("RSBE01"));
    EXPECT_EQ(riftwii::art_download_id("RSBE01@project-v3-2-1", "RSBEPL"), std::string("RSBEPL"));
    EXPECT_EQ(riftwii::art_download_id("RMCE01@retro-rewind-v6-13-1", "RMCETO"), std::string("RMCETO"));
    EXPECT_EQ(riftwii::art_download_id("RSBE01@my-own-build", ""), std::string());  // not listed: never the game's
    EXPECT_EQ(riftwii::art_download_id("RSBE01@x", "../a"), std::string());        // only a game's six characters
    EXPECT_EQ(riftwii::art_download_id("RSBE01@Bad", "RSBEPL"), std::string());

    // What is stored is the tile's only while it matches the same entry.
    EXPECT_TRUE(riftwii::tile_art_current("RSBEPL", "RSBEPL"));
    EXPECT_TRUE(!riftwii::tile_art_current("RSBEPM", "RSBEPL"));  // renamed from Project M
    EXPECT_TRUE(!riftwii::tile_art_current("RSBEPL", ""));        // matches nothing now
    EXPECT_TRUE(!riftwii::tile_art_current("", "RSBEPL"));        // stored before the .mod note

    riftwii::StoredArt none;
    riftwii::StoredArt picture;
    picture.picture = true;
    riftwii::StoredArt missed;
    missed.recent_miss = true;
    // A game: wanted until it has a picture or GameTDB recently had none.
    EXPECT_TRUE(riftwii::art_wanted("RSBE01", "", none));
    EXPECT_TRUE(!riftwii::art_wanted("RSBE01", "", picture));
    EXPECT_TRUE(!riftwii::art_wanted("RSBE01", "", missed));
    EXPECT_TRUE(!riftwii::art_wanted("bad", "", none));
    // A tile GameTDB doesn't list wants nothing (it shows the game's).
    EXPECT_TRUE(!riftwii::art_wanted("RSBE01@my-own-build", "", none));
    // A listed tile: wanted, and again when what is stored is another entry's.
    EXPECT_TRUE(riftwii::art_wanted("RSBE01@project-v3-2-1", "RSBEPL", none));
    picture.from = "RSBEPL";
    missed.from = "RSBEPL";
    EXPECT_TRUE(!riftwii::art_wanted("RSBE01@project-v3-2-1", "RSBEPL", picture));
    EXPECT_TRUE(!riftwii::art_wanted("RSBE01@project-v3-2-1", "RSBEPL", missed));
    EXPECT_TRUE(riftwii::art_wanted("RSBE01@project-v3-2-1", "RSBEPM", picture));
    EXPECT_TRUE(riftwii::art_wanted("RSBE01@project-v3-2-1", "RSBEPM", missed));
    // Super Smash REX has a 3D cover but no full box: its miss holds a week.
    missed.from = "RSBERX";
    EXPECT_TRUE(!riftwii::art_wanted("RSBE01@super-smash-rex", "RSBERX", missed));

    // The order a tile's pictures come in, where a 3D cover or box can stand.
    using riftwii::tile_art_order;
    // The player's own PNG for the tile: never the mod's art, the PNG instead of the game's.
    EXPECT_TRUE(!tile_art_order(true, true, true).tile_art_first);
    EXPECT_TRUE(tile_art_order(true, true, true).flat_before_game_art);
    EXPECT_TRUE(tile_art_order(true, true, false).flat_before_game_art);
    // The mod's own flat cover in memory: the mod's 3D cover or box first,
    // then that flat cover, never the stock game's.
    EXPECT_TRUE(tile_art_order(true, false, true).tile_art_first);
    EXPECT_TRUE(tile_art_order(true, false, true).flat_before_game_art);
    // No flat cover yet (or only the game's downloaded one): the mod's art,
    // then the game's.
    EXPECT_TRUE(tile_art_order(true, false, false).tile_art_first);
    EXPECT_TRUE(!tile_art_order(true, false, false).flat_before_game_art);
    // A game: no tile art; its own PNG or second set first, else its 3D cover or box.
    EXPECT_TRUE(!tile_art_order(false, false, true).tile_art_first);
    EXPECT_TRUE(tile_art_order(false, false, true).flat_before_game_art);
    EXPECT_TRUE(!tile_art_order(false, false, false).tile_art_first);
    EXPECT_TRUE(!tile_art_order(false, false, false).flat_before_game_art);
}

// A game page's box downloads: each once a visit, and a tile renamed while
// Home's download of its old entry's box runs still gets the new entry's.
static void TestPageBoxAsks() {
    using riftwii::PageBoxAsks;
    const std::string tile = "RSBE01@project-plus";
    EXPECT_EQ(PageBoxAsks::key("RSBE01", "RSBE01", ""), std::string("RSBE01"));
    EXPECT_EQ(PageBoxAsks::key(tile, "RSBE01", "RSBEPL"), std::string("RSBE01@project-plus|RSBEPL"));
    const std::string old_key = PageBoxAsks::key(tile, "RSBE01", "RSBEPM");
    const std::string new_key = PageBoxAsks::key(tile, "RSBE01", "RSBEPL");

    // Home started the tile's box for Project M; the tile is renamed to Project+.
    PageBoxAsks page;
    EXPECT_TRUE(!page.start(new_key, old_key, false));  // the old one's download isn't the new box's
    EXPECT_TRUE(!page.asked(new_key));
    EXPECT_TRUE(!page.asked(old_key));
    // Once it ends, the new entry's box starts and counts as asked.
    EXPECT_TRUE(!page.start(new_key, "", false));  // could not start yet: still not asked
    EXPECT_TRUE(!page.asked(new_key));
    EXPECT_TRUE(page.start(new_key, "", true));
    EXPECT_TRUE(page.asked(new_key));

    // A download Home started for the same entry is this one.
    PageBoxAsks same;
    EXPECT_TRUE(same.start(new_key, new_key, false));
    EXPECT_TRUE(same.asked(new_key));
    // The page's own download, seen running on later passes, too.
    PageBoxAsks own;
    EXPECT_TRUE(own.start("RSBE01", "", true));
    EXPECT_TRUE(own.start("RSBE01", "RSBE01", false));
    EXPECT_TRUE(own.asked("RSBE01") && !own.asked(new_key));

    // Project M's box got no answer (asked, still wanted); renamed to
    // Project+ and back: Project M's box may be tried again. An unchanged
    // match keeps its one try.
    PageBoxAsks back;
    back.tile_matched(tile, old_key);
    EXPECT_TRUE(back.start(old_key, "", true));
    back.tile_matched(tile, old_key);
    EXPECT_TRUE(back.asked(old_key));
    back.tile_matched(tile, new_key);
    back.tile_matched(tile, old_key);
    EXPECT_TRUE(!back.asked(old_key));
    // The game's own box is not the tile's: kept across the tile's renames.
    EXPECT_TRUE(back.start("RSBE01", "", true));
    back.tile_matched(tile, new_key);
    EXPECT_TRUE(back.asked("RSBE01"));
}

int main() {
    TestWords();
    TestTileArt();
    TestPageBoxAsks();
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
