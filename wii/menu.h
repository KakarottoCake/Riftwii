// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

void InitGUIThreads();
// Returns MENU_BOOT or MENU_DUMP with the GUI torn down; exits on MENU_EXIT.
int MainMenu(int menuitem);

enum
{
	MENU_EXIT = -1,
	MENU_NONE,
	MENU_HOME,
	MENU_BOOT,   // leave the GUI and boot the inserted disc unmodified
	MENU_DUMP    // leave the GUI and dump test files from the disc
};
