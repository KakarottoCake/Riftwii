// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend.hpp"

using riftwii::wii::FrontendState;

void InitGUIThreads();

// Returns MENU_LAUNCH, MENU_BOOT or MENU_DUMP with the GUI torn down; exits
// on MENU_EXIT. With MENU_LAUNCH the model holds the selection.
int MainMenu(int menuitem, FrontendState& state);

enum
{
	MENU_EXIT = -1,
	MENU_NONE,
	MENU_HOME,     // the picked game: its mod packs, saves and Start
	MENU_OPTIONS,  // settings: menu IOS, rescan, exit
	MENU_LAUNCH,   // leave the GUI and boot with the selection
	MENU_BOOT,     // leave the GUI and boot the game unmodified
	MENU_DUMP,     // leave the GUI and dump test files from the disc
	MENU_SOURCE    // home: every game as tiles
};
