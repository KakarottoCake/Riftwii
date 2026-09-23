// SPDX-License-Identifier: GPL-3.0-or-later
// Game display names: a GameTDB-style titles.txt, "Title [ID]" folder
// names, and the internal disc name as the last resort.
#include "riftwii/titles.hpp"

#include <iostream>
#include <string>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

static void test_table() {
    const std::string text =
        "TITLES = https://www.gametdb.com (type: Wii language: EN version: 20260101)\r\n"
        "SB4E01 = Super Mario Galaxy 2\r\n"
        "RMCE01 =   Mario Kart Wii  \n"
        "WAAA = Some WiiWare Game\n"
        "garbage line\n"
        "bad!01 = Not an ID\n"
        "RSBE01 = \n"
        "R8PE01 = Super Paper Mario";  // no trailing newline
    riftwii::TitleTable all;
    all.add_text(text);
    EXPECT_EQ(all.size(), std::size_t(4));
    EXPECT_TRUE(all.find("SB4E01") && *all.find("SB4E01") == "Super Mario Galaxy 2");
    EXPECT_TRUE(all.find("RMCE01") && *all.find("RMCE01") == "Mario Kart Wii");
    EXPECT_TRUE(all.find("R8PE01") && *all.find("R8PE01") == "Super Paper Mario");
    EXPECT_TRUE(all.find("WAAAE0") && *all.find("WAAAE0") == "Some WiiWare Game");  // 4-char fallback
    EXPECT_TRUE(all.find("RSBE01") == nullptr);
    EXPECT_TRUE(all.find("RZZE01") == nullptr);

    const std::set<std::string> wanted = {"SB4E01", "SB4E"};
    riftwii::TitleTable some;
    some.add_text(text, &wanted);
    EXPECT_EQ(some.size(), std::size_t(1));
    EXPECT_TRUE(some.find("SB4E01") != nullptr);
}

static void test_folder_and_choice() {
    EXPECT_EQ(riftwii::folder_title("usb:/wbfs/Super Mario Galaxy 2 [SB4E01]/SB4E01.wbfs", "SB4E01"),
              std::string("Super Mario Galaxy 2"));
    EXPECT_EQ(riftwii::folder_title("usb:/wbfs/SB4E01/SB4E01.wbfs", "SB4E01"), std::string(""));
    EXPECT_EQ(riftwii::folder_title("usb:/wbfs/Other [RMCE01]/SB4E01.wbfs", "SB4E01"), std::string(""));
    EXPECT_EQ(riftwii::folder_title("usb:/games/SB4E01.iso", "SB4E01"), std::string(""));
    EXPECT_EQ(riftwii::folder_title("SB4E01.iso", "SB4E01"), std::string(""));
    EXPECT_EQ(riftwii::folder_title("/[SB4E01]/x.wbfs", "SB4E01"), std::string(""));

    riftwii::TitleTable table;
    table.add_text("SB4E01 = Super Mario Galaxy 2\n");
    const std::string path = "usb:/wbfs/SUPER MARIO GALAXY MORE [SB4E01]/SB4E01.wbfs";
    EXPECT_EQ(riftwii::display_title(&table, "SB4E01", path, "SUPER MARIO GALAXY MORE"),
              std::string("Super Mario Galaxy 2"));
    EXPECT_EQ(riftwii::display_title(nullptr, "SB4E01", "usb:/wbfs/Galaxy Two [SB4E01]/SB4E01.wbfs", "X"),
              std::string("Galaxy Two"));
    EXPECT_EQ(riftwii::display_title(nullptr, "SB4E01", "usb:/games/SB4E01.iso", "SUPER MARIO GALAXY MORE"),
              std::string("SUPER MARIO GALAXY MORE"));
    EXPECT_EQ(riftwii::display_title(nullptr, "SB4E01", "usb:/games/SB4E01.iso", ""), std::string("SB4E01"));
}

static void test_id_from_path() {
    using riftwii::id_from_image_path;
    EXPECT_EQ(id_from_image_path("usb:/wbfs/ANIMAL CROSSING [RUUE01]/RUUE01.wbfs"), std::string("RUUE01"));
    EXPECT_EQ(id_from_image_path("usb:/wbfs/RUUE01.wbfs"), std::string("RUUE01"));
    EXPECT_EQ(id_from_image_path("sd:/wbfs/SMNE01_New Super Mario Bros. Wii/SMNE01.wbfs"), std::string("SMNE01"));
    EXPECT_EQ(id_from_image_path("usb:/wbfs/Some Game [SB4E01]/game.wbfs"), std::string("SB4E01"));
    EXPECT_EQ(id_from_image_path("usb:/games/Mario Kart Wii [RMCE01].iso"), std::string("RMCE01"));
    EXPECT_EQ(id_from_image_path("usb:/games/Mario Kart - Double Dash!! (USA).iso"), std::string(""));
    EXPECT_EQ(id_from_image_path("usb:/games/game.iso"), std::string(""));
    EXPECT_EQ(id_from_image_path("usb:/wbfs/x [rmce01]/game.wbfs"), std::string(""));  // IDs are upper case
    EXPECT_EQ(id_from_image_path("usb:/wbfs/[RMCE0]/game.wbfs"), std::string(""));
    EXPECT_EQ(id_from_image_path("RMCE01"), std::string("RMCE01"));
}

int main() {
    test_table();
    test_id_from_path();
    test_folder_and_choice();
    if (g_failures == 0) {
        std::cout << "ALL TITLES TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
