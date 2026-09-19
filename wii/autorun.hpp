// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "modplan.hpp"
#include "riftwii/launch.hpp"

// Unattended test driver: when sd:/riftwii/autorun.txt exists the loader
// skips the GUI, runs the commands in it on the text console and logs to
// sd:/riftwii/autorun.log. Commands, one per line ('#' comments):
//   probe            identify the disc, partition and IOS (always run first)
//   layout           read the data header, FST and apploader header
//   meta [dir]       dump the raw structures (default sd:/riftwii/dump)
//   dump <disc path> [sd path]   copy a disc file (default sd:/riftwii/dump/<name>)
//   nofallback       fail instead of launching under the wrong IOS
//   boot             reload IOS, run the apploader and start the game
//   launch           what the GUI's Launch does: the packages on the card
//                    with the saved choices (sd:/riftwii/choices)
namespace riftwii::wii {

constexpr const char* kAutorunPath = "sd:/riftwii/autorun.txt";
constexpr const char* kAutorunLogPath = "sd:/riftwii/autorun.log";

bool AutorunPresent();

// Runs the script. Returns only when it did not boot a game.
void RunAutorun();

// The GUI's actions share the same steps.
bool RunBoot(bool allow_ios_fallback, std::string& error);
bool RunDump(const std::vector<std::string>& disc_paths, const std::string& sd_dir, std::string& error);
// Identifies the disc in the drive without waiting for one: false with
// `error` when the drive is empty or the probe fails.
bool ProbeInserted(std::string& game_id, std::string& title, std::string& error);
// Compiles the packages (with their choices) and boots with them; returns
// only on failure. The two steps are separate so the GUI can show what
// the compile made of the selection before anything irreversible.
bool CompileSelection(const std::vector<PackageChoices>& packages, CompiledMod& out, std::string& error);
bool BootCompiled(const CompiledMod& mod, std::string& error);
bool RunLaunch(const std::vector<PackageChoices>& packages, std::string& error);

}  // namespace riftwii::wii
