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
	MENU_HOME,
	MENU_OPTIONS,  // the highlighted package's options
	MENU_PREFLIGHT,  // compile the enabled packages and show the result
	MENU_LAUNCH,   // leave the GUI and boot with the compiled selection
	MENU_BOOT,     // leave the GUI and boot the inserted disc unmodified
	MENU_DUMP      // leave the GUI and dump test files from the disc
};
