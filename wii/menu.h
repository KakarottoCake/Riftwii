// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

void InitGUIThreads();
void MainMenu(int menuitem);

enum
{
	MENU_EXIT = -1,
	MENU_NONE,
	MENU_HOME
};
