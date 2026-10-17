// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The Saves choice "SD, emulated NAND" (riftwii/sharednand.hpp): the
// folder read from the other loader's settings lines as it writes them,
// and only a plain folder of the SD card accepted.
#include "riftwii/sharednand.hpp"

#include <iostream>
#include <string>

#include "riftwii/launch.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

void global_lines() {
    std::string path;
    EXPECT_TRUE(shared_nand_global_line("NandEmuPath = sd:/nands/01/\n", path));
    EXPECT_EQ(path, std::string("sd:/nands/01/"));
    EXPECT_TRUE(shared_nand_global_line("  NandEmuPath=sd:/my nand/ \r\n", path));
    EXPECT_EQ(path, std::string("sd:/my nand/"));
    // The channel folder and other settings are not the games' folder.
    EXPECT_FALSE(shared_nand_global_line("NandEmuChanPath = sd:/nands/02/\n", path));
    EXPECT_FALSE(shared_nand_global_line("NandEmuMode = 1\n", path));
    EXPECT_FALSE(shared_nand_global_line("# NandEmuPath = sd:/old/\n", path));
    EXPECT_FALSE(shared_nand_global_line("USB Loader GX R1281\n", path));
    EXPECT_FALSE(shared_nand_global_line("", path));
    // Tabs, and a UTF-8 byte order mark at the start of the file.
    EXPECT_TRUE(shared_nand_global_line("\tNandEmuPath\t=\tsd:/tabbed/\t\n", path));
    EXPECT_EQ(path, std::string("sd:/tabbed/"));
    EXPECT_TRUE(shared_nand_global_line("\xEF\xBB\xBFNandEmuPath = sd:/bom/\n", path));
    EXPECT_EQ(path, std::string("sd:/bom/"));
}

void game_lines() {
    const std::string line =
        "game:RMCE01 = name:Mario Kart Wii; video:0; NandEmuMode:1; NandEmuPath:sd:/nands/mk/; Hooktype:0; \n";
    std::string path;
    EXPECT_TRUE(shared_nand_game_line(line, "RMCE01", path));
    EXPECT_EQ(path, std::string("sd:/nands/mk/"));
    // Another game's line, and a longer ID starting with this one.
    EXPECT_FALSE(shared_nand_game_line(line, "RMCP01", path));
    EXPECT_FALSE(shared_nand_game_line("game:RMCE01X = NandEmuPath:sd:/x/; \n", "RMCE01", path));
    // An empty NandEmuPath means the global folder.
    EXPECT_FALSE(shared_nand_game_line("game:RMCE01 = NandEmuMode:-1; NandEmuPath:; Hooktype:0; \n", "RMCE01", path));
    EXPECT_FALSE(shared_nand_game_line("game:RMCE01 = video:0; \n", "RMCE01", path));
    EXPECT_FALSE(shared_nand_game_line("game:RMCE01 = name:broken\n", "RMCE01", path));
    EXPECT_FALSE(shared_nand_game_line(line, "", path));
    // The last field without its ";".
    EXPECT_TRUE(shared_nand_game_line("game:SMNE01 = NandEmuPath:sd:/n\r\n", "SMNE01", path));
    EXPECT_EQ(path, std::string("sd:/n"));
    EXPECT_TRUE(shared_nand_game_line("\xEF\xBB\xBFgame:SMNE01\t= NandEmuPath:\tsd:/t/;\n", "SMNE01", path));
    EXPECT_EQ(path, std::string("sd:/t/"));
}

void folders() {
    std::string folder, why;
    EXPECT_TRUE(shared_nand_folder("sd:/nands/01/", folder, why));
    EXPECT_EQ(folder, std::string("sd:/nands/01"));
    EXPECT_TRUE(shared_nand_folder(kDefaultSharedNand, folder, why));
    EXPECT_EQ(folder, std::string("sd:/nands/01"));
    EXPECT_TRUE(shared_nand_folder("sd:/my nand", folder, why));
    EXPECT_EQ(folder, std::string("sd:/my nand"));

    folder = "unchanged";
    // On the USB drive: RiftWii keeps saves only on the SD card.
    EXPECT_FALSE(shared_nand_folder("usb1:/nands/01/", folder, why));
    EXPECT_TRUE(why.find("USB") != std::string::npos);
    EXPECT_EQ(folder, std::string("unchanged"));
    // Not a plain folder of the card.
    EXPECT_FALSE(shared_nand_folder("", folder, why));
    EXPECT_FALSE(shared_nand_folder("sd:/", folder, why));
    EXPECT_FALSE(shared_nand_folder("sd:", folder, why));
    EXPECT_FALSE(shared_nand_folder("nands/01", folder, why));
    EXPECT_FALSE(shared_nand_folder("sd:/nands/../riftwii", folder, why));
    EXPECT_FALSE(shared_nand_folder("sd:/nands/./01", folder, why));
    EXPECT_FALSE(shared_nand_folder("sd:/nands//01", folder, why));
    EXPECT_FALSE(shared_nand_folder("sd:/nands\\01", folder, why));
    EXPECT_FALSE(shared_nand_folder("sd:/na:nds", folder, why));
    EXPECT_FALSE(shared_nand_folder(std::string("sd:/nan\x01" "d"), folder, why));
    // Longer than d2x's path buffer in the other loader.
    EXPECT_TRUE(shared_nand_folder("sd:/" + std::string(30, 'a'), folder, why));
    EXPECT_FALSE(shared_nand_folder("sd:/" + std::string(31, 'a'), folder, why));
    EXPECT_EQ(folder, "sd:/" + std::string(30, 'a'));
}

// The Saves choice in the launch model: kept with the game's choices, a
// launch through the pipeline, the folder as given and the Wii's save
// copied in; a pack's <savegame> still wins.
void save_choice() {
    LaunchModel model;
    model.save_mode = "emunand";
    LaunchModel again;
    again.restore(model.save());
    EXPECT_EQ(again.save_mode, std::string("emunand"));
    EXPECT_TRUE(needs_launch_pipeline(false, "emunand"));

    SaveOverride o = resolve_save_override("emunand", "", "RMCE01", "sd:/nands/01");
    EXPECT_EQ(o.dir, std::string("sd:/nands/01"));
    EXPECT_TRUE(o.clone);
    // An empty data folder there is no save: the other loader makes it.
    EXPECT_TRUE(o.clone_into_empty);
    EXPECT_FALSE(o.note.empty());
    EXPECT_TRUE(resolve_save_override("emunand", "sd:/riivolution/save", "RMCE01", "sd:/nands/01").dir.empty());
    EXPECT_TRUE(resolve_save_override("emunand", "", "RMCE01").dir.empty());
    EXPECT_TRUE(resolve_save_override("emunand", "", "", "sd:/nands/01").dir.empty());
    // The folder is only for this choice.
    EXPECT_EQ(resolve_save_override("separate", "", "RMCE01", "sd:/nands/01").dir,
              std::string("sd:/riftwii/saves/RMCE01/clone"));
    // RiftWii's own folders keep their rule: only a new folder is filled.
    EXPECT_FALSE(resolve_save_override("separate", "", "RMCE01").clone_into_empty);
    EXPECT_TRUE(resolve_save_override("nand", "", "RMCE01", "sd:/nands/01").dir.empty());
}

}  // namespace

int main() {
    global_lines();
    game_lines();
    folders();
    save_choice();
    if (g_failures == 0) std::cout << "sharednand tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
