// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "modplan.hpp"
#include "riftwii/launch.hpp"
#include "usbcatalog.hpp"

// How a game is started: probe the disc or image, compile the packs, boot.
// The menu, the headless entry and (in test builds) autorun all use these.
namespace riftwii::wii {

using MissingModConfirmation = std::function<bool(const std::vector<MissingModInput>&)>;

bool RunBoot(bool allow_ios_fallback, std::string& error, const LaunchSource& source = LaunchSource());
bool RunDump(const std::vector<std::string>& disc_paths, const std::string& sd_dir, std::string& error);
bool ProbeInserted(std::string& game_id, std::string& title, std::string& error,
                   std::uint8_t* revision = nullptr, std::uint8_t* disc_number = nullptr);
bool CompileSelection(const std::vector<PackageChoices>& packages, CompiledMod& out, std::string& error,
                      const LaunchSource& source = LaunchSource());
bool BootCompiled(CompiledMod&& mod, std::string& error, const LaunchSource& source = LaunchSource(),
                    const std::string& save_mode = "nand", const std::string& game_id = "");
bool RunLaunch(const std::vector<PackageChoices>& packages, std::string& error,
               const LaunchSource& source = LaunchSource(), const std::string& save_mode = "nand",
               const std::string& game_id = "", const MissingModConfirmation& confirm_missing = {});

}  // namespace riftwii::wii
