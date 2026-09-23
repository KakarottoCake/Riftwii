/****************************************************************************
 * libwiigui
 *
 * Tantric 2009
 *
 * gui_trigger.cpp
 *
 * GUI class definitions
 ***************************************************************************/

#include "gui.h"
#include <ogc/lwp_watchdog.h>
#include <gctypes.h>
#include <algorithm>
#include "video.h"


/**
 * Constructor for the GuiTrigger class.
 */
GuiTrigger::GuiTrigger()
{
	chan = -1;
	WiimoteTurned = false;
	memset(&wiidrcdata, 0, sizeof(GamePadData));
	memset(&wpaddata, 0, sizeof(WPADData));
	memset(&pad, 0, sizeof(PADData));
	wpad = &wpaddata;
}

/**
 * Destructor for the GuiTrigger class.
 */
GuiTrigger::~GuiTrigger()
{
}

/**
 * Sets a simple trigger. Requires:
 * - Element is selected
 * - Trigger button is pressed
 */
void GuiTrigger::SetSimpleTrigger(s32 ch, u32 wiibtns, u16 gcbtns, u16 wiidrcbtns)
{
	type = TRIGGER::SIMPLE;
	chan = ch;
	wiidrcdata.btns_d = wiidrcbtns;
	wpaddata.btns_d = wiibtns;
	pad.btns_d = gcbtns;
}

/**
 * Sets a held trigger. Requires:
 * - Element is selected
 * - Trigger button is pressed and held
 */
void GuiTrigger::SetHeldTrigger(s32 ch, u32 wiibtns, u16 gcbtns, u16 wiidrcbtns)
{
	type = TRIGGER::HELD;
	chan = ch;
	wiidrcdata.btns_h = wiidrcbtns;
	wpaddata.btns_h = wiibtns;
	pad.btns_h = gcbtns;
}

/**
 * Sets a button trigger. Requires:
 * - Trigger button is pressed
 */
void GuiTrigger::SetButtonOnlyTrigger(s32 ch, u32 wiibtns, u16 gcbtns, u16 wiidrcbtns)
{
	type = TRIGGER::BUTTON_ONLY;
	chan = ch;
	wiidrcdata.btns_d = wiidrcbtns;
	wpaddata.btns_d = wiibtns;
	pad.btns_d = gcbtns;
}

/**
 * Sets a button trigger. Requires:
 * - Trigger button is pressed
 * - Parent window is in focus
 */
void GuiTrigger::SetButtonOnlyInFocusTrigger(s32 ch, u32 wiibtns, u16 gcbtns, u16 wiidrcbtns)
{
	type = TRIGGER::BUTTON_ONLY_IN_FOCUS;
	chan = ch;
	wiidrcdata.btns_d = wiidrcbtns;
	wpaddata.btns_d = wiibtns;
	pad.btns_d = gcbtns;
}

/****************************************************************************
 * WPAD_Stick
 *
 * Get X/Y value from Wii Joystick (classic, nunchuk) input
 ***************************************************************************/

s8 GuiTrigger::WPAD_Stick(u8 stick, int axis)
{
	#ifdef HW_RVL
	struct joystick_t* js = nullptr;

	switch (wpad->exp.type) {
		case WPAD_EXP_NUNCHUK:
		//case WPAD_EXP_GUITARHERO3: // untested
			js = stick ? nullptr : &wpad->exp.nunchuk.js;
			break;

		case WPAD_EXP_CLASSIC:
			js = stick ? &wpad->exp.classic.rjs : &wpad->exp.classic.ljs;
			break;

		default:
			break;
	}

	if (js) {
		int pos;
		int min;
		int max;
		int center;

		if(axis == 1) {
			pos = js->pos.y;
			min = js->min.y;
			max = js->max.y;
			center = js->center.y;
		}
		else {
			pos = js->pos.x;
			min = js->min.x;
			max = js->max.x;
			center = js->center.x;
		}

		if(min == max) {
			return 0;
		}

		// some 3rd party controllers return invalid analog sticks calibration data
		if ((min >= center) || (max <= center)) {
			// force default calibration settings
			min = 0;
			max = stick ? 32 : 64;
			center = stick ? 16 : 32;
		}

		if (pos > max) return 127;
		if (pos < min) return -128;

		pos -= center;

		if (pos > 0) {
			return (s8)(127.0 * ((float)pos / (float)(max - center)));
		}
		else {
			return (s8)(128.0 * ((float)pos / (float)(center - min)));
		}
	}
	#endif
	return 0;
}

s8 GuiTrigger::WPAD_StickX(u8 stick)
{
	return WPAD_Stick(stick, 0);
}

s8 GuiTrigger::WPAD_StickY(u8 stick)
{
	return WPAD_Stick(stick, 1);
}

void GuiTrigger::TurnWiimote(bool sideways)
{
	WiimoteTurned = sideways;
}

/****************************************************************************
 * Directions (Riftwii)
 *
 * A D-pad press steps once at once. Holding the D-pad, or pushing a stick
 * past its threshold, steps once, waits kRepeatFirst, then steps every
 * kRepeatNext at a steady pace (the template's repeat sped up to a step
 * every 30 ms and, for sticks, never slowed back down, so a nudge raced
 * through a list). Several widgets may ask in one frame: the answer is
 * worked out once per frame and channel, so they all agree.
 ***************************************************************************/
namespace {

constexpr u64 kRepeatFirst = 400000;  // microseconds
constexpr u64 kRepeatNext = 140000;
constexpr int kStickRelease = PADCAL * 7 / 10;  // hysteresis: a resting stick near the line does not flicker

enum { kLeft, kRight, kUp, kDown };

struct DirState {
	bool held = false;
	bool stick = false;   // held by a stick (for the hysteresis)
	u64 next = 0;         // when the next repeat is due
	u32 frame = ~0u;      // the frame `answer` belongs to
	bool answer = false;
};
DirState dirs[5][4];      // channel -1 (any) is kept in slot 4

}  // namespace

bool GuiTrigger::Direction(int dir, bool pressed, bool buttonHeld, int stick, int drcStick)
{
	DirState& d = dirs[chan >= 0 && chan < 4 ? chan : 4][dir];
	if (d.frame == FrameTimer)
		return d.answer;
	const int line = d.stick ? kStickRelease : PADCAL;
	const int drcLine = d.stick ? WIIDRCCAL * 7 / 10 : WIIDRCCAL;
	const bool byStick = stick > line || drcStick > drcLine;
	const u64 t = gettime();
	bool step = false;
	if (pressed) {
		step = true;
		d.next = t + microsecs_to_ticks(kRepeatFirst);
	} else if (buttonHeld || byStick) {
		if (!d.held) {
			step = true;
			d.next = t + microsecs_to_ticks(kRepeatFirst);
		} else if (t >= d.next) {
			step = true;
			d.next = t + microsecs_to_ticks(kRepeatNext);
		}
	}
	d.held = pressed || buttonHeld || byStick;
	d.stick = byStick && !buttonHeld && !pressed;
	d.frame = FrameTimer;
	d.answer = step;
	return step;
}

bool GuiTrigger::Left()
{
	const u32 wiibtn = WiimoteTurned ? WPAD_BUTTON_UP : WPAD_BUTTON_LEFT;
	const u32 wii = wiibtn | WPAD_CLASSIC_BUTTON_LEFT;
	return Direction(kLeft,
		(wpad->btns_d & wii) || (wiidrcdata.btns_d & WIIDRC_BUTTON_LEFT) || (pad.btns_d & PAD_BUTTON_LEFT),
		(wpad->btns_h & wii) || (wiidrcdata.btns_h & WIIDRC_BUTTON_LEFT) || (pad.btns_h & PAD_BUTTON_LEFT),
		std::max(-pad.stickX, -(int)WPAD_StickX(0)), -wiidrcdata.stickX);
}

bool GuiTrigger::Right()
{
	const u32 wiibtn = WiimoteTurned ? WPAD_BUTTON_DOWN : WPAD_BUTTON_RIGHT;
	const u32 wii = wiibtn | WPAD_CLASSIC_BUTTON_RIGHT;
	return Direction(kRight,
		(wpad->btns_d & wii) || (wiidrcdata.btns_d & WIIDRC_BUTTON_RIGHT) || (pad.btns_d & PAD_BUTTON_RIGHT),
		(wpad->btns_h & wii) || (wiidrcdata.btns_h & WIIDRC_BUTTON_RIGHT) || (pad.btns_h & PAD_BUTTON_RIGHT),
		std::max((int)pad.stickX, (int)WPAD_StickX(0)), wiidrcdata.stickX);
}

bool GuiTrigger::Up()
{
	const u32 wiibtn = WiimoteTurned ? WPAD_BUTTON_RIGHT : WPAD_BUTTON_UP;
	const u32 wii = wiibtn | WPAD_CLASSIC_BUTTON_UP;
	return Direction(kUp,
		(wpad->btns_d & wii) || (wiidrcdata.btns_d & WIIDRC_BUTTON_UP) || (pad.btns_d & PAD_BUTTON_UP),
		(wpad->btns_h & wii) || (wiidrcdata.btns_h & WIIDRC_BUTTON_UP) || (pad.btns_h & PAD_BUTTON_UP),
		std::max((int)pad.stickY, (int)WPAD_StickY(0)), wiidrcdata.stickY);
}

bool GuiTrigger::Down()
{
	const u32 wiibtn = WiimoteTurned ? WPAD_BUTTON_LEFT : WPAD_BUTTON_DOWN;
	const u32 wii = wiibtn | WPAD_CLASSIC_BUTTON_DOWN;
	return Direction(kDown,
		(wpad->btns_d & wii) || (wiidrcdata.btns_d & WIIDRC_BUTTON_DOWN) || (pad.btns_d & PAD_BUTTON_DOWN),
		(wpad->btns_h & wii) || (wiidrcdata.btns_h & WIIDRC_BUTTON_DOWN) || (pad.btns_h & PAD_BUTTON_DOWN),
		std::max(-pad.stickY, -(int)WPAD_StickY(0)), -wiidrcdata.stickY);
}
