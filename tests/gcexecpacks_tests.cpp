// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The packs a GameCube game keeps across a switch to another program
// (riftwii/gcexecpacks.hpp): written and read back whole, and only for the
// game and image they were written for.
#include "riftwii/gcexecpacks.hpp"

#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

const std::string kGame = "GLME01";
const std::string kPath = "sd:/games/GLME01/game.iso";

std::vector<PackageChoices> sample() {
    std::vector<PackageChoices> packs(2);
    packs[0].xml_sd_path = "sd:/riivolution/mod.xml";
    packs[0].choices = {{"Mod/Music", "Remix"}, {"Mod/Speed", "2"}};
    packs[1].xml_sd_path = "usb:/riivolution/other pack.xml";
    packs[1].choices = {{"Other/Off", ""}};
    return packs;
}

void test_round_trip() {
    std::string text;
    EXPECT_TRUE(gc_exec_packs_text(kGame, kPath, sample(), text));
    std::vector<PackageChoices> back;
    EXPECT_TRUE(gc_exec_packs_parse(text, kGame, kPath, back));
    const std::vector<PackageChoices> want = sample();
    EXPECT_EQ(back.size(), want.size());
    for (std::size_t i = 0; i < back.size() && i < want.size(); ++i) {
        EXPECT_EQ(back[i].xml_sd_path, want[i].xml_sd_path);
        EXPECT_TRUE(back[i].choices == want[i].choices);
    }
    // Windows line ends read the same.
    std::string crlf;
    for (char c : text) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    EXPECT_TRUE(gc_exec_packs_parse(crlf, kGame, kPath, back));
    EXPECT_EQ(back.size(), 2u);
}

void test_other_game_or_image() {
    std::string text;
    EXPECT_TRUE(gc_exec_packs_text(kGame, kPath, sample(), text));
    std::vector<PackageChoices> back;
    EXPECT_FALSE(gc_exec_packs_parse(text, "GLMP01", kPath, back));
    EXPECT_TRUE(back.empty());
    EXPECT_FALSE(gc_exec_packs_parse(text, kGame, "usb:/games/GLME01/game.iso", back));
    EXPECT_TRUE(back.empty());
    EXPECT_FALSE(gc_exec_packs_parse("", kGame, kPath, back));
}

void test_not_whole() {
    std::vector<PackageChoices> back;
    const std::string head = "game=" + kGame + "\npath=" + kPath + "\n";
    // A choice before any pack, a choice without its tab, an unknown line.
    EXPECT_FALSE(gc_exec_packs_parse(head + "choice=A/B\tC\n", kGame, kPath, back));
    EXPECT_FALSE(gc_exec_packs_parse(head + "xml=sd:/riivolution/a.xml\nchoice=A/B\n", kGame, kPath, back));
    EXPECT_FALSE(gc_exec_packs_parse(head + "packs=all\n", kGame, kPath, back));
    EXPECT_FALSE(gc_exec_packs_parse(head + "xml=\n", kGame, kPath, back));
    // Too many packs.
    std::string many = head;
    for (std::size_t i = 0; i <= kMaxGcExecPacks; ++i) many += "xml=sd:/riivolution/p" + std::to_string(i) + ".xml\n";
    EXPECT_FALSE(gc_exec_packs_parse(many, kGame, kPath, back));
    // No packs at all is a whole file with none.
    EXPECT_TRUE(gc_exec_packs_parse(head, kGame, kPath, back));
    EXPECT_TRUE(back.empty());
    // A name with a line break or a tab can't be written.
    std::vector<PackageChoices> bad(1);
    bad[0].xml_sd_path = "sd:/riivolution/a\nb.xml";
    std::string text;
    EXPECT_FALSE(gc_exec_packs_text(kGame, kPath, bad, text));
    bad[0].xml_sd_path = "sd:/riivolution/a.xml";
    bad[0].choices = {{"A\tB", "C"}};
    EXPECT_FALSE(gc_exec_packs_text(kGame, kPath, bad, text));
}

}  // namespace

int main() {
    test_round_trip();
    test_other_game_or_image();
    test_not_whole();
    if (g_failures == 0) std::cout << "gcexecpacks: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
