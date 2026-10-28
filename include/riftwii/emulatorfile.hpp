// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace riftwii {

constexpr std::size_t kEmulatorFileMax = 16 * 1024;
constexpr std::size_t kEmulatorSettingsMax = 16;
constexpr std::size_t kEmulatorValuesMax = 32;
constexpr std::size_t kEmulatorFoldersMax = 8;
constexpr std::size_t kEmulatorExtensionsMax = 16;
constexpr std::size_t kEmulatorPathMax = 512;
constexpr std::size_t kEmulatorCommandMax = 1024;

enum class EmulatorSettingType { Choice, Switch, Number };

struct EmulatorSetting {
    std::string key, label, help, default_value;
    EmulatorSettingType type = EmulatorSettingType::Choice;
    std::vector<std::string> values;
    std::vector<std::string> on;
    int minimum = 0;
    int maximum = 0;
};

struct EmulatorFile {
    std::string name, system, dol;
    std::string covers;  // optional libretro system folder
    std::string wiiflow_covers;  // a WiiFlow plugin's own covers folder, if read from one
    std::vector<std::string> folders, extensions, arguments;
    std::vector<EmulatorSetting> settings;
};

using EmulatorValues = std::map<std::string, std::string>;

// app_folder is an absolute sd:/apps/... or usb:/apps/... folder. Errors
// reject the whole file; warnings describe unknown keys that were ignored.
bool parse_emulator_file(const std::string& text, const std::string& app_folder,
                         EmulatorFile& out, std::string& error, std::vector<std::string>& warnings);
bool emulator_path_valid(const std::string& path);
bool emulator_value_valid(const EmulatorSetting& setting, const std::string& value);
bool emulator_game_matches(const EmulatorFile& emulator, const std::string& path);
std::string emulator_game_name(const std::string& path);
EmulatorValues emulator_defaults(const EmulatorFile& emulator);
// Saved overrides accept only declared settings and valid values. Invalid
// entries are ignored; they can never introduce an argument.
void read_emulator_values(const EmulatorFile& emulator, const std::string& text, EmulatorValues& values);
std::string write_emulator_values(const EmulatorFile& emulator, const EmulatorValues& values);
std::string step_emulator_value(const EmulatorSetting& setting, const std::string& value, int direction);

// Builds a NUL-separated HBC command line, including argv[0] and the final
// extra NUL. Never truncates: on failure both outputs are empty.
bool build_emulator_arguments(const EmulatorFile& emulator, const std::string& game,
                              const EmulatorValues& values, std::vector<std::string>& argv,
                              std::string& command_line, std::string& error);

}  // namespace riftwii
