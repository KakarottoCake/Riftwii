// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Home's own order (riftwii/homeorder.hpp): the file, the tiles' order
// from it, and a move written back.
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/homeorder.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using Keys = std::vector<std::string>;

// The keys in the order arrange_home_order puts them.
static Keys Arranged(const Keys& order, const Keys& keys) {
    Keys out;
    for (std::size_t i : riftwii::arrange_home_order(order, keys)) out.push_back(keys[i]);
    return out;
}

static void TestKeys() {
    EXPECT_EQ(riftwii::home_order_key(true, "RMCE01", 0), "usb:RMCE01");
    EXPECT_EQ(riftwii::home_order_key(false, "SMNE01", 0), "sd:SMNE01");
    // The second disc of a two-disc game is a game of its own.
    EXPECT_EQ(riftwii::home_order_key(true, "R8PE01", 1), "usb:R8PE01/2");
}

static void TestParse() {
    const Keys order = riftwii::parse_home_order(
        "# a comment\r\n"
        "usb:RMCE01\r\n"
        "\n"
        "sd:SMNE01  \n"
        "usb:R8PE01/2\n"
        "usb:RMCE01\n"          // named twice: the first place counts
        "nand:HAXX\n"           // no such drive
        "usb:rmce01\n"          // IDs are upper case
        "usb:RMCE0123\n"        // too long for an ID
        "usb:../../x\n"         // not a game at all
        "sd:SMNE01/1\n"         // the first disc has no number
        "usb:RSBE01");          // the last line without its newline
    EXPECT_EQ(order, (Keys{"usb:RMCE01", "sd:SMNE01", "usb:R8PE01/2", "usb:RSBE01"}));
    // What is written reads back the same.
    EXPECT_EQ(riftwii::parse_home_order(riftwii::serialize_home_order(order)), order);
    EXPECT_TRUE(riftwii::parse_home_order("").empty());
    // Bounded: a file with more lines than any drive has games stops there.
    std::string big;
    for (int i = 0; i < 3000; ++i) {
        std::string id = "R" + std::to_string(10000 + i);
        big += "usb:" + id + "\n";
    }
    EXPECT_EQ(riftwii::parse_home_order(big).size(), riftwii::kHomeOrderMaxGames);
    Keys many;
    for (int i = 0; i < 3000; ++i) many.push_back("usb:R" + std::to_string(10000 + i));
    EXPECT_EQ(riftwii::parse_home_order(riftwii::serialize_home_order(many)).size(), riftwii::kHomeOrderMaxGames);
}

static void TestArrange() {
    // The drives' games A to Z, the same game on both drives twice.
    const Keys az = {"usb:RZDE01", "sd:RMCE01", "usb:RMCE01", "usb:SMNE01", "usb:RSBE01"};
    // Listed games first in the file's order; the rest after them A to Z.
    // A game that is gone (sd:SOUE01) changes nothing.
    EXPECT_EQ(Arranged({"usb:SMNE01", "sd:SOUE01", "usb:RMCE01"}, az),
              (Keys{"usb:SMNE01", "usb:RMCE01", "usb:RZDE01", "sd:RMCE01", "usb:RSBE01"}));
    // No file: A to Z as it was.
    EXPECT_EQ(Arranged({}, az), az);
}

static void TestMove() {
    const Keys stored = {"usb:SMNE01", "sd:SOUE01", "usb:RMCE01"};
    const Keys present = {"usb:RZDE01", "usb:RMCE01", "usb:SMNE01", "usb:RSBE01"};
    // The file completed with the games it does not list, in Home's order,
    // the gone game's place kept.
    const Keys full = riftwii::merge_home_order(stored, Arranged(stored, present));
    EXPECT_EQ(full, (Keys{"usb:SMNE01", "sd:SOUE01", "usb:RMCE01", "usb:RZDE01", "usb:RSBE01"}));
    // All games shown: Brawl moved to the front.
    EXPECT_EQ(riftwii::refill_home_order(full, {"usb:RSBE01", "usb:SMNE01", "usb:RMCE01", "usb:RZDE01"}),
              (Keys{"usb:RSBE01", "sd:SOUE01", "usb:SMNE01", "usb:RMCE01", "usb:RZDE01"}));
    // A view showing some games only (the ones with mods: Mario Kart and
    // Brawl), the two swapped: the other games stay where they were.
    EXPECT_EQ(riftwii::refill_home_order(full, {"usb:RSBE01", "usb:RMCE01"}),
              (Keys{"usb:SMNE01", "sd:SOUE01", "usb:RSBE01", "usb:RZDE01", "usb:RMCE01"}));
    // Moving a tile first in A to Z: that order becomes the file's, the
    // names of games that are gone after it.
    EXPECT_EQ(riftwii::merge_home_order(present, stored),
              (Keys{"usb:RZDE01", "usb:RMCE01", "usb:SMNE01", "usb:RSBE01", "sd:SOUE01"}));
    // A game shown twice (two copies on one drive) keeps one place.
    EXPECT_EQ(riftwii::refill_home_order({"usb:A1AE01", "usb:B1BE01"}, {"usb:B1BE01", "usb:A1AE01", "usb:B1BE01"}),
              (Keys{"usb:B1BE01", "usb:A1AE01"}));
    // A game shown that the order did not name goes at its end.
    EXPECT_EQ(riftwii::refill_home_order({"usb:A1AE01"}, {"usb:C1CE01", "usb:A1AE01"}),
              (Keys{"usb:C1CE01", "usb:A1AE01"}));
}

int main() {
    TestKeys();
    TestParse();
    TestArrange();
    TestMove();
    if (g_failures == 0) std::cout << "homeorder: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
