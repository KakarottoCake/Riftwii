// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The games Home remembers from its last look at the drives
// (riftwii/homelist.hpp): the file written and read back, and every way a
// file that is not whole, or not RiftWii's, gives no games.
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/homelist.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using riftwii::RememberedGame;
using Games = std::vector<RememberedGame>;

static RememberedGame Game(bool usb, const std::string& id, const std::string& name, const std::string& path) {
    RememberedGame game;
    game.usb = usb;
    game.id = id;
    game.name = name;
    game.path = path;
    return game;
}

static bool Same(const RememberedGame& a, const RememberedGame& b) {
    return a.usb == b.usb && a.id == b.id && a.disc_number == b.disc_number && a.gamecube == b.gamecube &&
           a.has_mods == b.has_mods && a.name == b.name && a.path == b.path && a.patch_source == b.patch_source;
}

// A few games of every kind Home lists from a drive.
static Games Library() {
    Games games;
    games.push_back(Game(true, "RMCE01", "Mario Kart Wii", "usb:/wbfs/Mario Kart Wii [RMCE01]/RMCE01.wbfs"));
    games.back().has_mods = true;
    games.push_back(Game(false, "GALE01", "Super Smash Bros. Melee", "sd:/games/Melee/game.iso"));
    games.back().gamecube = true;
    games.push_back(Game(true, "GM4E01", "Mario Kart: Double Dash!!", "usb2:/games/Double Dash/disc2.iso"));
    games.back().gamecube = true;
    games.back().has_mods = true;
    games.back().disc_number = 1;
    // A drive formatted as WBFS: the game is a slot, not a file.
    games.push_back(Game(true, "SMNE01", "New Super Mario Bros. Wii", "usb:/wbfs slot 12"));
    // A patch-only GameCube mod, listed as the game it makes.
    games.push_back(Game(false, "GZLE99", "Wind Waker: a mod", "sd:/games/mods/mod.xdelta"));
    games.back().gamecube = true;
    games.back().patch_source = "usb:/games/Wind Waker/game.iso";
    // A name in another script, and one with spaces at its ends.
    games.push_back(Game(false, "RSBJ01", "大乱闘スマッシュブラザーズX", "sd:/wbfs/RSBJ01.wbfs"));
    games.push_back(Game(false, "R8PE01", " Super Paper Mario ", "sd:/wbfs/Super Paper Mario [R8PE01]/R8PE01.wbfs"));
    // A WiiWare-style four-character ID in an image's name.
    games.push_back(Game(true, "WBME", "A four-letter game", "usb:/wbfs/WBME.wbfs"));
    return games;
}

static void TestRoundTrip() {
    const Games games = Library();
    const std::string text = riftwii::serialize_home_list(games);
    Games read;
    EXPECT_TRUE(riftwii::parse_home_list(text, read));
    EXPECT_EQ(read.size(), games.size());
    for (std::size_t i = 0; i < games.size() && i < read.size(); ++i) EXPECT_TRUE(Same(games[i], read[i]));
    // Written again, the same file: Home writes the card only when the
    // games changed.
    EXPECT_EQ(riftwii::serialize_home_list(read), text);

    // No games: still a whole file, and it reads as none.
    const std::string empty = riftwii::serialize_home_list({});
    EXPECT_TRUE(riftwii::parse_home_list(empty, read));
    EXPECT_TRUE(read.empty());

    // Windows line ends (the file opened and saved on a computer).
    std::string crlf;
    for (const char c : text) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    EXPECT_TRUE(riftwii::parse_home_list(crlf, read));
    EXPECT_EQ(read.size(), games.size());
}

// A game that cannot be written as one line, or whose path is not on the
// drive it says, is left out; the rest are kept.
static void TestGamesLeftOut() {
    Games games = Library();
    const std::size_t good = games.size();
    games.push_back(Game(true, "RMCE01", "Tab\tin its name", "usb:/wbfs/RMCE01.wbfs"));
    games.push_back(Game(true, "RMCE01", "Line\nbreak", "usb:/wbfs/RMCE01.wbfs"));
    games.push_back(Game(true, "RMCE01", "", "usb:/wbfs/RMCE01.wbfs"));
    games.push_back(Game(true, "", "No ID", "usb:/wbfs/x.wbfs"));
    games.push_back(Game(true, "RMCE012", "ID too long", "usb:/wbfs/x.wbfs"));
    games.push_back(Game(true, "RM/E01", "A slash in its ID", "usb:/wbfs/x.wbfs"));
    games.push_back(Game(true, "RM E01", "A space in its ID", "usb:/wbfs/x.wbfs"));
    games.push_back(Game(true, "RMCE01", "Says USB, is on the card", "sd:/wbfs/RMCE01.wbfs"));
    games.push_back(Game(false, "RMCE01", "Says the card, is on USB", "usb:/wbfs/RMCE01.wbfs"));
    games.push_back(Game(false, "RMCE01", "No drive at all", "/wbfs/RMCE01.wbfs"));
    games.push_back(Game(true, "RMCE01", "No such partition name", "usb0:/wbfs/RMCE01.wbfs"));
    games.push_back(Game(false, "RMCE01", "A tab in its path", "sd:/wbfs/a\tb.wbfs"));
    games.push_back(Game(false, "RMCE01", "Path too long", "sd:/" + std::string(riftwii::kHomeListMaxPath, 'a')));
    games.push_back(Game(false, "RMCE01", std::string(riftwii::kHomeListMaxName + 1, 'n'), "sd:/wbfs/RMCE01.wbfs"));
    games.push_back(Game(false, "GZLE99", "Its original is nowhere", "sd:/games/mod.xdelta"));
    games.back().patch_source = "games/original.iso";
    for (std::size_t i = good; i < games.size(); ++i) EXPECT_FALSE(riftwii::remembered_game_valid(games[i]));
    for (std::size_t i = 0; i < good; ++i) EXPECT_TRUE(riftwii::remembered_game_valid(games[i]));

    Games read;
    EXPECT_TRUE(riftwii::parse_home_list(riftwii::serialize_home_list(games), read));
    EXPECT_EQ(read.size(), good);
}

static std::string Lines(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) text += line + "\n";
    return text;
}

static const char* const kGameLine = "usb\tRMCE01\t0\tm\tMario Kart Wii\tusb:/wbfs/RMCE01.wbfs\t";

// A file that is not whole, or not as RiftWii writes it, gives no games.
static void TestRefusedFiles() {
    Games read = Library();
    EXPECT_TRUE(riftwii::parse_home_list(Lines({"homelist 1", kGameLine, "end 1"}), read));
    EXPECT_EQ(read.size(), std::size_t(1));
    EXPECT_TRUE(read.size() == 1 && read[0].usb && read[0].has_mods && !read[0].gamecube && read[0].id == "RMCE01");

    const std::vector<std::vector<std::string>> refused = {
        {},                                                     // an empty file
        {"homelist 1", kGameLine},                              // cut short: no count
        {"homelist 1", kGameLine, "end 2"},                     // a game lost
        {"homelist 1", kGameLine, kGameLine, "end 1"},          // one more than counted
        {"homelist 1", kGameLine, "end"},                       // no count on the last line
        {"homelist 1", kGameLine, "end 01"},
        {"homelist 1", kGameLine, "end 1", kGameLine},          // games after the count
        {"homelist 1", kGameLine, "end 1", "end 1"},
        {"homelist 2", kGameLine, "end 1"},                     // another version's file
        {kGameLine, "end 1"},                                   // no first line
        {"homelist 1", "usb\tRMCE01\t0\tm\tMario Kart Wii\tusb:/wbfs/RMCE01.wbfs", "end 1"},        // six fields
        {"homelist 1", std::string(kGameLine) + "\textra", "end 1"},                                 // eight
        {"homelist 1", "disc\tRMCE01\t0\t-\tMario Kart Wii\tusb:/wbfs/RMCE01.wbfs\t", "end 1"},     // no such drive
        {"homelist 1", "usb\tRMCE01\t256\t-\tMario Kart Wii\tusb:/wbfs/RMCE01.wbfs\t", "end 1"},    // no such disc
        {"homelist 1", "usb\tRMCE01\t-1\t-\tMario Kart Wii\tusb:/wbfs/RMCE01.wbfs\t", "end 1"},
        {"homelist 1", "usb\tRMCE01\t\t-\tMario Kart Wii\tusb:/wbfs/RMCE01.wbfs\t", "end 1"},
        {"homelist 1", "usb\tRMCE01\t0\tx\tMario Kart Wii\tusb:/wbfs/RMCE01.wbfs\t", "end 1"},      // an unknown mark
        {"homelist 1", "usb\tRMCE01\t0\tmm\tMario Kart Wii\tusb:/wbfs/RMCE01.wbfs\t", "end 1"},     // a mark twice
        {"homelist 1", "usb\tRMCE01\t0\t\tMario Kart Wii\tusb:/wbfs/RMCE01.wbfs\t", "end 1"},       // no marks field
        {"homelist 1", "usb\tRMCE01\t0\t-\tMario Kart Wii\tsd:/wbfs/RMCE01.wbfs\t", "end 1"},       // the wrong drive
        {"homelist 1", "usb\tRMCE01\t0\t-\t\tusb:/wbfs/RMCE01.wbfs\t", "end 1"},                    // no name
        {"homelist 1", "usb\t../../x\t0\t-\tMario Kart Wii\tusb:/wbfs/RMCE01.wbfs\t", "end 1"},     // an ID that climbs
        {"homelist 1", "some other text", "end 0"},
    };
    for (std::size_t i = 0; i < refused.size(); ++i) {
        read = Library();
        const bool parsed = riftwii::parse_home_list(Lines(refused[i]), read);
        if (parsed || !read.empty()) {
            std::cerr << "FAILED: refused file " << i << " was read" << std::endl;
            g_failures++;
        }
    }

    // Comments and blank lines are passed over, wherever they are.
    EXPECT_TRUE(riftwii::parse_home_list(Lines({"# a comment", "", "homelist 1", "# another", kGameLine, "", "end 1", ""}),
                                         read));
    EXPECT_EQ(read.size(), std::size_t(1));
}

// The limits: a file past them is not RiftWii's.
static void TestLimits() {
    Games games;
    for (std::size_t i = 0; i < riftwii::kHomeListMaxGames + 5; ++i) {
        games.push_back(Game(i % 2 == 0, "RMCE01", "Game " + std::to_string(i),
                             (i % 2 == 0 ? "usb:/wbfs/" : "sd:/wbfs/") + std::to_string(i) + ".wbfs"));
    }
    const std::string text = riftwii::serialize_home_list(games);
    Games read;
    EXPECT_TRUE(riftwii::parse_home_list(text, read));
    EXPECT_EQ(read.size(), riftwii::kHomeListMaxGames);
    EXPECT_TRUE(text.size() <= riftwii::kHomeListMaxBytes);

    // One game more than the most, counted honestly: refused.
    std::string over = "homelist 1\n";
    for (std::size_t i = 0; i <= riftwii::kHomeListMaxGames; ++i) over += std::string(kGameLine) + "\n";
    over += "end " + std::to_string(riftwii::kHomeListMaxGames + 1) + "\n";
    EXPECT_FALSE(riftwii::parse_home_list(over, read));
    EXPECT_TRUE(read.empty());

    // A file larger than the most bytes is not looked at.
    std::string huge = "homelist 1\n";
    huge += "# " + std::string(riftwii::kHomeListMaxBytes, 'x') + "\n";
    huge += "end 0\n";
    EXPECT_FALSE(riftwii::parse_home_list(huge, read));
}

// Long names and paths: the file stops short of the byte limit rather than
// writing one that is refused when read back.
static void TestByteLimit() {
    Games games;
    for (std::size_t i = 0; i < 4000; ++i) {
        games.push_back(Game(false, "RMCE01", std::string(riftwii::kHomeListMaxName, 'n') + std::to_string(i % 10),
                             "sd:/wbfs/" + std::string(900, 'p') + std::to_string(i) + ".wbfs"));
        games.back().name.resize(riftwii::kHomeListMaxName);
    }
    const std::string text = riftwii::serialize_home_list(games);
    EXPECT_TRUE(text.size() <= riftwii::kHomeListMaxBytes);
    Games read;
    EXPECT_TRUE(riftwii::parse_home_list(text, read));
    EXPECT_TRUE(!read.empty() && read.size() < games.size());
    for (std::size_t i = 0; i < read.size(); ++i) EXPECT_TRUE(Same(read[i], games[i]));
}

int main() {
    TestByteLimit();
    TestRoundTrip();
    TestGamesLeftOut();
    TestRefusedFiles();
    TestLimits();
    if (g_failures == 0) {
        std::cout << "ALL HOME LIST TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
