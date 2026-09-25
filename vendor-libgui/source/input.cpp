/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * input.cpp
 * Wii/GameCube controller management
 ***************************************************************************/

#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ogcsys.h>
#include <unistd.h>
#include <wiiuse/wpad.h>

#include "menu.h"
#include "video.h"
#include "input.h"
#include "libwiigui/gui.h"
#include "wiidrc.h"
#include "gcadapter.hpp"

static void UpdatePadPointers();

int rumbleRequest[4] = {0,0,0,0};
GuiTrigger userInput[4];
static int rumbleCount[4] = {0,0,0,0};

/****************************************************************************
 * UpdatePads
 *
 * Scans pad and wpad
 ***************************************************************************/
void UpdatePads()
{
	WPAD_ScanPads();
	PAD_ScanPads();

	// The Wii U GamePad is a single controller behind FIX94's direct-I2C
	// driver; it reports on channel 0 when one is connected. Everywhere
	// else the fields stay zero, so plain Wii and Dolphin (whose reads
	// never match the driver's magic) behave exactly as before.
	if (WiiDRC_Inited() && WiiDRC_Connected()) {
		WiiDRC_ScanPads();
		userInput[0].wiidrcdata.btns_d = (u16)WiiDRC_ButtonsDown();
		userInput[0].wiidrcdata.btns_u = (u16)WiiDRC_ButtonsUp();
		userInput[0].wiidrcdata.btns_h = (u16)WiiDRC_ButtonsHeld();
		userInput[0].wiidrcdata.stickX = WiiDRC_lStickX();
		userInput[0].wiidrcdata.stickY = WiiDRC_lStickY();
		userInput[0].wiidrcdata.substickX = WiiDRC_rStickX();
		userInput[0].wiidrcdata.substickY = WiiDRC_rStickY();
	} else {
		userInput[0].wiidrcdata.btns_d = 0;
		userInput[0].wiidrcdata.btns_u = 0;
		userInput[0].wiidrcdata.btns_h = 0;
		userInput[0].wiidrcdata.stickX = 0;
		userInput[0].wiidrcdata.stickY = 0;
		userInput[0].wiidrcdata.substickX = 0;
		userInput[0].wiidrcdata.substickY = 0;
	}

	for(int i=3; i >= 0; i--)
	{
		userInput[i].pad.btns_d = PAD_ButtonsDown(i);
		userInput[i].pad.btns_u = PAD_ButtonsUp(i);
		userInput[i].pad.btns_h = PAD_ButtonsHeld(i);
		userInput[i].pad.stickX = PAD_StickX(i);
		userInput[i].pad.stickY = PAD_StickY(i);
		userInput[i].pad.substickX = PAD_SubStickX(i);
		userInput[i].pad.substickY = PAD_SubStickY(i);
		userInput[i].pad.triggerL = PAD_TriggerL(i);
		userInput[i].pad.triggerR = PAD_TriggerR(i);
	}

	// Riftwii: a GameCube controller adapter for Wii U works the menu like
	// the Wii's own GameCube ports, its port N on channel N. Buttons add to
	// the port's; a stick is taken while the port's own is idle.
	static u16 adapterHeld[4] = {0, 0, 0, 0};
	riftwii::wii::GcAdapterView adapter;
	riftwii::wii::GcAdapterMenuPads(adapter);
	for (int i = 0; i < 4; i++)
	{
		const u16 held = adapter.present[i] ? adapter.pads[i].buttons : 0;
		userInput[i].pad.btns_d |= held & ~adapterHeld[i];
		userInput[i].pad.btns_u |= adapterHeld[i] & ~held;
		userInput[i].pad.btns_h |= held;
		adapterHeld[i] = held;
		if (!adapter.present[i]) continue;
		const gcad_pad &a = adapter.pads[i];
		PADData &p = userInput[i].pad;
		if (abs(p.stickX) < 14 && abs(p.stickY) < 14) {
			p.stickX = a.stick_x;
			p.stickY = a.stick_y;
		}
		if (abs(p.substickX) < 14 && abs(p.substickY) < 14) {
			p.substickX = a.substick_x;
			p.substickY = a.substick_y;
		}
		if (a.trigger_l > p.triggerL) p.triggerL = a.trigger_l;
		if (a.trigger_r > p.triggerR) p.triggerR = a.trigger_r;
	}

	UpdatePadPointers();
}

/****************************************************************************
 * UpdatePadPointers (Riftwii)
 *
 * A GameCube controller's control stick, or a Classic Controller's left
 * stick (also what fakemote makes of a USB DS3 or DS4), drives an
 * on-screen pointer the way a Wii Remote does, and A clicks what is under
 * it. It is written into the channel's Wii Remote IR data, so every
 * widget treats it as pointing. A Wii Remote on the same channel takes
 * over the moment it points or presses one of its own buttons; the pad
 * takes back over on its next input. That stick then no longer steps
 * through lists (the D-pad still does).
 ***************************************************************************/
static void UpdatePadPointers()
{
	static float x[4], y[4];
	static bool placed[4] = {false, false, false, false};
	static bool active[4] = {false, false, false, false};
	const int deadzone = 14;

	for (int i = 0; i < 4; i++)
	{
		WPADData * w = userInput[i].wpad;
		if (!w) continue;
		// Only a connected remote's data is refreshed by the scan; on an
		// empty channel ir.valid is still the pointer written last frame.
		u32 type = 0;
		const bool remote = WPAD_Probe(i, &type) == WPAD_ERR_NONE;
		// The Wii Remote's own buttons are the low 16 bits; a Classic
		// Controller's are the high ones.
		if (remote && (w->ir.valid || (w->btns_d & 0xFFFF))) {
			active[i] = false;  // the Wii Remote is in use
			continue;
		}
		if (!remote) w->ir.valid = 0;
		// A Classic Controller's left stick (scaled to the GameCube
		// stick's range, about +-100) when the GameCube stick is idle.
		const bool classic = remote && w->exp.type == WPAD_EXP_CLASSIC;
		int sx = userInput[i].pad.stickX;
		int sy = userInput[i].pad.stickY;
		if (classic && abs(sx) <= deadzone && abs(sy) <= deadzone) {
			sx = userInput[i].WPAD_StickX(0) * 100 / 128;
			sy = userInput[i].WPAD_StickY(0) * 100 / 128;
		}
		const bool moved = abs(sx) > deadzone || abs(sy) > deadzone;
		if (moved || userInput[i].pad.btns_d || (classic && (w->btns_d & ~0xFFFFu)))
			active[i] = true;
		if (!active[i]) continue;
		if (!placed[i]) {
			x[i] = screenwidth / 2;
			y[i] = screenheight / 2;
			placed[i] = true;
		}
		// Quadratic response: fine control near the centre, about 14 px per
		// frame at full tilt.
		const auto speed = [&](int v) -> float {
			if (abs(v) <= deadzone) return 0.0f;
			float t = (abs(v) - deadzone) / (float)(100 - deadzone);
			if (t > 1.0f) t = 1.0f;
			const float s = 1.0f + 13.0f * t * t;
			return v < 0 ? -s : s;
		};
		x[i] += speed(sx);
		y[i] -= speed(sy);  // stick up is positive, screen y grows downward
		if (x[i] < 0) x[i] = 0;
		if (y[i] < 0) y[i] = 0;
		if (x[i] > screenwidth - 1) x[i] = screenwidth - 1;
		if (y[i] > screenheight - 1) y[i] = screenheight - 1;
		w->ir.valid = 1;
		w->ir.x = x[i];
		w->ir.y = y[i];
		w->ir.angle = 0;
		userInput[i].pad.stickX = 0;
		userInput[i].pad.stickY = 0;
		if (classic) {
			// Centred, so the stick no longer steps lists as well.
			w->exp.classic.ljs.pos = w->exp.classic.ljs.center;
		}
	}
}

/****************************************************************************
 * SetupPads
 *
 * Sets up userInput triggers for use
 ***************************************************************************/
void SetupPads()
{
	PAD_Init();
	WPAD_Init();
	// Best effort: false on plain Wii and under Dolphin, where every
	// GamePad read below stays zeroed by the Connected() gate.
	WiiDRC_Init();

	// read wiimote accelerometer and IR data
	WPAD_SetDataFormat(WPAD_CHAN_ALL,WPAD_FMT_BTNS_ACC_IR);
	WPAD_SetVRes(WPAD_CHAN_ALL, screenwidth, screenheight);

	for(int i=0; i < 4; i++)
	{
		userInput[i].chan = i;
		userInput[i].wpad = WPAD_Data(i);
	}
}

/****************************************************************************
 * ShutoffRumble
 ***************************************************************************/

void ShutoffRumble()
{
	for(int i=0;i<4;i++)
	{
		WPAD_Rumble(i, 0);
		rumbleCount[i] = 0;
	}
}

/****************************************************************************
 * DoRumble
 ***************************************************************************/

void DoRumble(int i)
{
	if(rumbleRequest[i] && rumbleCount[i] < 3)
	{
		WPAD_Rumble(i, 1); // rumble on
		rumbleCount[i]++;
	}
	else if(rumbleRequest[i])
	{
		rumbleCount[i] = 12;
		rumbleRequest[i] = 0;
	}
	else
	{
		if(rumbleCount[i])
			rumbleCount[i]--;
		WPAD_Rumble(i, 0); // rumble off
	}
}
