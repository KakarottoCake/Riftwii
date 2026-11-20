// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "riftwii/launchargs.hpp"

// A launch another loader asked for (docs/HEADLESS.md): RiftWii's menu
// never opens. The game is found by its ID, the packs the other loader
// picked are turned on with the options saved for them (RiftWii's own
// choices, else Riivolution's config), its settings go on top, and the
// game boots as it would from the game page.
namespace riftwii::wii {

// Where a headless launch's arguments came from. A continuation is a launch
// file RiftWii's own runtime writes as a game carries on: a GameCube program
// switch (sd:/riftwii/gc/exec.txt) or a Wii game's own Reset
// (sd:/riftwii/restart.txt). Anything else is a request: another loader's
// arguments or launch file, or sd:/riftwii/launch.txt. Parental controls
// let only a continuation carry on without asking again.
enum class HeadlessSource { Request, Continuation };

// The arguments RiftWii was started with, when they ask for a headless
// launch, and where they came from; false otherwise (the menu opens as usual).
bool HeadlessArguments(std::vector<std::string>& args, HeadlessSource& source);

// Runs the launch on the console screen. Returns only on failure, after
// logging it to sd:/riftwii/boot.log and showing it.
void RunHeadless(const std::vector<std::string>& args, HeadlessSource source);

}  // namespace riftwii::wii
