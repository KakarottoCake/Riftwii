// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gamepage.hpp"

#include <algorithm>
#include <iostream>

static int failures;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Failed at line " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

int main() {
    using namespace riftwii;
    LoaderSettings settings;
    CHECK(!game_page_hidden(settings, "RMCE01"));
    CHECK(!game_page_show_hidden(settings));
    game_page_hide(settings, "RMCE01", true);
    game_page_rename(settings, "RMCE01", "  My  Kart\n ");
    CHECK(game_page_name(settings, "RMCE01", "Mario Kart") == "My Kart");
    CHECK(game_page_hidden(settings, "RMCE01"));
    LoaderSettings loaded;
    loaded.parse(settings.serialize());
    CHECK(game_page_hidden(loaded, "RMCE01"));
    CHECK(game_page_hidden(settings_after_reset(loaded), "RMCE01"));
    CHECK(game_page_name(settings_after_reset(loaded), "RMCE01", "Original") == "My Kart");
    CHECK(game_page_name(loaded, "RMCE01", "Original") == "My Kart");
    game_page_rename(loaded, "RMCE01", "");
    game_page_hide(loaded, "RMCE01", false);
    CHECK(game_page_name(loaded, "RMCE01", "Original") == "Original");
    CHECK(!game_page_hidden(loaded, "RMCE01"));
    game_page_rename(loaded, "../bad", "Name");
    CHECK(loaded.other.empty());

    GameDeletion plan;
    CHECK(game_deletion_files("sd:/wbfs/Kart [RMCE01]/RMCE01.wbfs", false,
        {"RMCE01.wbf2", "RMCE01.wbf1", "RMCE01.WBF9", "other.wbf1", "RMCE01.wbf0",
         "RMCE01.wbf1/../../victim", "../RMCE01.wbf3", "RMCE01.wbf01", "RMCE01.wbfX"}, plan));
    const std::vector<std::string> expected = {
        "sd:/wbfs/Kart [RMCE01]/RMCE01.wbfs",
        "sd:/wbfs/Kart [RMCE01]/RMCE01.wbf2",
        "sd:/wbfs/Kart [RMCE01]/RMCE01.wbf1",
        "sd:/wbfs/Kart [RMCE01]/RMCE01.WBF9"};
    CHECK(plan.images == expected);
    CHECK(plan.empty_folder == "sd:/wbfs/Kart [RMCE01]");
    CHECK(game_deletion_files("usb2:/games/Title/game.iso", true, {"disc2.iso", "other.iso", "../disc2.rvz"}, plan));
    CHECK(plan.images == std::vector<std::string>({"usb2:/games/Title/game.iso", "usb2:/games/Title/disc2.iso"}));
    for (const char* primary : {"sd:/games/Title/game.iso", "sd:/games/Title/game.gcm", "sd:/games/Title/game.rvz"}) {
        const std::vector<std::string> siblings = {"disc2.iso", "disc2.gcm", "disc2.rvz", "disc2.iso", "other.iso"};
        CHECK(game_deletion_files(primary, false, siblings, plan));
        CHECK(plan.images == std::vector<std::string>({primary}));
        CHECK(game_deletion_files(primary, true, siblings, plan));
        CHECK(plan.images == std::vector<std::string>({primary, "sd:/games/Title/disc2.iso",
            "sd:/games/Title/disc2.gcm", "sd:/games/Title/disc2.rvz"}));
    }
    CHECK(game_deletion_files("sd:/games/title.rvz", false, {"other.rvz"}, plan));
    CHECK(plan.images.size() == 1);
    CHECK(plan.empty_folder.empty());
    for (const char* path : {"sd:/games/../victim.iso", "sd:/games//title.iso", "sd:/games/./title.iso",
                             "sd:/games/title.iso/", "sd:/games/x:bad.iso", "nand:/title.iso", "sd:/apps/boot.dol",
                             "sd:/games/.. /title.iso", "usb999:/games/title.iso", "sd:/games/bad\t/title.iso"}) {
        CHECK(!game_deletion_files(path, false, {}, plan));
        CHECK(plan.images.empty());
    }
    const auto keep_saves = game_deletion_extras("RMCE01", false);
    const auto delete_saves = game_deletion_extras("RMCE01", true);
    CHECK(delete_saves.size() == keep_saves.size() + 3);
    for (const std::string& path : keep_saves) {
        CHECK(path.find("sd:/riftwii/") == 0);
        CHECK(path.find("RMCE01") != std::string::npos);
        CHECK(path.find("/saves/") == std::string::npos);
    }
    for (const std::string& path : delete_saves) CHECK(path.find("shared") == std::string::npos);
    CHECK(game_deletion_extras("../victim", true).empty());
    return failures ? 1 : 0;
}
