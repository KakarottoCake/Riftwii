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
// Whether this choice is the CTGP-R pack (CTGP-R's own channel app standing in
// for the game): such a launch, the channel's or the native one, goes through
// RunLaunch, not the packs compiled in the menu.
bool CtgprNativeLaunch(const std::vector<PackageChoices>& packages, const std::string& game_id);
#ifdef RIFTWII_TEST_BUILD
// Test builds only (autorun's "ctgpnative"; a release build uses a file
// ctgpnative.txt next to its boot.dol): the CTGP-R pack uses the native
// launch, when its files are on the card, instead of CTGP-R's own channel app
// (the default, for a disc, a USB image and an SD image alike).
void ForceCtgprNativeLaunch(bool on);
#endif
bool RunLaunch(const std::vector<PackageChoices>& packages, std::string& error,
               const LaunchSource& source = LaunchSource(), const std::string& save_mode = "nand",
               const std::string& game_id = "", const MissingModConfirmation& confirm_missing = {});

}  // namespace riftwii::wii
