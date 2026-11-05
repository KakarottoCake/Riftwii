// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamefiles.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

static int failures;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Failed at line " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

namespace riftwii::wii {
bool usb_delete_ok = true;
std::vector<std::string> usb_paths;

void logf(const char*, ...) {}
void forget_sd_layout() {}
bool list_usb_folder(const std::string&, std::vector<VolumeEntry>&, std::string*) { return false; }
std::vector<std::string> usb_volume_roots() { return {"usb:/"}; }
bool read_usb_range(const std::string& path, std::uint64_t, std::uint8_t*, std::size_t, std::uint64_t&) {
    return path == "usb:/riftwii/covers/RMCE01.png";
}
bool delete_usb_image_files(const std::vector<std::string>& paths, const std::string&,
                            std::string& error, std::vector<std::string>* remaining) {
    usb_paths = paths;
    if (!usb_delete_ok) {
        error = "The USB test write failed.";
        if (remaining) remaining->insert(remaining->end(), paths.begin(), paths.end());
    }
    return usb_delete_ok;
}
}  // namespace riftwii::wii

namespace {
namespace fs = std::filesystem;
using namespace riftwii;
using namespace riftwii::wii;

void Write(const std::string& path) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream file(path);
    file << "test";
    file.close();
    CHECK(file.good());
}

ImageGame Game(const std::string& path, const std::string& id = "RMCE01") {
    ImageGame game;
    game.path = path;
    game.id = id;
    return game;
}

void TestOtherCopiesKeepSaves() {
    const ImageGame game = Game("sd:/games/Title/game.iso");
    GameDeletion plan;
    CHECK(game_deletion_files(game.path, true, {"disc2.iso"}, plan));
    ImageCatalog catalog;
    catalog.games = {game, Game("sd:/games/Title/disc2.iso")};
    CHECK(!HasOtherGameCopy(game, plan, catalog));
    catalog.games.push_back(Game("usb2:/games/other.iso", "OTHER1"));
    CHECK(!HasOtherGameCopy(game, plan, catalog));
    catalog.games.push_back(Game("usb2:/games/copy.iso"));
    CHECK(HasOtherGameCopy(game, plan, catalog));
    catalog.games.back().path = "sd:/games/copy.iso";
    CHECK(HasOtherGameCopy(game, plan, catalog));
}

void TestMissingImagesAndKeptSaves() {
    const ImageGame game = Game("sd:/wbfs/Kart/game.wbfs");
    Write(game.path);
    Write("sd:/wbfs/Kart/game.wbf2");
    Write("sd:/riftwii/saves/RMCE01/keep.bin");
    Write("sd:/riftwii/covers/RMCE01.png");
    GameDeletion plan;
    CHECK(game_deletion_files(game.path, false, {"game.wbf1", "game.wbf2"}, plan));
    std::vector<std::string> remaining;
    std::string error;
    CHECK(DeleteGameFiles(game, plan, false, remaining, error));
    CHECK(remaining.empty());
    CHECK(error.empty());
    CHECK(!fs::exists(game.path));
    CHECK(!fs::exists("sd:/wbfs/Kart"));
    CHECK(!fs::exists("sd:/riftwii/covers/RMCE01.png"));
    CHECK(fs::exists("sd:/riftwii/saves/RMCE01/keep.bin"));
    CHECK(usb_paths == std::vector<std::string>({"usb:/riftwii/covers/RMCE01.png"}));
    // A retry of the same plan succeeds even after all images are gone.
    CHECK(DeleteGameFiles(game, plan, false, remaining, error));
}

void TestPartialCleanupAndRetry() {
    const ImageGame game = Game("sd:/games/Kart/game.iso");
    Write(game.path);
    std::string deep = "sd:/riftwii/saves/RMCE01";
    for (int i = 0; i < 17; ++i) deep += "/d";
    Write(deep + "/save.bin");
    Write("sd:/riftwii/cheats/RMCE01.txt");
    GameDeletion plan;
    CHECK(game_deletion_files(game.path, false, {}, plan));
    std::vector<std::string> remaining;
    std::string error;
    CHECK(!DeleteGameFiles(game, plan, true, remaining, error));
    CHECK(!error.empty());
    CHECK(remaining.size() == 1);
    CHECK(!fs::exists(game.path));
    CHECK(!fs::exists("sd:/riftwii/cheats/RMCE01.txt"));
    CHECK(fs::exists(deep + "/save.bin"));
    // Clear the deliberately over-deep fixture, then retry the original plan.
    fs::remove_all("sd:/riftwii/saves/RMCE01");
    CHECK(DeleteGameFiles(game, plan, true, remaining, error));
    CHECK(remaining.empty());
    CHECK(error.empty());

    usb_delete_ok = false;
    CHECK(!DeleteGameFiles(game, plan, false, remaining, error));
    CHECK(remaining == std::vector<std::string>({"usb:/riftwii/covers/RMCE01.png"}));
    usb_delete_ok = true;
    CHECK(DeleteGameFiles(game, plan, false, remaining, error));
}

void TestImageErrorsStopDeletion() {
    const ImageGame game = Game("sd:/games/Blocked/game.iso");
    Write(game.path + "/keep.bin");
    Write("sd:/games/Blocked/later.iso");
    Write("sd:/riftwii/cheats/RMCE01.txt");
    GameDeletion plan;
    plan.images = {game.path, "sd:/games/Blocked/later.iso"};
    std::vector<std::string> remaining;
    std::string error;
    CHECK(!DeleteGameFiles(game, plan, false, remaining, error));
    CHECK(remaining == std::vector<std::string>({game.path}));
    CHECK(fs::exists("sd:/games/Blocked/later.iso"));
    CHECK(fs::exists("sd:/riftwii/cheats/RMCE01.txt"));
}

void TestScanCountsDirectories() {
    const ImageGame game = Game("sd:/games/Many/game.iso");
    Write(game.path);
    for (int i = 0; i < 4097; ++i)
        fs::create_directory("sd:/games/Many/d" + std::to_string(i));
    GameDeletion plan;
    std::string error;
    CHECK(!PlanGameDeletion(game, plan, error));
    CHECK(fs::exists(game.path));
}
}  // namespace

int main() {
    // CTest gives this suite its own working folder; only these fixtures go.
    fs::remove_all("sd:");
    TestOtherCopiesKeepSaves();
    TestMissingImagesAndKeptSaves();
    TestPartialCleanupAndRetry();
    TestImageErrorsStopDeletion();
    TestScanCountsDirectories();
    fs::remove_all("sd:");
    return failures ? 1 : 0;
}
