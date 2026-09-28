/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * demo.h
 ***************************************************************************/

#ifndef _DEMO_H_
#define _DEMO_H_

#include "FreeTypeGX.h"

enum {
	METHOD_AUTO,
	METHOD_SD,
	METHOD_USB,
	METHOD_DVD,
	METHOD_SMB,
	METHOD_MC_SLOTA,
	METHOD_MC_SLOTB,
	METHOD_SD_SLOTA,
	METHOD_SD_SLOTB
};

struct SSettings {
    int		AutoLoad;
    int		AutoSave;
    int		LoadMethod;
	int		SaveMethod;
	char	Folder1[256]; // Path to files
	char	Folder2[256]; // Path to files
	char	Folder3[256]; // Path to files
};
extern struct SSettings Settings;

void ExitApp();
extern volatile int ExitRequested;  // RiftWii: set from the power button's interrupt too
constexpr int kExitPowerButton = 5;  // RiftWii: fade out, then off with the red light
extern FreeTypeGX *fontSystem[];

#endif
