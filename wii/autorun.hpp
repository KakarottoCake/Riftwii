// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "modplan.hpp"
#include "riftwii/launch.hpp"
#include "usbcatalog.hpp"

// Unattended test driver. Source commands select the image before probe,
// xml, boot, or launch: `sd <path-or-id> [cios-slot]`,
// `usb <path-or-id> [cios-slot]`, and `disc`.
namespace riftwii::wii {

constexpr const char* kAutorunPath = "sd:/riftwii/autorun.txt";
constexpr const char* kAutorunLogPath = "sd:/riftwii/autorun.log";

bool AutorunPresent();
void RunAutorun();
bool RunBoot(bool allow_ios_fallback, std::string& error, const LaunchSource& source = LaunchSource());
bool RunDump(const std::vector<std::string>& disc_paths, const std::string& sd_dir, std::string& error);
bool ProbeInserted(std::string& game_id, std::string& title, std::string& error,
                   std::uint8_t* revision = nullptr, std::uint8_t* disc_number = nullptr);
bool CompileSelection(const std::vector<PackageChoices>& packages, CompiledMod& out, std::string& error,
                      const LaunchSource& source = LaunchSource());
bool BootCompiled(const CompiledMod& mod, std::string& error, const LaunchSource& source = LaunchSource(),
                    const std::string& save_mode = "nand", const std::string& game_id = "");
bool RunLaunch(const std::vector<PackageChoices>& packages, std::string& error,
               const LaunchSource& source = LaunchSource(), const std::string& save_mode = "nand",
               const std::string& game_id = "");

}  // namespace riftwii::wii
