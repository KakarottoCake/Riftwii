// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// WiiFlow plugin files read as RiftWii emulators (src/data/wiiflowplugin.cpp):
// the keys, the arguments as WiiFlow gives them, what is left out and what
// is refused.
#include "riftwii/wiiflowplugin.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define EXPECT(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

const std::string kLoader = "sd:/apps/riftwii/boot.dol";
const std::string kFolder = "sd:/wiiflow/plugins";

// A plugin file as WiiFlow Lite's plugin packs write them.
const std::string snes =
    "[PLUGIN]\r\n"
    "displayname=Super Nintendo\r\n"
    "dolFile=snes9xgx/boot.dol\r\n"
    "coverFolder=SNES\r\n"
    "magic=534E4553\r\n"
    "romDir=/roms/snes/\r\n"
    "fileTypes=.smc|.SFC|.fig|.bin.zip|.zip\r\n"
    "arguments={device}:/{path}/|{name}|{loader}\r\n"
    "coverColor=8B8B8B\r\n";

riftwii::WiiflowPluginResult parse(const std::string& text, riftwii::WiiflowPlugin& plugin) {
    std::string why;
    return riftwii::parse_wiiflow_plugin(text, kFolder, kLoader, plugin, why);
}

std::string replaced(std::string text, const std::string& before, const std::string& after) {
    const auto at = text.find(before);
    if (at != std::string::npos) text.replace(at, before.size(), after);
    return text;
}

void test_snes_plugin() {
    riftwii::WiiflowPlugin plugin;
    EXPECT(parse(snes, plugin) == riftwii::WiiflowPluginResult::Emulator);
    const auto& file = plugin.file;
    EXPECT(file.name == "Super Nintendo");
    EXPECT((file.folders == std::vector<std::string>{"sd:/roms/snes", "usb:/roms/snes"}));
    // Lower case, no dot; ".bin.zip" can't be told from ".zip" by its last
    // extension, so only the plain ones stay.
    EXPECT((file.extensions == std::vector<std::string>{"smc", "sfc", "fig", "zip"}));
    EXPECT((plugin.dol_candidates ==
            std::vector<std::string>{"sd:/wiiflow/plugins/snes9xgx/boot.dol", "sd:/snes9xgx/boot.dol", "usb:/snes9xgx/boot.dol"}));
    EXPECT(file.wiiflow_covers == "SNES");
    EXPECT(file.dol.empty());

    // The start: argv as WiiFlow builds it for this plugin.
    riftwii::EmulatorFile ready = file;
    ready.dol = "sd:/apps/snes9xgx/boot.dol";
    std::vector<std::string> argv;
    std::string line, error;
    EXPECT(riftwii::build_emulator_arguments(ready, "sd:/roms/snes/Super Metroid (USA).sfc", {}, argv, line, error));
    EXPECT((argv == std::vector<std::string>{"sd:/apps/snes9xgx/boot.dol", "sd:/roms/snes/", "Super Metroid (USA).sfc", kLoader}));
    EXPECT(riftwii::build_emulator_arguments(ready, "usb:/roms/snes/RPG/Chrono Trigger (USA).smc", {}, argv, line, error));
    EXPECT((argv == std::vector<std::string>{"sd:/apps/snes9xgx/boot.dol", "usb:/roms/snes/RPG/", "Chrono Trigger (USA).smc", kLoader}));
}

void test_name_and_paths() {
    riftwii::WiiflowPlugin plugin;
    // No displayname: the DOL's name without its extension.
    EXPECT(parse(replaced(snes, "displayname=Super Nintendo\r\n", ""), plugin) == riftwii::WiiflowPluginResult::Emulator);
    EXPECT(plugin.file.name == "boot");
    // A full path is used as it is.
    EXPECT(parse(replaced(snes, "dolFile=snes9xgx/boot.dol", "dolFile=usb:/apps/snes9xgx/boot.dol"), plugin) ==
           riftwii::WiiflowPluginResult::Emulator);
    EXPECT((plugin.dol_candidates == std::vector<std::string>{"usb:/apps/snes9xgx/boot.dol"}));
    // {name_no_ext} is not read as {name} plus "_no_ext}".
    EXPECT(parse(replaced(snes, "arguments={device}:/{path}/|{name}|{loader}", "arguments={name_no_ext}"), plugin) ==
           riftwii::WiiflowPluginResult::Emulator);
    EXPECT((plugin.file.arguments == std::vector<std::string>{"{game_name}"}));
    // Keys in another case, other sections and comments.
    const std::string mixed = "; a comment\n[OTHER]\nmagic=0\n[plugin]\nDolFile=a/boot.dol\nMAGIC=41424344\n"
                              "romdir=games\nfiletypes=.nes\narguments={device}:/{path}/{name}\n";
    EXPECT(parse(mixed, plugin) == riftwii::WiiflowPluginResult::Emulator);
    EXPECT((plugin.file.arguments == std::vector<std::string>{"{game_device}:/{game_path}/{game_file}"}));
}

void test_left_out() {
    riftwii::WiiflowPlugin plugin;
    // WiiFlow's own lists, its music player and ScummVM.
    for (const char* magic : {"4E574949", "4e47434d", "4E414E44", "454E414E", "48425257"})
        EXPECT(parse(replaced(snes, "534E4553", magic), plugin) == riftwii::WiiflowPluginResult::NotAnEmulator);
    EXPECT(parse(replaced(snes, "dolFile=snes9xgx/boot.dol", "dolFile=music"), plugin) ==
           riftwii::WiiflowPluginResult::NotAnEmulator);
    EXPECT(parse(replaced(snes, "romDir=/roms/snes/", "romDir=scummvm/scummvm.ini"), plugin) ==
           riftwii::WiiflowPluginResult::NotAnEmulator);
}

void test_dol_candidates() {
    riftwii::WiiflowPlugin plugin;
    const auto with_dol = [&](const std::string& path) {
        return parse(replaced(snes, "dolFile=snes9xgx/boot.dol", "dolFile=" + path), plugin);
    };
    EXPECT(with_dol("snes9xgx/BOOT.DoL") == riftwii::WiiflowPluginResult::Emulator);
    EXPECT(plugin.dol_candidates.size() == 3);
    EXPECT(with_dol("usb:/apps/snes9xgx/BOOT.DOL") == riftwii::WiiflowPluginResult::Emulator);
    EXPECT(with_dol("snes9xgx/boot.elf") == riftwii::WiiflowPluginResult::Refused);
    EXPECT(with_dol("usb:/apps/snes9xgx/boot.elf") == riftwii::WiiflowPluginResult::Refused);
    EXPECT(with_dol("a") == riftwii::WiiflowPluginResult::Refused);
    EXPECT(with_dol(std::string(256, 'a') + "/boot.dol") == riftwii::WiiflowPluginResult::Refused);

    // Only the SD-root candidate fits exactly 512 bytes. The candidate
    // beside the INI and the USB-root candidate exceed the limit.
    const std::string relative = std::string(250, 'a') + "/" + std::string(250, 'b') + "/ab.dol";
    EXPECT(with_dol(relative) == riftwii::WiiflowPluginResult::Emulator);
    EXPECT((plugin.dol_candidates == std::vector<std::string>{"sd:/" + relative}));
    EXPECT(plugin.dol_candidates.front().size() == riftwii::kEmulatorPathMax);
    EXPECT(with_dol(relative + "x.dol") == riftwii::WiiflowPluginResult::Refused);
}

void test_configuration_identity() {
    riftwii::WiiflowPlugin plugin;
    EXPECT(parse(snes, plugin) == riftwii::WiiflowPluginResult::Emulator);
    plugin.file.dol = "sd:/apps/snes9xgx/boot.dol";
    const auto original = plugin.file;
    const auto identity = riftwii::wiiflow_plugin_identity(original);
    auto changed = original;
    changed.name = "Another label";
    changed.system = "Another badge";
    changed.wiiflow_covers = "Another cover folder";
    EXPECT(riftwii::wiiflow_plugin_identity(changed) == identity);
    changed.dol = "sd:/APPS/SNES9XGX/BOOT.DOL";
    std::reverse(changed.folders.begin(), changed.folders.end());
    std::reverse(changed.extensions.begin(), changed.extensions.end());
    EXPECT(riftwii::wiiflow_plugin_identity(changed) == identity);

    changed = original;
    changed.dol = "usb:/apps/snes9xgx/boot.dol";
    EXPECT(riftwii::wiiflow_plugin_identity(changed) != identity);
    changed = original;
    changed.folders = {"sd:/other-games"};
    EXPECT(riftwii::wiiflow_plugin_identity(changed) != identity);
    changed = original;
    changed.extensions.push_back("rom");
    EXPECT(riftwii::wiiflow_plugin_identity(changed) != identity);
    changed = original;
    changed.arguments.push_back("--alternate-core");
    EXPECT(riftwii::wiiflow_plugin_identity(changed) != identity);
    changed = original;
    std::reverse(changed.arguments.begin(), changed.arguments.end());
    EXPECT(riftwii::wiiflow_plugin_identity(changed) != identity);
    changed.arguments = {"ab", "c"};
    const auto split = riftwii::wiiflow_plugin_identity(changed);
    changed.arguments = {"a", "bc"};
    EXPECT(riftwii::wiiflow_plugin_identity(changed) != split);
}

void test_refused() {
    riftwii::WiiflowPlugin plugin;
    const auto refused = [&](const std::string& text) { return parse(text, plugin) == riftwii::WiiflowPluginResult::Refused; };
    EXPECT(refused(""));
    EXPECT(refused(replaced(snes, "magic=534E4553\r\n", "")));
    EXPECT(refused(replaced(snes, "romDir=/roms/snes/", "romDir=../roms")));
    EXPECT(refused(replaced(snes, "romDir=/roms/snes/", "romDir=roms/../../x")));
    EXPECT(refused(replaced(snes, "dolFile=snes9xgx/boot.dol", "dolFile=../boot.dol")));
    EXPECT(refused(replaced(snes, "dolFile=snes9xgx/boot.dol", "dolFile=nand:/boot.dol")));
    EXPECT(refused(replaced(snes, "fileTypes=.smc|.SFC|.fig|.bin.zip|.zip", "fileTypes=.bin.zip|.s m c")));
    EXPECT(refused(replaced(snes, "{loader}", "{unknown}")));
    EXPECT(refused(replaced(snes, "{loader}", "{name")));
    EXPECT(refused(std::string(16385, 'x')));
    EXPECT(refused("[PLUGIN]\nmagic=1\nromDir=a\ndolFile=a.dol\nfileTypes=.a\narguments=" + std::string(3000, 'a') + "\n"));
    std::string many = "[PLUGIN]\nmagic=1\nromDir=a\ndolFile=a.dol\nfileTypes=.a\narguments=";
    for (int i = 0; i < 40; ++i) many += "x|";
    EXPECT(refused(many + "\n"));
    std::string keys = "[PLUGIN]\n";
    for (int i = 0; i < 70; ++i) keys += "k" + std::to_string(i) + "=v\n";
    EXPECT(refused(keys));
    // The caller's own paths must be on a drive too.
    std::string why;
    EXPECT(riftwii::parse_wiiflow_plugin(snes, "wiiflow/plugins", kLoader, plugin, why) == riftwii::WiiflowPluginResult::Refused);
}

}  // namespace

int main() {
    test_snes_plugin();
    test_name_and_paths();
    test_dol_candidates();
    test_configuration_identity();
    test_left_out();
    test_refused();
    if (failures != 0) return 1;
    std::cout << "ALL WIIFLOWPLUGIN TESTS PASSED" << std::endl;
    return 0;
}
