/****************************************************************************
 * libwiigui Template
 * Tantric 2009
 *
 * input.cpp
 * Wii/GameCube controller management
 ***************************************************************************/
/* Changed for RiftWii (September and October 2026), under
 * GPL-3.0-or-later: the data format set on the four Wii Remote channels only (not the Balance Board's), Classic Controller stick as a pointer, the GameCube adapter's pads, the controller used last owns the pointer, the D-pad takes over from it, the Wii Remote's pointer kept on screen and in menu units under a display scale and steadied, steady repeat, and the Wii U GamePad scan (WiiDRC).
 * Every change is in RiftWii's git history; NOTICE.md lists the origin. */

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
 * widget treats it as pointing.
 *
 * With a Wii Remote and a pad on one channel, the one used last has it
 * and the other is ignored until it is really used: the Remote takes over
 * with one of its buttons or by moving its pointer kRemoteMove pixels (a
 * Remote lying in view of the sensor bar reports a valid, jittering
 * pointer every frame, which used to keep the pad from ever moving); the
 * pad takes over with a button or a deliberate push of kClaim. While the
 * Remote has the channel, a resting or drifting stick reads as centred,
 * so it cannot step through lists either. While the pad has it, its stick
 * moves the pointer and no longer steps lists (the D-pad still does).
 *
 * The dead zone is round, and the speed is taken from how far past it
 * the stick is, in the stick's own direction, so diagonals and full tilt
 * keep their full speed.
 *
 * The Wii Remote's pointer is steadied: the sensor's shake of a pixel or
 * two (larger on a widescreen menu, which spreads the Remote's 640 across
 * more of the menu) is filtered out while it rests, and the faster it
 * moves the less it is held back. Its tilt is steadied first, and this
 * frame's point turned by the steady tilt: the tilt's shake swung the
 * pointer more the further it was from the screen's middle. Then the
 * point hangs on a short rope, which the shake never pulls.
 *
 * The D-pad (a Wii Remote's, a Classic Controller's or a GameCube
 * controller's) takes the channel from either pointer: the pointer hides
 * and the D-pad moves the highlight, which a pointer resting over the
 * screen used to take straight back. The pointer comes back when it is
 * really used again: the stick pushed past kClaim, or the Remote's
 * pointer moved kRemoteMove pixels from where it was.
 ***************************************************************************/
static void UpdatePadPointers()
{
	static float x[4], y[4];
	static bool placed[4] = {false, false, false, false};
	static bool padHas[4] = {false, false, false, false};
	static bool anchored[4] = {false, false, false, false};
	static float anchorX[4], anchorY[4];  // where the idle Remote pointed when the pad took over
	static bool dpadHas[4] = {false, false, false, false};
	static bool dpadAnchored[4] = {false, false, false, false};
	static float dpadX[4], dpadY[4];  // where the Remote pointed when the D-pad took over
	const u32 kRemoteDpad = WPAD_BUTTON_UP | WPAD_BUTTON_DOWN | WPAD_BUTTON_LEFT | WPAD_BUTTON_RIGHT |
	                        WPAD_CLASSIC_BUTTON_UP | WPAD_CLASSIC_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_LEFT |
	                        WPAD_CLASSIC_BUTTON_RIGHT;
	const u16 kPadDpad = PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT;
	const int kDeadZone = 20;       // of about +-100: worn sticks rest past the old 14
	const int kClaim = 45;          // a push, not drift, takes the channel from the Remote
	const float kRemoteMove = 40.0f;
	static float steadyX[4], steadyY[4], steadyA[4];  // the steadied Remote pointer, in menu units
	static float steadySpeed[4];
	static float prevTilt[4], tiltRate[4];  // the Remote's tilt last frame, and how fast it turns
	static bool steadyOn[4] = {false, false, false, false};
	// libogc refreshes a Remote's data only when the Remote has sent a new
	// report, and this function writes its results into that data. On a
	// frame without a report the data still holds them: libogc's own
	// values (kept here) go back first, so nothing is converted twice and
	// the steadied tilt never comes back as the Remote's.
	static float wroteX[4], wroteY[4], wroteA[4];   // what this function left
	static float givenX[4], givenY[4], givenA[4];   // what libogc had given
	static bool wrote[4] = {false, false, false, false};
	// The tilt: held stiller than the pointer (its shake swings the
	// pointer most at the screen's sides), let go as the Remote turns.
	const float kTiltRestHz = 0.3f;
	const float kTiltGain = 0.05f;  // Hz per degree a second
	const int kIrBothDots = 1;      // libogc's IR_STATE_GOOD (wiiuse/ir.c): the sensor bar's two dots seen
	// A low-pass filter whose cut-off rises with the pointer's speed (the
	// "1 euro filter", Casiez, Roussel and Vogel, CHI 2012): at rest it is
	// cut to kRestHz, which holds the shake still; moving, it opens up by
	// kSpeedGain Hz per unit a second, so a real move is not held back.
	const float kRestHz = 1.0f;
	static float holdX[4], holdY[4];  // the pointer on its rope (below), in menu units
	const float kHold = 3.0f;
	const float kPastEdge = 0.15f;  // the pointer kept this far past libogc's box (a share of its size)
	const float kSpeedGain = 0.03f;
	const float kSpeedHz = 1.0f;  // how quickly the speed itself is followed
	const auto Follow = [](float hz) {  // the share of a 60 Hz step to take
		const float tau = 1.0f / (2.0f * 3.14159265f * hz);
		return 1.0f / (1.0f + tau * 60.0f);
	};
	// The whole screen in menu units (wider than 640 on a widescreen menu).
	f32 vx, vy, vw, vh;
	Menu_VisibleArea(&vx, &vy, &vw, &vh);
	const float minX = vx, minY = vy, maxX = vx + vw - 1, maxY = vy + vh - 1;

	for (int i = 0; i < 4; i++)
	{
		WPADData * w = userInput[i].wpad;
		if (!w) continue;
		// Only a connected remote's data is refreshed by the scan; on an
		// empty channel ir.valid is still the pointer written last frame.
		u32 type = 0;
		const bool remote = WPAD_Probe(i, &type) == WPAD_ERR_NONE;
		if (!remote) w->ir.valid = 0;
		if (wrote[i] && w->ir.x == wroteX[i] && w->ir.y == wroteY[i] && w->ir.angle == wroteA[i]) {
			w->ir.x = givenX[i];
			w->ir.y = givenY[i];
			w->ir.angle = givenA[i];
		}
		wrote[i] = false;
		// libogc calls the pointer gone the moment the Remote points past
		// its box, which is the screen's own edges: on a TV that crops them
		// it went before the corner buttons and the page arrows were
		// reached. A little past the box (kPastEdge of its size each way)
		// it stays, at the screen's edge (kept on screen below).
		if (remote && !w->ir.valid && w->ir.raw_valid && w->ir.smooth_valid) {
			const bool wide = w->ir.aspect == WIIUSE_ASPECT_16_9;
			const float boxW = wide ? 660.0f : 560.0f, boxH = wide ? 370.0f : 420.0f;
			const float left = (1024.0f - boxW) / 2 + w->ir.offset[0], top = (768.0f - boxH) / 2 + w->ir.offset[1];
			if (w->ir.ax > left - kPastEdge * boxW && w->ir.ax < left + boxW * (1 + kPastEdge) &&
			    w->ir.ay > top - kPastEdge * boxH && w->ir.ay < top + boxH * (1 + kPastEdge))
				w->ir.valid = 1;  // x and y follow from ax and ay below
		}
		// RiftWii: the menu may be drawn smaller than the screen
		// (widescreen, screen size): where the Remote points, in its units.
		if (remote && w->ir.valid) {
			givenX[i] = w->ir.x;
			givenY[i] = w->ir.y;
			givenA[i] = w->ir.angle;
			// The tilt first. libogc turns the sensor bar's two dots by
			// the tilt it reads from them (ir.angle) before it takes their
			// middle as the pointer, so the tilt's shake of a fraction of a
			// degree swings the pointer round the camera's centre: hardly
			// at the screen's middle, more and more towards its sides.
			// The tilt is steadied, and the pointer turned by the steadied
			// tilt instead of this frame's (with both dots seen, the state
			// libogc turned them in).
			float da = w->ir.angle - (steadyOn[i] ? prevTilt[i] : w->ir.angle);
			if (da > 180.0f) da -= 360.0f;
			if (da < -180.0f) da += 360.0f;
			prevTilt[i] = w->ir.angle;
			if (!steadyOn[i]) {
				steadyA[i] = w->ir.angle;
				tiltRate[i] = 0.0f;
			} else {
				// How fast the Remote really turns (degrees a second): the
				// change from frame to frame, smoothed with its sign, so the
				// shake averages out of it.
				tiltRate[i] += (da * 60.0f - tiltRate[i]) * Follow(kSpeedHz);
				float off = w->ir.angle - steadyA[i];
				if (off > 180.0f) off -= 360.0f;
				if (off < -180.0f) off += 360.0f;
				steadyA[i] += off * Follow(kTiltRestHz + kTiltGain * fabsf(tiltRate[i]));
			}
			if (w->ir.raw_valid) {
				// This frame's own point (ir.ax, ir.ay: the dots' middle in the
				// camera's pixels), not libogc's smoothed one (ir.sx, ir.sy),
				// which only moves past a dead zone and so is not turned by
				// this frame's tilt: turning it by the tilt's change put the
				// shake back in. RiftWii's own filter (below) smooths instead.
				float px = w->ir.ax - 512.0f, py = w->ir.ay - 384.0f;
				if (w->ir.state == kIrBothDots) {
					float turn = (steadyA[i] - w->ir.angle) * 3.14159265f / 180.0f;
					if (turn > 3.14159265f) turn -= 2.0f * 3.14159265f;
					if (turn < -3.14159265f) turn += 2.0f * 3.14159265f;
					const float s = sinf(turn), c = cosf(turn);
					const float tx = c * px - s * py, ty = s * px + c * py;
					px = tx;
					py = ty;
				}
				// To the screen as libogc does it (wiiuse/ir.c: the bounds
				// box of the aspect's size, its offset, then the resolution).
				const bool wide = w->ir.aspect == WIIUSE_ASPECT_16_9;
				const int boxW = wide ? 660 : 560, boxH = wide ? 370 : 420;
				w->ir.x = (px + 512.0f - w->ir.offset[0] - (1024 - boxW) / 2) / boxW * w->ir.vres[0];
				w->ir.y = (py + 384.0f - w->ir.offset[1] - (768 - boxH) / 2) / boxH * w->ir.vres[1];
			}
			w->ir.angle = steadyA[i];
			w->ir.x = Menu_ScreenToMenuX(w->ir.x);
			w->ir.y = Menu_ScreenToMenuY(w->ir.y);
			if (!steadyOn[i]) {
				steadyX[i] = holdX[i] = w->ir.x;
				steadyY[i] = holdY[i] = w->ir.y;
				steadySpeed[i] = 0.0f;
				steadyOn[i] = true;
			} else {
				// A point on a short rope (kHold menu units): it stays where
				// it is while the Remote's point wanders inside the rope's
				// reach, and is pulled along by only what goes past it. The
				// camera's few-pixel shake never gets past; a real move
				// does, at once.
				const float hx = w->ir.x - holdX[i], hy = w->ir.y - holdY[i];
				const float reach = sqrtf(hx * hx + hy * hy);
				if (reach > kHold) {
					holdX[i] += hx * (reach - kHold) / reach;
					holdY[i] += hy * (reach - kHold) / reach;
				}
				const float dx = holdX[i] - steadyX[i], dy = holdY[i] - steadyY[i];
				// How fast the pointer goes (menu units a second), itself
				// smoothed, sets how much of this frame's move goes through.
				steadySpeed[i] += (sqrtf(dx * dx + dy * dy) * 60.0f - steadySpeed[i]) * Follow(kSpeedHz);
				const float k = Follow(kRestHz + kSpeedGain * steadySpeed[i]);
				steadyX[i] += dx * k;
				steadyY[i] += dy * k;
				w->ir.x = steadyX[i];
				w->ir.y = steadyY[i];
			}
			wroteX[i] = w->ir.x;
			wroteY[i] = w->ir.y;
			wroteA[i] = w->ir.angle;
			wrote[i] = true;
		} else {
			steadyOn[i] = false;
		}
		// A Classic Controller's left stick (scaled to the GameCube
		// stick's range, about +-100) when the GameCube stick is idle.
		const bool classic = remote && w->exp.type == WPAD_EXP_CLASSIC;
		int sx = userInput[i].pad.stickX;
		int sy = userInput[i].pad.stickY;
		if (classic && sx * sx + sy * sy <= kDeadZone * kDeadZone) {
			sx = userInput[i].WPAD_StickX(0) * 100 / 128;
			sy = userInput[i].WPAD_StickY(0) * 100 / 128;
		}
		const float r = sqrtf((float)(sx * sx + sy * sy));
		// The Wii Remote's own buttons are the low 16 bits; a Classic
		// Controller's are the high ones.
		const bool remotePressed = remote && (w->btns_d & 0xFFFF);
		const bool padPressed = userInput[i].pad.btns_d || (classic && (w->btns_d & ~0xFFFFu));

		if ((remote && (w->btns_d & kRemoteDpad)) || (userInput[i].pad.btns_d & kPadDpad)) {
			dpadHas[i] = true;
			dpadAnchored[i] = false;
		}
		if (dpadHas[i]) {
			bool back = r > kClaim;  // the stick, really pushed
			if (remote && w->ir.valid) {
				if (!dpadAnchored[i]) {
					dpadX[i] = w->ir.x;
					dpadY[i] = w->ir.y;
					dpadAnchored[i] = true;
				} else if (hypotf(w->ir.x - dpadX[i], w->ir.y - dpadY[i]) > kRemoteMove) {
					back = true;
					padHas[i] = false;  // the Remote's own pointer
				}
			}
			if (!back) {
				w->ir.valid = 0;
				userInput[i].pad.stickX = 0;
				userInput[i].pad.stickY = 0;
				if (classic) w->exp.classic.ljs.pos = w->exp.classic.ljs.center;  // drift steps nothing
				continue;
			}
			dpadHas[i] = false;
			if (r > kClaim) {
				padHas[i] = true;  // the pad's pointer, where it was
				anchored[i] = false;
			}
		}

		if (remotePressed) {
			padHas[i] = false;
		} else if (padHas[i] && remote && w->ir.valid) {
			// The Remote's own pointer (the scan refreshed it): it takes
			// back over once it really moves.
			if (!anchored[i]) {
				anchorX[i] = w->ir.x;
				anchorY[i] = w->ir.y;
				anchored[i] = true;
			} else if (hypotf(w->ir.x - anchorX[i], w->ir.y - anchorY[i]) > kRemoteMove) {
				padHas[i] = false;
			}
		}
		if (!padHas[i] && (padPressed || r > kClaim || (!remote && r > kDeadZone))) {
			padHas[i] = true;
			anchored[i] = false;
			// The pad's pointer starts where the Remote's is: pointing at a
			// game, then pressing a Classic Controller's (or a GameCube
			// controller's) A, put the pointer back in the screen's middle,
			// or where the pad last left it, before the press landed.
			if (remote && w->ir.valid) {
				x[i] = w->ir.x;
				y[i] = w->ir.y;
				placed[i] = true;
			}
		}
		if (!padHas[i]) {
			// The Remote has the channel: a stick short of a real push is
			// centred, so drift steps nothing.
			if (r <= kClaim) {
				userInput[i].pad.stickX = 0;
				userInput[i].pad.stickY = 0;
			}
			// Pointing just past an edge keeps the pointer on it, as
			// the pad's does, instead of letting it slide off screen.
			// The edge is the TV's, in menu units: past 0 and 640 on a
			// widescreen or smaller menu, where the corners' buttons sit.
			if (remote && w->ir.valid) {
				if (w->ir.x < minX) w->ir.x = minX;
				if (w->ir.y < minY) w->ir.y = minY;
				if (w->ir.x > maxX) w->ir.x = maxX;
				if (w->ir.y > maxY) w->ir.y = maxY;
				if (wrote[i]) {
					wroteX[i] = w->ir.x;
					wroteY[i] = w->ir.y;
				}
			}
			continue;
		}
		if (!placed[i]) {
			x[i] = screenwidth / 2;
			y[i] = screenheight / 2;
			placed[i] = true;
		}
		// Quadratic response past the dead zone: fine control near it,
		// about 14 px per frame at full tilt.
		if (r > kDeadZone) {
			float t = (r - kDeadZone) / (float)(100 - kDeadZone);
			if (t > 1.0f) t = 1.0f;
			const float speed = 1.0f + 13.0f * t * t;
			x[i] += speed * sx / r;
			y[i] -= speed * sy / r;  // stick up is positive, screen y grows downward
		}
		if (x[i] < minX) x[i] = minX;
		if (y[i] < minY) y[i] = minY;
		if (x[i] > maxX) x[i] = maxX;
		if (y[i] > maxY) y[i] = maxY;
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

	// read wiimote accelerometer and IR data, on the four Wii Remote
	// channels only: the menu never reads the Balance Board's slot. (The
	// crash with a board on was its missing command queue: wpadqueue.cpp.)
	for (int chan = WPAD_CHAN_0; chan <= WPAD_CHAN_3; ++chan)
	{
		WPAD_SetDataFormat(chan, WPAD_FMT_BTNS_ACC_IR);
		WPAD_SetVRes(chan, screenwidth, screenheight);
	}

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
