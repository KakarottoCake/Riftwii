// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2009 Tantric (libwiigui template) <https://github.com/dborth/libwiigui>
// SPDX-License-Identifier: GPL-3.0-or-later
/****************************************************************************
 * RiftWii
 *
 * rift_menu.cpp
 * The frontend, in a light Wii-Menu-like look (skin.hpp):
 *   Home      the games on the SD card and the USB drive as a page of
 *             tiles (plus the disc drive), a filter (games with mods, the
 *             default, or all games), the clock, and Settings.
 *   Game      the picked game's page: Mods (its own page, where A turns a
 *             pack on or off and steps its options), Saves, Cheats and
 *             the picture settings; Start leaves for the boot.
 *   Settings  the menu IOS, rescan and exit.
 * On Start the last frame stays on screen and the boot log prints into
 * its white card (see main.cpp). The launch state itself
 * (riftwii/launch.hpp) is host-tested; this file only draws and reads the
 * pads. Built on the vendored libwiigui template.
 ***************************************************************************/

#include <malloc.h>
#include <gccore.h>
#include <ogcsys.h>
#include <dirent.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <ctime>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <new>
#include <set>
#include <unordered_map>
#include <sstream>
#include <wiiuse/wpad.h>
#include <ogc/lwp_watchdog.h>

#include "libwiigui/gui.h"
#include "transition.hpp"
#include "drawtrace.hpp"
#include "gui_flowlist.hpp"
#include "gui_prompts.hpp"
#include "gui_tiles.hpp"
#include "memlimits.hpp"
#include "modpicture.hpp"
#include "modtiles.hpp"
#include "vsdimage.hpp"
#include "bannerplay.hpp"
#include "banners.hpp"
#include "bannersound.hpp"
#include "riftwii/gxpatches.hpp"
#include "riftwii/offlinegames.hpp"
#include "riftwii/titles.hpp"
#include "riftwii/homeorder.hpp"
#include "riftwii/bnr.hpp"
#include "riftwii/gcbanner.hpp"
#include "boxart.hpp"
#include "cover3d.hpp"
#include "covers.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/gcbackup.hpp"
#include "riftwii/gcimport.hpp"
#include "riftwii/gccard.hpp"
#include "riftwii/qrcode.hpp"
#include "riftwii/update.hpp"
#include "gui_gamegrid.hpp"
#include "gui_searchkeys.hpp"
#include "guiscript.hpp"
#include "credits.hpp"
#include "gcadapter.hpp"
#include "skin.hpp"
#include "riftwii/skinpaint.hpp"
#include "wiidrc.h"
#include "menu.h"
#include "autorun.hpp"
#include "menumusic.hpp"
#include "menutheme.hpp"
#include "themesounds.hpp"
#include "screenshot.hpp"
#include "boot.hpp"
#include "demo.h"
#include "input.h"
#include "riftwii/apply.hpp"
#include "riftwii/patch.hpp"
#include "codebuilds.hpp"
#include "gclaunch.hpp"
#include "gcnative.hpp"
#include "gc_crash.h"
#include "riftwii/gccrash.hpp"
#include "gcpatch.hpp"
#include "vsdmake.hpp"
#include "gameextras.hpp"
#include "gameinfo.hpp"
#include "i18n.hpp"
#include "loadersettings.hpp"
#include "memorybudget.hpp"
#include "log.hpp"
#include "ios_reload.hpp"
#include "menuios.hpp"
#include "usbprobe.hpp"
#include "online.hpp"
#include "reportsend.hpp"
#include "modplan.hpp"
#include "restart.hpp"
#include "riitagsend.hpp"
#include "channel.hpp"
#include "riftwii/settingsfile.hpp"
#include "netpacks.hpp"
#include "netsock.hpp"
#include "sharednandfind.hpp"
#include "otpkey.hpp"
#include "wadinstall.hpp"
#include "video.h"

namespace transition = riftwii::wii::transition;

#define THREAD_SLEEP 100

using riftwii::wii::logf;
using riftwii::wii::ScanPackages;
using riftwii::wii::SaveChoices;
using riftwii::wii::CompileSelection;
using riftwii::wii::SelectDisc;
using riftwii::wii::SelectSdGame;
using riftwii::wii::SelectUsbGame;
using riftwii::wii::scan_sd_games;
using riftwii::wii::scan_usb_games;
using riftwii::wii::tr;

namespace skin = riftwii::wii::skin;

// Where the left and right pill of a two-button row sit (x of the 244 wide
// glass; centred on 320). The hovered pill grows to 110%, 12 more on each
// side, so the 36 gap keeps it clear of its neighbour (a 12 gap, as the
// pills had at 70 and 326, closed up and covered the other one).
constexpr int kPairLeftX = 58, kPairRightX = 338;

// For the session log: which screen the user reached last.
static const char* ScreenName(int menu)
{
	switch (menu)
	{
		case MENU_EXIT: return "exit";
		case MENU_HOME: return "game";
		case MENU_OPTIONS: return "settings";
		case MENU_LAUNCH: return "launch";
		case MENU_BOOT: return "boot unmodified";
		case MENU_DUMP: return "dump";
		case MENU_SOURCE: return "home";
		case MENU_CHANNEL: return "channel";
		case MENU_NEEDS_SD: return "needs an SD card";
		case MENU_PLAY_CHANNEL: return "launch (Wii channel)";
		default: return "?";
	}
}

static GuiWindow * mainWindow = nullptr;
static skin::GuiBackdrop * backdrop = nullptr;
static GuiSound * soundOver = nullptr;
static lwp_t guithread = LWP_THREAD_NULL;
static std::atomic<bool> guiHalt{true};
// Set by the GUI thread itself while it waits at the top of its loop for
// ResumeGui. Being suspended is not enough to call it halted: GX also
// suspends it in the middle of a frame while the GPU catches up (UpdateGUI).
static std::atomic<bool> guiParked{false};
// The menu's own thread, for the GUI watch (it is the one that called InitGUIThreads).
static lwp_t menuthread = LWP_THREAD_NULL;
static std::atomic<bool> hidePointers{false};

// The Wii's screen burn-in reduction (Wii Settings): while it is on, the
// menu dims after five idle minutes, as the Wii Menu does. A button, a
// stick or a moved pointer brings it back; that press does nothing else.
#ifndef RIFTWII_DIM_SECONDS
#define RIFTWII_DIM_SECONDS 300
#endif
static bool dimAllowed = false;
static u64 lastActive = 0;
static int dimAlpha = 0;
static bool swallowInput = false;
static int pointerX[4], pointerY[4];
static bool pointerSeen[4];

// What is in use, one bit each. The log names the ones seen last when the
// screen dims and the ones that woke it, so a problem report shows which
// controller the idle timer stopped seeing.
enum ActivitySource { kActRemote, kActPorts, kActAdapter, kActPad, kActGamePad, kActPointer, kActSources };
static const char* const kActNames[kActSources] = {
	"Wii Remote", "GameCube ports", "GameCube adapter", "menu pad", "Wii U GamePad", "pointer"};
static u64 lastSeen[kActSources];
static bool dimLogged = false;

static bool Pushed(int x, int y)
{
	return std::abs(x) > 40 || std::abs(y) > 40;
}

// Which controllers someone is using this frame, as ActivitySource bits.
// "menu pad" is what the menu reads: the GameCube ports' and the adapter's
// buttons together, and the stick the pointer has not taken.
static unsigned ActivitySources()
{
	unsigned used = 0;
	riftwii::wii::GcAdapterView adapter;
	const bool adapterOpen = riftwii::wii::GcAdapterMenuLastView(adapter) && adapter.open;
	for (int i = 0; i < 4; ++i) {
		const GuiTrigger& t = userInput[i];
		if (PAD_ButtonsHeld(i) || Pushed(PAD_StickX(i), PAD_StickY(i)))
			used |= 1u << kActPorts;
		if (adapterOpen && i < static_cast<int>(GCAD_PORTS) && adapter.present[i] &&
		    (adapter.pads[i].buttons || Pushed(adapter.pads[i].stick_x, adapter.pads[i].stick_y)))
			used |= 1u << kActAdapter;
		if (t.pad.btns_h || Pushed(t.pad.stickX, t.pad.stickY))
			used |= 1u << kActPad;
		if (t.wiidrcdata.btns_h)
			used |= 1u << kActGamePad;
		if (!t.wpad) continue;
		if (t.wpad->btns_h)
			used |= 1u << kActRemote;
		const bool seen = t.wpad->ir.valid;
		const int x = static_cast<int>(t.wpad->ir.x), y = static_cast<int>(t.wpad->ir.y);
		// A remote lying still jitters a little: only a real move counts.
		if (seen != pointerSeen[i] || (seen && (std::abs(x - pointerX[i]) > 12 || std::abs(y - pointerY[i]) > 12))) {
			pointerSeen[i] = seen;
			pointerX[i] = x;
			pointerY[i] = y;
			used |= 1u << kActPointer;
		}
	}
	return used;
}

// The adapter's state for the dim log: whether its reports still come.
static std::string AdapterState()
{
	riftwii::wii::GcAdapterView adapter;
	if (!riftwii::wii::GcAdapterMenuLastView(adapter) || !adapter.open) return "adapter not in use";
	std::string ports;
	for (int i = 0; i < static_cast<int>(GCAD_PORTS); ++i)
		if (adapter.present[i]) ports += (ports.empty() ? "" : ",") + std::to_string(i + 1);
	return "adapter " + std::to_string(adapter.reports) + " reports, link " + std::to_string(adapter.link) +
	       ", ports " + (ports.empty() ? std::string("none") : ports);
}

static void LogDimmed(u64 now)
{
	std::string seen;
	for (int s = 0; s < kActSources; ++s) {
		seen += s ? ", " : "";
		seen += kActNames[s];
		seen += lastSeen[s] ? " " + std::to_string(diff_sec(lastSeen[s], now)) + " s ago" : std::string(" never");
	}
	logf("Screen dimmed after %u s idle; last used: %s; %s\n", static_cast<unsigned>(diff_sec(lastActive, now)),
	     seen.c_str(), AdapterState().c_str());
}

static void LogWoke(unsigned used)
{
	std::string by;
	for (int s = 0; s < kActSources; ++s)
		if (used & (1u << s)) by += (by.empty() ? "" : ", ") + std::string(kActNames[s]);
	logf("Screen woke: %s; %s\n", by.c_str(), AdapterState().c_str());
}

static bool AnyHeld()
{
	for (int i = 0; i < 4; ++i) {
		const GuiTrigger& t = userInput[i];
		if (t.pad.btns_h || t.wiidrcdata.btns_h || (t.wpad && t.wpad->btns_h)) return true;
	}
	return false;
}

// The menu's own loops tick here (HaltGui and ResumeGui, every frame).
static volatile u64 g_menuTick = 0;

static void ResumeGui()
{
	g_menuTick = gettime();
	// Whatever held the cover loader (an unmount, a raw read, an IOS
	// reload) was the menu's own work, and it is done.
	riftwii::wii::CoverLoaderRelease();
	guiHalt = false;
	// Only from its park at the top of its loop: in the middle of a frame
	// GX may be holding it until the GPU catches up, and woken there it
	// would write on over commands the GPU has not read yet (UpdateGUI).
	if (guiParked)
		LWP_ResumeThread(guithread);
}

// Halted: parked at the top of its loop, its frame finished. Not merely
// suspended, which GX also does in the middle of a frame (UpdateGUI).
static bool GuiParked()
{
	return guiParked && LWP_ThreadIsSuspended(guithread);
}

static void HaltGui()
{
	g_menuTick = gettime();
	guiHalt = true;
	while(!GuiParked())
		usleep(THREAD_SLEEP);
}

// The crash screen (wii/core/crash.cpp) draws over the menu's last frame: the
// GUI thread stops drawing first, unless it is the thread that crashed.
void MenuHaltForCrash()
{
	if (guithread == LWP_THREAD_NULL || LWP_GetSelf() == guithread) return;
	guiHalt = true;
	for (int i = 0; i < 50 && !LWP_ThreadIsSuspended(guithread); ++i)
		usleep(10000);
}

static void *
UpdateGUI(void *arg)
{
	(void)arg;
	int i;

	// GX stops the thread that draws when its command buffer (the FIFO,
	// 256 KB, read by the GPU while the CPU writes) is nearly full, and lets
	// it go on once the GPU has read half of it. The thread it stops is
	// the "GX thread", by default the one that called GX_Init: the menu's
	// own thread, not this one. A frame that ran more than 240 KB ahead of
	// the GPU then suspended the menu's thread while this one wrote on,
	// round the ring and over commands the GPU had not read yet; a real
	// GPU reads that as garbage and never finishes the frame (GX_DrawDone
	// waits for ever), and the menu's thread stays suspended. Dolphin's GPU
	// keeps up, so it never showed there. A tester's Wii froze just so on a
	// game's page with the Indigo theme (both threads stopped, the GUI in
	// GX_DrawDone, the menu's thread outside HaltGui): most likely the
	// theme's full-screen picture under the page's outlined text, every
	// glyph a texture load, kept the GPU behind long enough. GX now stops
	// this thread instead, the one writing.
	GX_SetCurrentGXThread();

	while(1)
	{
		if(guiHalt)
		{
			guiParked = true;
			// Again if something wakes it while the menu still holds it
			// (GX lets "its" thread go when the GPU catches up, even one
			// suspended here).
			while(guiHalt)
				LWP_SuspendThread(LWP_GetSelf());
			guiParked = false;
		}
		else
		{
			riftwii::wii::GuiPhase("pads and scripts");
			UpdatePads();
			riftwii::wii::GuiScriptApply();
			riftwii::wii::ScreenshotPoll();
			const u64 now = gettime();
			const unsigned used = ActivitySources();
			for (int s = 0; s < kActSources; ++s)
				if (used & (1u << s)) lastSeen[s] = now;
			if (used || lastActive == 0) {
				if (dimAlpha > 0) swallowInput = true;
				if (dimLogged && used) LogWoke(used);
				dimLogged = false;
				lastActive = now;
			}
			const bool idle = dimAllowed && diff_sec(lastActive, now) >= RIFTWII_DIM_SECONDS;
			if (idle && !dimLogged) {
				LogDimmed(now);
				dimLogged = true;
			}
			dimAlpha = idle ? std::min(dimAlpha + 4, 150) : std::max(dimAlpha - 30, 0);
			if (swallowInput && !AnyHeld()) swallowInput = false;
			riftwii::wii::GuiPhase("transition start");
			riftwii::wii::transition::FrameStart();
			riftwii::wii::GuiPhase("drawing the windows");
			mainWindow->Draw();
			riftwii::wii::GuiPhase("transition end");
			// The old screen over the new one while it comes in, and this
			// frame kept for the next change (before the pointers).
			riftwii::wii::transition::FrameEnd();
			riftwii::wii::GuiPhase("pointers");

			for(i = 3; i >= 0; i--)
			{
				if(!hidePointers && userInput[i].wpad->ir.valid)
					Menu_DrawImg(userInput[i].wpad->ir.x-48, userInput[i].wpad->ir.y-48,
						96, 96, skin::hand[i].data, userInput[i].wpad->ir.angle, 1, 1, 255);
				DoRumble(i);
			}
			if (dimAlpha > 0)
				Menu_FillWholeScreen((GXColor){0, 0, 0, static_cast<u8>(dimAlpha)});
			if (const int flash = riftwii::wii::ScreenshotFlash())
				Menu_FillWholeScreen((GXColor){255, 255, 255, static_cast<u8>(flash)});

			riftwii::wii::GuiPhase("rendering (GX_DrawDone, vsync)");
			Menu_Render();
			riftwii::wii::GuiPhase("after the frame");
			riftwii::wii::GuiScriptAfterFrame(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight());
			riftwii::wii::ScreenshotAfterFrame(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight());

			riftwii::wii::GuiPhase("updating the windows");
			if (!idle && !swallowInput && !riftwii::wii::transition::Busy())
				for(i = 0; i < 4; i++)
					mainWindow->Update(&userInput[i]);
			riftwii::wii::GuiPhase("frame done");

			// An exit waits while the update's files are being swapped on
			// the card: the menu keeps drawing until they are.
			static bool toldWaiting = false;
			if (ExitRequested && riftwii::wii::CardWritesBusy() && !toldWaiting) {
				logf("Exit: waiting for the card writes to finish\n");
				toldWaiting = true;
			}
			if(ExitRequested && !riftwii::wii::CardWritesBusy())
			{
				// The power button fades out more slowly, as the Wii Menu does.
				const int step = ExitRequested == kExitPowerButton ? 6 : 15;
				if (ExitRequested == kExitPowerButton) logf("Power button: turning the Wii off\n");
				for(i = 0; i <= 255; i += step)
				{
					mainWindow->Draw();
					Menu_FillWholeScreen((GXColor){0, 0, 0, (u8)i});
					Menu_Render();
				}
				ExitApp();
			}
		}
	}
	return nullptr;
}

// The console's power button, or a Wii Remote's: the GUI thread fades the
// screen out and turns the Wii off. Called from interrupts: only the
// request is set here.
static void PowerPressed()
{
	if (!ExitRequested) ExitRequested = kExitPowerButton;
}

static void RemotePowerPressed(s32)
{
	PowerPressed();
}

// A way out of a stuck menu: a tester took the SD card out and nothing
// answered any more, HOME included (the menu waits on the card with the
// GUI thread halted). A thread of its own, never halted: once the menu
// has not ticked for 4 s, HOME on a Wii Remote, RESET or POWER on the
// console leaves RiftWii (POWER turns the Wii off) without touching the
// card. Stopped when the menu closes for a launch.
static lwp_t g_watchdog = LWP_THREAD_NULL;
static volatile bool g_watchdogOn = false;
static volatile bool g_resetPressed = false;

static void ResetPressed(u32, void*)
{
	g_resetPressed = true;
}

// For a minute after DrawTraceArm, and when the frames stop, with
// settings.txt's debug_gx on: once a second, whether the GUI thread
// is drawing frames, where it is, and whether GX has the menu's thread
// waiting (wii/menu/drawtrace.hpp).
static void LogGuiWatch()
{
	static u64 lastSample = 0;
	static u32 lastFrame = 0;
	static bool stallLogged = false;
	const u64 now = gettime();
	if (lastSample != 0 && diff_msec(lastSample, now) < 1000) return;
	const bool firstSample = lastSample == 0;
	const u32 frame = FrameTimer;
	const u32 framesSince = frame - lastFrame;
	lastSample = now;
	lastFrame = frame;

	const bool armed = riftwii::wii::g_drawTraceUntil != 0 && now <= riftwii::wii::g_drawTraceUntil;
	const bool stalled = !firstSample && framesSince == 0 && !guiHalt;
	if (!stalled) stallLogged = false;
	const bool newStall = riftwii::wii::g_gxWatch && stalled && !stallLogged;
	if (!armed && !newStall) return;

	const char* gui = GuiParked() ? "halted" : LWP_ThreadIsSuspended(guithread) ? "suspended by GX in a frame" : "running";
	const char* menu = menuthread == LWP_THREAD_NULL ? "unknown"
		: LWP_ThreadIsSuspended(menuthread) ? "suspended" : "not suspended";
	logf("GUI watch: frame %u (%u since the last line), halted %s, GUI thread %s, in \"%s\" (render step \"%s\"), "
	     "element \"%s\", menu tick %d ms ago, menu thread %s, GX FIFO overflows %u\n",
	     static_cast<unsigned>(frame), static_cast<unsigned>(framesSince), guiHalt ? "yes" : "no", gui,
	     riftwii::wii::g_guiPhase, menuRenderStep, riftwii::wii::g_guiElement,
	     static_cast<int>(diff_msec(g_menuTick, now)), menu, static_cast<unsigned>(GX_GetOverflowCount()));
	if (!riftwii::wii::g_gxTrace) return;
	// The elements last started, newest first: with the GUI waiting for the
	// GPU after each one, the first is the one the GPU stopped in.
	riftwii::wii::GxDrawRecord records[8];
	const int count = riftwii::wii::GxTraceLast(records, newStall ? 8 : 1);
	for (int i = 0; i < count; ++i) {
		const riftwii::wii::GxDrawRecord& r = records[i];
		logf("GX trace %d: frame %u, element %s \"%s\", last picture %p, %ux%u %s\n", i + 1,
		     static_cast<unsigned>(r.frame), r.kind, r.name, r.texture, static_cast<unsigned>(r.width),
		     static_cast<unsigned>(r.height), r.texture ? riftwii::wii::GxFormatName(r.format) : "-");
	}
	if (newStall) stallLogged = true;
}

// GX's command FIFO filling up (UpdateGUI): the GUI thread then waits for
// the GPU. Said in the log, at most once a minute, for problem reports.
static void LogGxOverflows()
{
	static u32 told = 0;
	static u64 toldAt = 0;
	const u32 count = GX_GetOverflowCount();
	if (count == told) return;
	const u64 now = gettime();
	if (told != 0 && diff_sec(toldAt, now) < 60) return;
	logf("Graphics: the menu's drawing ran ahead of the GPU %u time(s) so far; each time the command buffer filled up "
	     "and the GUI thread waited for the GPU to catch up\n", static_cast<unsigned>(count));
	told = count;
	toldAt = now;
}

static void* MenuWatchdog(void*)
{
	while (g_watchdogOn) {
		usleep(200000);
		LogGuiWatch();
		LogGxOverflows();
		const u64 tick = g_menuTick;
		if (!g_watchdogOn) break;
		if (tick == 0 || diff_msec(tick, gettime()) < 4000) {
			g_resetPressed = false;  // RESET in a menu that answers is its own
			continue;
		}
		// An update being put in place is busy, not stuck: nothing cuts it
		// short (the power button waits for it, as the GUI's exit does).
		if (riftwii::wii::CardWritesBusy()) continue;
		bool home = false;
		if (GuiParked()) {
			// Nobody else reads the Remotes now.
			WPAD_ScanPads();
			for (int c = 0; c < 4; ++c) home = home || (WPAD_ButtonsDown(c) & WPAD_BUTTON_HOME);
		} else {
			for (int c = 0; c < 4; ++c) home = home || (userInput[c].wpad && (userInput[c].wpad->btns_h & WPAD_BUTTON_HOME));
		}
		if (ExitRequested == kExitPowerButton) SYS_ResetSystem(SYS_POWEROFF, 0, 0);
		if (home || g_resetPressed) SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
	}
	return nullptr;
}

void MenuWatchdogStop()
{
	g_watchdogOn = false;
}

void InitGUIThreads()
{
	dimAllowed = CONF_Init() >= 0 && CONF_GetScreenSaverMode() == 1;
	SYS_SetPowerCallback(PowerPressed);
	SYS_SetResetCallback(ResetPressed);
	WPAD_SetPowerButtonCallback(RemotePowerPressed);
	menuthread = LWP_GetSelf();
	riftwii::wii::GxTraceStart();
	if (LWP_CreateThread(&guithread, UpdateGUI, nullptr, nullptr, 24576, 70) < 0)
		ExitApp();
	HaltGui();
	g_watchdogOn = true;
	LWP_CreateThread(&g_watchdog, MenuWatchdog, nullptr, nullptr, 8192, 80);
}

// A text at a fixed spot, left-aligned or centred on the screen.
static void Place(GuiText& t, int x, int y, bool centre = false)
{
	t.SetAlignment(centre ? ALIGN_H::CENTRE : ALIGN_H::LEFT, ALIGN_V::TOP);
	t.SetPosition(x, y);
}

// The `n`th lowest set bit of `mask`, or 0.
static u32 NthBit(u32 mask, int n)
{
	for (u32 bit = 1; bit != 0; bit <<= 1) {
		if (!(mask & bit)) continue;
		if (n-- == 0) return bit;
	}
	return 0;
}

// A painted button (skin textures) triggered by A and by a hotkey. The
// Wii U GamePad mirrors the Wii names; every button carries a GamePad and
// a GameCube hotkey so the menus are drivable without a pointer.
struct SkinButton {
	// libwiigui fires a button-only trigger only when the buttons pressed
	// equal its whole mask (per controller), so "B or HOME" as one mask
	// never fired on a Wii Remote or Classic Controller: each hotkey gets a
	// trigger of its own (the Nth Remote, Classic, GameCube and GamePad
	// button together; libwiigui compares each controller on its own).
	static constexpr int kMaxHot = 4;  // with trigA, libwiigui's 5 triggers
	GuiImage image;
	GuiImage imageOver;
	GuiImage icon;
	GuiText text;
	GuiTrigger trigA;
	GuiTrigger trigHot[kMaxHot];
	GuiButton button;
	// `x`, `y`: where the visible shape starts; `margin`: the texture's
	// transparent border around it; `scale` shrinks the button (and its
	// hit area) around the same top left corner.
	SkinButton(const skin::Tex& face, const skin::Tex& faceOver, int margin, int x, int y, const char* label,
		   u32 wpadHot, u16 padHot, u16 drcHot, const skin::Tex* iconTex = nullptr, float scale = 1.0f)
		: image(face.data, face.w, face.h), imageOver(faceOver.data, faceOver.w, faceOver.h),
		  icon(iconTex ? iconTex->data : nullptr, iconTex ? iconTex->w : 0, iconTex ? iconTex->h : 0),
		  text(label, 22, skin::kInk),
		  button(static_cast<int>(face.w * scale), static_cast<int>(face.h * scale))
	{
		trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, WIIDRC_BUTTON_A);
		button.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		button.SetPosition(x - static_cast<int>(margin * scale), y - static_cast<int>(margin * scale));
		image.SetScale(scale);
		imageOver.SetScale(scale);
		icon.SetScale(0.5f + scale / 2);
		button.SetImage(&image);
		button.SetImageOver(&imageOver);
		if (label) button.SetLabel(&text);
		if (iconTex) {
			icon.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
			button.SetIcon(&icon);
		}
		if (soundOver) button.SetSoundOver(soundOver);
		button.SetTrigger(&trigA);
		for (int k = 0; k < kMaxHot; ++k) {
			const u32 remote = NthBit(wpadHot & 0xFFFF, k);
			const u32 classic = NthBit(wpadHot & 0xFFFF0000u, k);
			const u16 pad = static_cast<u16>(NthBit(padHot, k));
			const u16 drc = static_cast<u16>(NthBit(drcHot, k));
			if (!remote && !classic && !pad && !drc) break;
			// An empty half would match any press of the other controller
			// (0 == 0), so it gets a bit no controller sends.
			constexpr u32 kNoRemote = 0x0040, kNoClassic = 0x0100u << 16;
			trigHot[k].SetButtonOnlyTrigger(-1, (remote ? remote : kNoRemote) | (classic ? classic : kNoClassic), pad, drc);
			button.SetTrigger(&trigHot[k]);
		}
		button.SetEffectGrow();
	}
	bool Clicked() { return button.GetState() == STATE::CLICKED; }
};

// libwiigui only clears a hover-selected button while the pointer is live:
// with an invalid pointer a stale SELECTED survives, and the next A press
// fires both it and the focused tile or row. Drop stale selection when no
// pointer is live; hotkeys (BUTTON_ONLY) and fresh hovers are unaffected.
static bool AnyPointerLive() {
	for (int i = 0; i < 4; ++i)
		if (userInput[i].wpad && userInput[i].wpad->ir.valid) return true;
	return false;
}
// A button that only its hotkeys press: the prompts along the bottom name
// it instead (off the screen, so the pointer never lands on it).
static void HideButton(SkinButton& b)
{
	b.button.SetPosition(-4000, -4000);
	b.button.SetVisible(false);
}

static void ClearStaleButtons(std::initializer_list<GuiButton*> buttons) {
	if (AnyPointerLive()) return;
	for (GuiButton* b : buttons)
		if (b->GetState() == STATE::SELECTED) b->ResetState();
}

// Flattened to one capped row: raw driver errors are sentences.
static std::string FlatCapped(const std::string& text, std::size_t max)
{
	std::string flat;
	for (char c : text) {
		if (c == '\n' || c == '\r') {
			if (!flat.empty() && flat.back() != ' ') flat += ' ';
		} else {
			flat += c;
		}
	}
	if (flat.size() > max) {
		std::size_t cut = max;  // not inside a UTF-8 character
		while (cut > 0 && (static_cast<unsigned char>(flat[cut]) & 0xC0) == 0x80) --cut;
		flat = flat.substr(0, cut) + "...";
	}
	return flat;
}

// Hard-wrapped rows for a flow list: XML errors can hold long runs
// without spaces, which word wrapping never breaks.
static std::vector<std::string> Chunks(const std::string& text, std::size_t width, std::size_t maxRows)
{
	std::vector<std::string> rows;
	std::string flat = FlatCapped(text, width * maxRows);
	while (!flat.empty() && rows.size() < maxRows) {
		std::size_t cut = flat.size() <= width ? flat.size() : flat.rfind(' ', width);
		if (cut == std::string::npos || cut < width / 2) cut = std::min(width, flat.size());
		while (cut > 1 && cut < flat.size() && (static_cast<unsigned char>(flat[cut]) & 0xC0) == 0x80) --cut;
		rows.push_back(flat.substr(0, cut));
		flat = flat.substr(cut);
		while (!flat.empty() && flat.front() == ' ') flat.erase(0, 1);
	}
	return rows;
}

// One short problem per image catalog for Home's status line.
static std::string ShortSourceProblem(const char* tag, const riftwii::wii::ImageCatalog& catalog)
{
	if (!catalog.games.empty() || catalog.status.empty()) return "";
	if (catalog.status == std::string(tag) + ": select to scan") return "";
	std::string reason = catalog.status;
	const std::string prefix = std::string(tag) + ": ";
	if (reason.compare(0, prefix.size(), prefix) == 0) reason = reason.substr(prefix.size());
	if (reason.compare(0, 8, "No valid") == 0) return tr("{1}: no games in /wbfs or /games", {tag});
	if (reason == "no USB mass-storage device is inserted") return "";  // no drive is not a problem
	if (catalog.status == tr("USB: a Wii U drive (RiftWii leaves it alone)")) return "";  // nor is a Wii U's own drive
	if (reason == "no SD card is inserted") return tr("SD: no card");
	return std::string(tag) + ": " + FlatCapped(reason, 110);
}

// "Menu sounds": Quiet (the default) softens the tick the pointer makes
// moving onto something, which a tester found too loud.
static void ApplyMenuSounds()
{
	const std::string& v = riftwii::wii::Settings().menu_sounds;
	GuiSound::hoverPercent = v == "off" ? 0 : v == "quiet" ? 60 : 100;
	GuiSound::otherPercent = v == "off" ? 0 : v == "quiet" ? 80 : 100;
}
static std::string ReturnToNote()
{
	// A test switch in settings.txt turns it off whatever this says.
	if (riftwii::wii::debug_off("returnto"))
		return tr("Off for testing: settings.txt has \"debug_off = returnto\", so games go back to the Wii Menu. Press A here to take that switch out.");
	unsigned version = 0;
	return riftwii::wii::ChannelInstalled(version) ? tr("The Wii Menu button in a game's HOME Menu brings you back to RiftWii. It needs the RiftWii channel installed.") : tr("The Wii Menu button in a game's HOME Menu can bring you back to RiftWii once the RiftWii channel is installed (Settings).");
}
static const char* HomeSourceName(const std::string& v)
{
	return v == "sd" ? tr("SD card") : v == "usb" ? tr("USB drive") : v == "gamecube" ? tr("GameCube")
		: v == "wii" ? tr("Wii channels") : tr("All games");
}

static std::string HomeSourceNote(const std::string& v)
{
	if (v == "gamecube") return tr("Home lists the GameCube games only, from sd:/games.");
	return tr("Which games Home lists: every drive's and the Wii's own, or one place's only. With a game on both drives, one copy is enough.");
}

static const char* HomeSortName(const std::string& v)
{
	if (v == "custom") return tr("Your order");
	return v == "recent" ? tr("Last played") : v == "most" ? tr("Most played") : tr("A to Z");
}

static const char* MenuSoundsName(const std::string& v)
{
	return v == "off" ? tr("Off") : v == "normal" ? tr("Normal") : tr("Quiet");
}

// The Menu IOS setting's label and what choosing a slot means.
static std::string MenuIosLabel(int slot)
{
	return "IOS " + std::to_string(slot == 0 ? 58 : slot);
}
static std::string MenuIosNote(int slot)
{
	const int running = riftwii::wii::MenuCiosSlot();
	std::string note = slot == 0
		? tr("The menu runs under the Homebrew Channel's IOS (the default).")
		: tr("The menu and every game run under cIOS {1}, so a cIOS with fakemote makes USB DS3/DS4 pads work as Wii Remotes. USB drives in the menu need a base-58 cIOS.",
		     {std::to_string(slot)});
	if (slot != running) note += std::string(" ") + tr("Takes effect the next time RiftWii starts.");
	return note;
}

// The pack's name without ".xml", for display; a code build's is its
// folder's ("rex_/RSBE01.GCT": "rex_ (codes)"; one inside a virtual SD
// card, "pm.raw/Project+/RSBE01.GCT": "Project+ (in pm.raw)").
static std::string PackName(const std::string& file)
{
	if (const std::string image = riftwii::wii::VsdImageOfKey(file); !image.empty()) {
		const std::string rest = file.substr(image.size() + 1);
		return rest.substr(0, rest.find('/')) + " (in " + image + ")";
	}
	if (file.size() > 4) {
		const std::string ext = file.substr(file.size() - 4);
		if (strcasecmp(ext.c_str(), ".xml") == 0) return file.substr(0, file.size() - 4);
		const std::size_t slash = file.find('/');
		if (strcasecmp(ext.c_str(), ".gct") == 0 && slash != std::string::npos) {
			const std::string top = file.substr(0, slash);
			return strcasecmp(top.c_str(), "codes") == 0 ? "sd:/codes" : top + " (codes)";
		}
	}
	return file;
}

// A pack as the menu names it: the name its author gave it (its section,
// riftwii::pack_title), else its file's (a tester saw
// "mkwiiriivoslottest" where the pack had a name of its own).
static std::string PackLabel(const riftwii::LaunchPackage& p)
{
	const std::string title = riftwii::pack_title(p);
	return title.empty() ? PackName(p.file) : FlatCapped(title, 44);
}

// What the status line says about a focused pack.
static std::string PackSummary(const riftwii::LaunchPackage& p)
{
	if (p.code_build()) {
		const std::string image = p.gct_path.compare(0, 5, "vsd:/") == 0 ? riftwii::wii::VsdImageOfKey(p.file) : "";
		const std::string where = !image.empty() ? p.file : p.gct_path;
		if (!image.empty())
			return p.enabled ? "On. Runs the codes in " + where + "; the game gets " + image + " as its SD card."
					 : "Off. A turns on the codes in " + where + ".";
		return p.enabled ? "On. Runs the codes in " + where + "; they load the build's files from the SD card."
				 : "Off. A turns on the codes in " + where + ".";
	}
	if (!p.valid) return "This XML cannot be read; the error is listed under it.";
	const std::size_t n = p.package.options.size();
	if (!p.enabled) {
		if (n == 0) return tr("Off. A turns it on.");
		return n == 1 ? tr("Off. A turns it on and shows its setting.")
			      : tr("Off. A turns it on and shows its {1} settings.", {std::to_string(n)});
	}
	std::size_t on = 0;
	for (const riftwii::Option& o : p.package.options)
		if (o.selected != 0 && o.selected <= o.choices.size()) ++on;
	if (n == 0) return "On. It applies as a whole.";
	if (on == 0) return "On, but nothing chosen yet: pick its settings below.";
	return tr("On, {1} of {2} settings chosen.", {std::to_string(on), std::to_string(n)});
}

// ---------------------------------------------------------------------------
// Home

// The Home views, stepped with 1; the one last used is remembered in
// settings.txt ("view").
enum class Filter { Mods, All, Recent, Favorites };
static Filter g_filter = Filter::Mods;
static bool g_filterLoaded = false;
static bool g_scanned = false;   // the drives were read this session
static int g_homeFocus = 0;      // the focused tile, kept across screens
static bool g_focusRestored = false;  // opened on the last game played
static riftwii::PackIndex g_packs;

static const char* FilterLabel(Filter f)
{
	switch (f) {
		case Filter::Mods: return "Games with mods";
		case Filter::Recent: return "Recently played";
		case Filter::Favorites: return "Favourites";
		default: return "All games";
	}
}
// For the Wii Menu's bar, whose corner left of the dip holds one short
// line ("Recently played" wrapped onto the round button below).
static const char* FilterShortLabel(Filter f)
{
	switch (f) {
		case Filter::Mods: return "Mods";
		case Filter::Recent: return "Recent";
		case Filter::Favorites: return "Favourites";
		default: return "All games";
	}
}
static const char* FilterKey(Filter f)
{
	return f == Filter::Mods ? "mods" : f == Filter::Recent ? "recent" : f == Filter::Favorites ? "favorites" : "all";
}
static void LoadFilter()
{
	if (g_filterLoaded) return;
	g_filterLoaded = true;
	const auto& other = riftwii::wii::Settings().other;
	const auto it = other.find("view");
	if (it == other.end()) return;
	if (it->second == "all") g_filter = Filter::All;
	else if (it->second == "recent" && riftwii::wii::History().size() != 0) g_filter = Filter::Recent;
	else if (it->second == "favorites" && !riftwii::wii::Settings().favorites.empty()) g_filter = Filter::Favorites;
}

// Which games have packs: every XML in sd:/riivolution, usb:/riivolution
// and the network packs' cache, by game ID only.
static void LoadPackIndex()
{
	g_packs = riftwii::PackIndex();
	bool limited = false;
	for (const riftwii::wii::PackFile& pack : riftwii::wii::ListPackFiles(256, limited)) {
		const std::string text = riftwii::wii::ReadPackText(pack.path);
		if (!text.empty()) g_packs.add(text);
	}
	for (const riftwii::wii::CodeBuildFile& b : riftwii::wii::ListCodeBuilds()) g_packs.add_game(b.game_id);
	logf("Home: %u pack(s) indexed\n", static_cast<unsigned>(g_packs.size()));
}

// What the player typed into the search; empty when none. While it is set
// it picks from every game on the drives, whatever the filter.
static std::string g_search;

// Trimmed, so a lone space clears the search.
static std::string TrimSearch(const std::string& typed)
{
	const std::size_t from = typed.find_first_not_of(' ');
	return from == std::string::npos ? "" : typed.substr(from, typed.find_last_not_of(' ') - from + 1);
}

// Every word of `typed` is in the name or the game ID, in any order.
// A searched word that names the GameCube: "gc", or "gamecube" or the
// start of it ("game" alone is too common: it is in many names).
static bool NamesGameCube(const std::string& word)
{
	if (strcasecmp(word.c_str(), "gc") == 0) return true;
	return word.size() >= 5 && strncasecmp("gamecube", word.c_str(), word.size()) == 0;
}

// Every typed word is in the name or the ID; for a GameCube game, a word
// may name the GameCube instead.
static bool WordsMatch(const std::string& typed, const std::string& name, const std::string& id, bool gamecube = false)
{
	std::istringstream words(typed);
	std::string word;
	while (words >> word) {
		if (gamecube && NamesGameCube(word)) continue;
		if (!strcasestr(name.c_str(), word.c_str()) && !strcasestr(id.c_str(), word.c_str())) return false;
	}
	return true;
}

static bool MatchesSearch(const std::string& name, const std::string& id, bool gamecube)
{
	return WordsMatch(g_search, name, id, gamecube);
}

struct HomeEntry {
	// Channel: a WiiWare or Virtual Console game installed on the Wii
	// (FrontendState::channels).
	enum class Kind { Disc, Usb, Sd, Channel } kind;
	std::size_t index;
	// One of the game's profiles (riftwii/profiles.hpp): its key. Empty for
	// the game's own tile.
	std::string profile;
	// What the search reads: the tile's name (and a profile's game's name).
	std::string searchName;
};

static std::string ChannelName(const riftwii::wii::NandChannel& c)
{
	return riftwii::wii::nand_channel_name(c, riftwii::wii::BannerLanguageIndex());
}

// The banner of a Home entry for the Channels view: an image game's from
// its disc, a channel's from the Wii's memory.
static bool StoreEntryBanner(const FrontendState& state, const HomeEntry& e, std::string& why)
{
	if (e.kind == HomeEntry::Kind::Channel) return riftwii::wii::StoreChannelBanner(state.channels[e.index], why);
	const riftwii::wii::ImageGame& game = e.kind == HomeEntry::Kind::Usb ? state.usb_catalog.games[e.index]
		: state.sd_catalog.games[e.index];
	return riftwii::wii::StoreBanner(game, why);
}

static std::string GameName(const riftwii::wii::ImageGame& g)
{
	if (!g.display.empty()) return g.display;
	if (!g.title.empty()) return g.title;
	return g.id;
}

// A game's name in Home's own order (riftwii/homeorder.hpp): where it is
// (a drive, or the Wii's own channels) and its ID.
static std::string HomeKey(const FrontendState& state, const HomeEntry& entry)
{
	if (entry.kind == HomeEntry::Kind::Channel)
		return riftwii::home_order_key("wii", state.channels[entry.index].game_id, 0);
	const bool usb = entry.kind == HomeEntry::Kind::Usb;
	const riftwii::wii::ImageGame& game = usb ? state.usb_catalog.games[entry.index] : state.sd_catalog.games[entry.index];
	// A profile's tile has a place of its own: "usb:RMCE01@retro-rewind".
	const std::string& name = entry.profile.empty() ? game.id : entry.profile;
	return riftwii::home_order_key(usb ? "usb" : "sd", name, game.disc_number);
}

// `name` is the tile's, `gameName` the game's: a profile's tile sorts A to
// Z beside its game's, after it. `id` is the game's; `key` is what the
// history and the favourites know the tile by (the game ID, or a profile's
// "<ID>@<slug>"). `game` is the image game behind a drive's row (none for
// a channel), for the GameCube badge and the to-build flag.
struct HomeRow {
	std::string name, gameName;
	HomeEntry entry;
	std::string id, key;
	const riftwii::wii::ImageGame* game;
};

// The games in Settings > Home order (`ordered`; else A to Z), before a
// view or a search picks from them.
static std::vector<HomeRow> HomeRows(const FrontendState& state, bool ordered)
{
	using Row = HomeRow;
	std::vector<Row> rows;
	const auto add = [&](const std::string& name, HomeEntry::Kind kind, std::size_t i, const std::string& id,
				const riftwii::wii::ImageGame* game) {
		rows.push_back({name, name, {kind, i, "", name}, id, id, game});
	};
	// Settings > Games from: everything, or one place's games only (a
	// drive, the Wii's own WiiWare and Virtual Console games, or the
	// GameCube games, all on the SD card).
	const std::string& source = riftwii::wii::Settings().home_source;
	if (source == "all" || source == "usb")
		for (std::size_t i = 0; i < state.usb_catalog.games.size(); ++i)
			add(GameName(state.usb_catalog.games[i]), HomeEntry::Kind::Usb, i, state.usb_catalog.games[i].id,
				&state.usb_catalog.games[i]);
	if (source == "all" || source == "sd" || source == "gamecube")
		for (std::size_t i = 0; i < state.sd_catalog.games.size(); ++i) {
			if (source == "gamecube" && !state.sd_catalog.games[i].gamecube) continue;
			add(GameName(state.sd_catalog.games[i]), HomeEntry::Kind::Sd, i, state.sd_catalog.games[i].id,
				&state.sd_catalog.games[i]);
		}
	if (source == "all" || source == "wii")
		for (std::size_t i = 0; i < state.channels.size(); ++i)
			add(ChannelName(state.channels[i]), HomeEntry::Kind::Channel, i, state.channels[i].game_id, nullptr);
	// The games' profiles, each a tile of its own, with the first copy of
	// its game found on a drive (a game on both drives gets them once; the
	// Wii's channels take no packs). Only a game Home lists has them: a
	// profile of a game not found (not scanned yet, or deleted) stays on
	// the card, with its choices, and its tile comes back with the game.
	{
		std::set<std::string> withProfiles;
		const std::size_t games = rows.size();
		for (std::size_t r = 0; r < games; ++r) {
			const HomeEntry::Kind kind = rows[r].entry.kind;
			const bool onDrive = kind == HomeEntry::Kind::Usb || kind == HomeEntry::Kind::Sd;
			if (!onDrive || !withProfiles.insert(rows[r].id).second) continue;
			for (const riftwii::Profile* p : riftwii::wii::Profiles().tiles_for(rows[r].id, onDrive)) {
				Row row = rows[r];
				row.name = p->name;
				row.key = p->key();
				row.entry.profile = row.key;
				row.entry.searchName = p->name + " " + rows[r].gameName;
				rows.push_back(std::move(row));
			}
		}
	}
	std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
		// A leading "The" is not sorted on (The Legend of Zelda under L).
		const int by = strcasecmp(a.gameName.c_str() + riftwii::sort_name_start(a.gameName),
			b.gameName.c_str() + riftwii::sort_name_start(b.gameName));
		if (by != 0) return by < 0;
		const int whole = strcasecmp(a.gameName.c_str(), b.gameName.c_str());
		if (whole != 0) return whole < 0;
		// The game's own tile first, then its profiles by name.
		if (a.entry.profile.empty() != b.entry.profile.empty()) return a.entry.profile.empty();
		return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	});
	const std::string& order = riftwii::wii::Settings().home_sort;
	if (ordered && order == "custom") {
		// Your order: the games sd:/riftwii/order.txt lists first, as it
		// lists them, the rest after them A to Z.
		std::vector<std::string> keys;
		keys.reserve(rows.size());
		for (const Row& r : rows) keys.push_back(HomeKey(state, r.entry));
		std::vector<Row> arranged;
		arranged.reserve(rows.size());
		for (const std::size_t i : riftwii::arrange_home_order(riftwii::wii::HomeOrder(), keys)) arranged.push_back(rows[i]);
		rows.swap(arranged);
	} else if (ordered && order != "az") {
		// Settings > Home order: the played games first (latest or most
		// played), the rest after them A to Z.
		const riftwii::PlayHistory& history = riftwii::wii::History();
		const bool most = order == "most";
		std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
			const auto* pa = history.find(a.key);
			const auto* pb = history.find(b.key);
			if (!pa || !pb) return pa && !pb;
			if (most && pa->count != pb->count) return pa->count > pb->count;
			return pa->last > pb->last;
		});
	}
	return rows;
}

static void BuildHome(const FrontendState& state, std::vector<GridItem>& items, std::vector<HomeEntry>& entries)
{
	items.clear();
	entries.clear();
	{
		GridItem disc;
		disc.title = tr("Disc drive");  // translated whole: the tile splits it into lines
		disc.badge = "DISC";
		disc.hue = {88, 92, 104, 255};
		items.push_back(disc);
		entries.push_back({HomeEntry::Kind::Disc, 0, "", ""});
	}
	using Row = HomeRow;
	const bool searching = !g_search.empty();
	std::vector<Row> rows = HomeRows(state, !searching && g_filter != Filter::Recent);
	if (!searching && g_filter == Filter::Recent) {
		// The games played from RiftWii, the latest first.
		const riftwii::PlayHistory& history = riftwii::wii::History();
		rows.erase(std::remove_if(rows.begin(), rows.end(),
			[&](const Row& r) { return history.find(r.key) == nullptr; }), rows.end());
		std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
			return history.find(a.key)->last > history.find(b.key)->last;
		});
	}
	for (const Row& r : rows) {
		const bool profile = !r.entry.profile.empty();
		const bool mods = g_packs.has_packs(r.id);
		if (searching && !MatchesSearch(r.entry.searchName, r.id, r.game && r.game->gamecube)) continue;
		// A profile is a setup of mods: it stays in the mods view.
		if (!searching && g_filter == Filter::Mods && !mods && !profile) continue;
		if (!searching && g_filter == Filter::Favorites && riftwii::wii::Settings().favorites.count(r.key) == 0) continue;
		GridItem item;
		item.title = r.name;
		item.id = r.id;
		item.key = r.key;
		item.profile = profile;
		// A channel of an emulated NAND is on the SD card.
		const bool wiiChannel = r.entry.kind == HomeEntry::Kind::Channel && state.channels[r.entry.index].emu_nand.empty();
		const bool gamecube = r.game && r.game->gamecube;
		item.badge = r.entry.kind == HomeEntry::Kind::Usb ? "USB" : wiiChannel ? "WII" : gamecube ? "GC" : "SD";
		item.mods = mods || profile;
		item.to_build = r.game && !r.game->gc_patch_source.empty();
		item.hue = skin::HueFor(r.id);
		items.push_back(std::move(item));
		entries.push_back(r.entry);
	}
	// Settings > Disc Channel off: the tile goes (an empty view keeps its
	// note under the clock: Press 1 for all games).
	if (riftwii::wii::Settings().home_disc == "off") {
		items.erase(items.begin());
		entries.erase(entries.begin());
	}
}

// The tile at `from` moved to `to`, the ones between shifted by one.
template <typename T>
static void MoveInList(std::vector<T>& list, int from, int to)
{
	if (from < to) std::rotate(list.begin() + from, list.begin() + from + 1, list.begin() + to + 1);
	else std::rotate(list.begin() + to, list.begin() + from, list.begin() + from + 1);
}

// The player moved a game on Home (GuiGameGrid::TakeMove): the list
// follows, and sd:/riftwii/order.txt takes the new order. In another
// Home order, the order shown becomes the player's own, moved, so nothing
// jumps; true then, for the notice that says so.
static bool MoveHomeGame(const FrontendState& state, std::vector<GridItem>& items, std::vector<HomeEntry>& entries,
	int from, int to)
{
	const int count = static_cast<int>(items.size());
	if (from < 0 || to < 0 || from >= count || to >= count || from == to) return false;
	const std::string title = items[static_cast<std::size_t>(from)].title;
	MoveInList(items, from, to);
	MoveInList(entries, from, to);
	// Every game on the drives in the order Home had, the ones this view
	// leaves out too.
	std::vector<std::string> before;
	for (const HomeRow& r : HomeRows(state, true)) before.push_back(HomeKey(state, r.entry));
	riftwii::LoaderSettings& settings = riftwii::wii::Settings();
	const bool switched = settings.home_sort != "custom";
	const std::vector<std::string> order = switched ? riftwii::merge_home_order(before, riftwii::wii::HomeOrder())
		: riftwii::merge_home_order(riftwii::wii::HomeOrder(), before);
	std::vector<std::string> shown;
	for (const HomeEntry& e : entries)
		if (e.kind != HomeEntry::Kind::Disc) shown.push_back(HomeKey(state, e));
	riftwii::wii::SaveHomeOrder(riftwii::refill_home_order(order, shown));
	logf("Home order: %s moved from place %d to place %d\n", title.c_str(), from + 1, to + 1);
	if (switched) {
		logf("Home order: was %s, now the player's own, starting from the order shown\n", settings.home_sort.c_str());
		settings.home_sort = "custom";
		riftwii::wii::SaveSettings();
	}
	return switched;
}

// Games can be moved where the tiles are the drives' in Home's order:
// not in a search, nor in Recently played (always latest first), nor on
// the shelf views, which have no grid to move them in.
static bool HomeCanMove()
{
	const std::string& tiles = riftwii::wii::Settings().home_tiles;
	const bool shelf = tiles == "shelf" || tiles == "carousel" || tiles == "covers3d";
	return g_search.empty() && g_filter != Filter::Recent && !shelf;
}

static void SetHomeMovable(GuiGameGrid& grid, const std::vector<HomeEntry>& entries)
{
	const bool discFirst = !entries.empty() && entries[0].kind == HomeEntry::Kind::Disc;
	grid.SetMovable(HomeCanMove(), discFirst ? 1 : 0);
}

// The bottom bar with the clock's bump.
// A flat band in the bar's colours from `top` down to the screen's
// bottom: a shadow over what is above it, the accent's line with a lit
// edge under it, faint lines across the body, a little darker lower down
// (the Wii Menu's bottom panels).
static void DrawBand(int top)
{
	Menu_FillScreen(top - 5, 5, (GXColor){0, 0, 0, 14});
	Menu_FillScreen(top - 2, 2, (GXColor){0, 0, 0, 18});
	Menu_FillScreen(top, 1000, skin::kBar);
	const auto darker = [](GXColor c, float k) {
		return (GXColor){static_cast<u8>(c.r * k), static_cast<u8>(c.g * k), static_cast<u8>(c.b * k), c.a};
	};
	// Shaded in steps down to the screen's bottom (480 at the most).
	for (int y = top + 40; y < 480; y += 20) Menu_FillScreen(y, 20, darker(skin::kBar, 1.0f - 0.08f * (y - top) / 130.0f));
	for (int y = top + 7; y < 480; y += 4) Menu_FillScreen(y, 2, (GXColor){0, 0, 0, 7});
	Menu_FillScreen(top + 3, 2, (GXColor){255, 255, 255, 150});
	Menu_FillScreen(top, 3, skin::kAccent);
}

// A page's title band (Settings and the pages under it), as Wii Settings'
// own: the bar's colours from the screen's top down to kTitleBand, the
// accent's line along its foot, a soft shadow under that.
static constexpr int kTitleBand = 70;
class TitleBand : public GuiElement {
public:
	void Draw() override {
		Menu_FillScreen(-1000, 1000 + kTitleBand, skin::kBar);
		for (int y = 3; y < kTitleBand - 4; y += 4) Menu_FillScreen(y, 2, (GXColor){0, 0, 0, 7});
		Menu_FillScreen(kTitleBand - 5, 2, (GXColor){255, 255, 255, 150});
		Menu_FillScreen(kTitleBand - 3, 3, skin::kAccent);
		Menu_FillScreen(kTitleBand, 2, (GXColor){0, 0, 0, 26});
		Menu_FillScreen(kTitleBand + 2, 3, (GXColor){0, 0, 0, 12});
	}
};

class HomeBar : public GuiElement {
public:
	// Lifted by kLift so the round buttons (y 386) sit wholly inside the
	// bar, under its line, instead of poking out over it.
	static constexpr int kLift = 18;
	void Draw() override {
		f32 vx, vy, vw, vh;
		Menu_VisibleArea(&vx, &vy, &vw, &vh);
		// A widescreen menu takes the theme's wide bar when it has one. Past
		// the picture's sides, its flat ends (kFlat columns, clear of the
		// bump) are mirrored out to the screen's; below it, down to the
		// screen's bottom, its lowest kBody rows.
		const skin::Tex& picture = skin::WideMenu() && skin::barWide.data ? skin::barWide : skin::bar;
		const f32 top = 356 - kLift;
		skin::DrawExtended(picture, 320.0f - picture.w / 2.0f, top, vx, top, vx + vw, vy + vh, kFlat, kBody);
	}
private:
	static constexpr int kFlat = 140;  // clear of the dip (53% of 640) and of a bump (288)
	static constexpr int kBody = 40;
};

static std::string g_homeNotice;
static bool g_homeNoticeShown = false;  // its toast, once

// A notice that slides down at Home's top and goes again: an error (the
// warning colour's "!") or news (the accent's "i") on a card, the text
// wrapped to three lines at most.
class Toast : public GuiElement {
public:
	Toast() : text("", 17, skin::kInk) {
		text.SetParent(this);
		text.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	}
	void Show(const std::string& what, bool error) {
		if (what.empty()) return;
		text.SetWrap(false);
		text.SetText(what.c_str());
		const int one = text.GetTextWidth();
		lines = one <= kMaxText ? 1 : std::min(3, (one * 108 / 100 + kMaxText - 1) / kMaxText);
		textW = lines == 1 ? one : kMaxText;
		if (lines > 1) text.SetWrap(true, kMaxText, 3);
		isError = error;
		start = gettime();
		on = true;
	}
	void Draw() override {
		if (!on) return;
		const float ms = ticks_to_microsecs(gettime() - start) / 1000.0f / transition::SlowMotion();
		const float stay = isError ? 9000.0f : 6000.0f;
		float k = 1;
		if (ms < 360) k = transition::EaseBack(ms / 360);
		else if (ms > stay + 300) {
			on = false;
			return;
		} else if (ms > stay) {
			const float t = (ms - stay) / 300;
			k = 1 - t * t;
		}
		const int boxW = textW + 70, boxH = lines * 21 + 24;
		// It comes to rest at y 28, inside a TV's overscan (y 24).
		const float x = 320 - boxW / 2.0f, y = -boxH - 12 + (boxH + 40) * k;
		skin::DrawNine(skin::card9, x - 8, y - 8, boxW + 16, boxH + 16, riftwii::kCardCorner);
		skin::Draw(skin::noticeIcon[isError ? 1 : 0], x + 14, y + boxH / 2.0f - 14);
		text.SetPosition(static_cast<int>(x) + 54, static_cast<int>(y) + 12);
		text.Draw();
	}
private:
	static constexpr int kMaxText = 400;  // the box ends by x 555, clear of the search button (x 568)
	GuiText text;
	int lines = 1, textW = 0;
	bool isError = false, on = false;
	u64 start = 0;
};
// Covers: the games looked at this session; off after a network failure.
static std::set<std::string> g_coversChecked;
static bool g_coversOff = false;
// The shelf's boxes (wii/menu/boxart.cpp), asked for as the shelf comes near them.
static std::set<std::string> g_boxesChecked;
static bool g_boxesOff = false;
// The same for the 3D covers view's pictures (wii/menu/cover3d.hpp).
static std::set<std::string> g_covers3dChecked;
static bool g_covers3dOff = false;

// The Channels view: each game's own icon from its banner (wii/menu/banners.cpp),
// read from its image once and kept on the card. The icons of the page
// shown and of the pages either side are kept playing (the ones drawn
// last stay); the menu's loop loads them from the card, one a turn, so
// drawing never waits on the card, nor a page turn on its icons.
struct IconSlot {
	std::string id;
	std::unique_ptr<riftwii::wii::BannerPlayer> player;
	u32 used = 0;
};
static IconSlot g_icons[40];  // the page shown (and the next one's peeking column), and a page either side
static std::unordered_map<std::string, IconSlot*> g_iconIndex;  // the filled slots by game
static u32 g_iconClock = 0;
static std::set<std::string> g_bannersTried;   // read from the image this session (or tried)
static std::set<std::string> g_bannerAbsent;   // not on the card when last looked for
// GameCube games whose animated banner was asked for this session; none
// asked after a download failed for want of a network.
static std::set<std::string> g_gcBannersAsked;
static bool g_gcBannersOff = false;

static riftwii::wii::BannerPlayer* IconFor(const std::string& id)
{
	if (id.empty()) return nullptr;
	const auto found = g_iconIndex.find(id);
	if (found == g_iconIndex.end()) return nullptr;
	found->second->used = ++g_iconClock;
	return found->second->player.get();
}

// The menu's loop: reads `id`'s icon from the card (the GUI drawing on
// meanwhile). False when it has none.
static std::unique_ptr<riftwii::wii::BannerPlayer> LoadIcon(const std::string& id)
{
	std::vector<std::uint8_t> bytes;
	std::string error;
	std::unique_ptr<riftwii::wii::BannerPlayer> player(new riftwii::wii::BannerPlayer());
	if (!riftwii::wii::LoadBannerIcon(id, bytes)) return nullptr;
	if (!player->Load(bytes, true, error, id)) {
		logf("Icon of %s: %s\n", id.c_str(), error.c_str());
		return nullptr;
	}
	return player;
}

// With the GUI halted: the loaded icon in the slot drawn longest ago.
static void KeepIcon(const std::string& id, std::unique_ptr<riftwii::wii::BannerPlayer> player)
{
	IconSlot* victim = &g_icons[0];
	for (IconSlot& s : g_icons)
		if (s.used < victim->used) victim = &s;
	if (!victim->id.empty()) g_iconIndex.erase(victim->id);
	victim->id = id;
	victim->player = std::move(player);
	victim->used = ++g_iconClock;
	g_iconIndex[id] = victim;
}

// With the GUI halted: the game's icon let go, after a new banner was
// stored for it (a GameCube game's animated one), so the loop loads it again.
static void ForgetIcon(const std::string& id)
{
	const auto found = g_iconIndex.find(id);
	if (found == g_iconIndex.end()) return;
	IconSlot* slot = found->second;
	g_iconIndex.erase(found);
	slot->id.clear();
	slot->player.reset();
	slot->used = 0;
}

// The full banner, as the Wii Menu shows a channel before it starts:
// Start, or Settings for the game's page. The bar starts where the Wii
// Menu's Disc Channel starts its own (measured in Dolphin), just under
// the frames banners draw along their bottom edge.
static constexpr int kChannelBarTop = 354;

class ChannelView : public GuiElement {
public:
	riftwii::wii::BannerPlayer player;
	// How long the banner took on this console, for the log when it closes:
	// a banner that flashes on a Wii plays smoothly in Dolphin.
	u64 lastDraw = 0, drawTotalUs = 0, drawMaxUs = 0;
	unsigned frames = 0, late = 0;
	// Left playing after Start (g_lingerBanner): its sound is kept going
	// here, with no channel screen looping any more.
	bool tendSound = false;
	// A GameCube disc's picture has no name in it: the game's name is
	// written under its frame (riftwii/gcbanner.hpp's gc_picture_layout).
	GuiText caption{"", 26, (GXColor){40, 44, 52, 255}};
	ChannelView() {
		caption.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		caption.SetWrap(true, 540);
	}
	void Draw() override {
		const u64 start = gettime();
		if (tendSound) riftwii::wii::BannerSoundUpdate();
		if (lastDraw != 0 && ticks_to_microsecs(start - lastDraw) > 20000) ++late;  // a frame missed
		lastDraw = start;
		// Black over Home, and its depth too: whatever Home's icons left
		// in the depth buffer cannot hide any of the banner.
		GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
		// A band, not Menu_FillWholeScreen: it zooms with the channel as it
		// opens out of its tile.
		Menu_FillScreen(-1000, 3000, (GXColor){0, 0, 0, 255});
		GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
		player.Step();
		// The whole screen, as the Wii Menu shows a channel: wider than the
		// menu's 640 on a widescreen menu, taller at a smaller screen size.
		f32 vx, vy, vw, vh;
		Menu_VisibleArea(&vx, &vy, &vw, &vh);
		player.Draw(vx, vy, vw, vh, 255);
		if (player.Picture()) {
			// The name starts just under the frame and its shadow, clear of
			// the bar below.
			const f32 nameTop = vy + vh * (228.0f + riftwii::kGcBannerNameTop) / 456.0f;
			caption.SetPosition(static_cast<int>(vx + vw / 2), static_cast<int>(nameTop));
			caption.Draw();
		}
		const u64 took = ticks_to_microsecs(gettime() - start);
		drawTotalUs += took;
		drawMaxUs = std::max(drawMaxUs, took);
		++frames;
		// The bar the buttons sit on, over the banner's bottom edge: the
		// part the Wii Menu covers too, where banners leave their seams.
		DrawBand(kChannelBarTop);
	}
};

// The tile of the game last opened from Home: the game page zooms out of
// it and back into it.
static transition::Rect g_openRect;

// From the shelf: the game's box flies off it onto the page's cover, and
// back. Where the page shows the cover (CoverArt, MenuHome).
static ShelfFlight g_flight;
static bool g_flightOn = false;
constexpr float kPageCoverX = 528, kPageCoverY = 4;
static void FlightOut(float t)
{
	// Settling on the cover, then gone into it.
	const float fade = t < 0.8f ? 1.0f : 1.0f - (t - 0.8f) / 0.2f;
	DrawShelfFlight(g_flight, kPageCoverX, kPageCoverY, riftwii::kCoverWidth, riftwii::kCoverHeight,
		transition::Ease(t), static_cast<int>(255 * fade), false);
}
static void FlightBack(float t)
{
	DrawShelfFlight(g_flight, kPageCoverX, kPageCoverY, riftwii::kCoverWidth, riftwii::kCoverHeight,
		transition::Ease(t), 255, true);
}
static void FlightLanded() { SetShelfFlying(""); }

// The game `index` of `count` shown as the Wii Menu shows a channel: its
// banner, playing, with its sound; Start plays it, Settings opens its page. The
// arrows at the sides (or the D-pad's left and right) go to the games
// before and after it that have a banner, as the Wii Menu goes from
// channel to channel; `load` reads a game's opening.bnr (false: none).
// What was chosen, with `index` the game shown last. Built and closed
// with the GUI halted.
using BannerLoader = std::function<bool(int index, std::vector<std::uint8_t>& bytes)>;
// What the banner screen was left with.
enum class ChannelChoice { Back, Start, Page };
// Set by the banner screen's Start: the game's page presses its own Start
// as it opens (its checks and warnings, then the launch).
static bool g_startOnOpen = false;
// The game whose page the banner screen's Settings opened: back from the
// page, Home opens its banner again (as the Wii Menu's channel screen).
static std::string g_reopenBanner;

// Set when the banner screen's star changed a favourite: Home's
// Favourites list is built again.
static bool g_favoritesChanged = false;

// The banner Start was pressed on: left on screen, still playing, while
// the game's page runs its checks out of sight, until the launch screen
// (or the page, when a check keeps the player there) takes its place.
// A still picture of it looked as if the menu had frozen.
static std::unique_ptr<ChannelView> g_lingerBanner;

// With the GUI halted. Its sound stops with it and the menu's music comes back.
static void DropLingerBanner()
{
	if (!g_lingerBanner) return;
	riftwii::wii::BannerSoundStop();
	mainWindow->Remove(g_lingerBanner.get());
	g_lingerBanner.reset();
}

static ChannelChoice ShowChannel(int& index, int count, const std::string& firstId, const BannerLoader& load,
	const std::function<std::string(int)>& idOf)
{
	std::unique_ptr<ChannelView> owned(new ChannelView());
	ChannelView& view = *owned;
	std::string shownId;
	// The banner's sound starts a few frames after its banner, with the
	// menu drawing: decoding it took the longest part of a + or - (and
	// the screen stood still meanwhile).
	std::vector<std::uint8_t> pendingSound;
	int soundIn = -1;  // frames until it starts; -1 none due
	// After + or -, once the slide is over; a banner just opened starts
	// its sound at once, as on the Wii Menu (a tester heard it late).
	int soundDelay = 0;
	const auto logShown = [&] {
		if (view.frames == 0) return;
		logf("Banner of %s: %u frames; the CPU drew each in %.1f ms on average, %.1f ms at most; %u frame(s) late "
		     "(more than 20 ms after the one before)\n",
		     shownId.c_str(), view.frames, view.drawTotalUs / 1000.0 / view.frames, view.drawMaxUs / 1000.0, view.late);
		view.frames = view.late = 0;
		view.drawTotalUs = view.drawMaxUs = view.lastDraw = 0;
	};
	// Puts game `i`'s banner, read into `bytes` (from t0 to t1), up (and
	// its sound on), with the GUI halted. False when it is broken.
	const auto show = [&](int i, std::vector<std::uint8_t>& bytes, u64 t0, u64 t1) {
		std::string error;
		riftwii::OpeningBanner parts;
		std::vector<std::uint8_t> sound;
		// Checked before the banner showing is let go: a broken one is
		// skipped and the one up stays.
		bool parsed = false;
		try {
			// A GameCube disc's picture has no sound; the player checks it.
			parsed = riftwii::is_gc_banner(bytes.data(), bytes.size()) ||
				riftwii::parse_opening_bnr(bytes.data(), bytes.size(), parts, error, false);
		} catch (const std::bad_alloc&) {
			error = "out of memory for the banner";
		}
		if (!parsed) {
			logf("Banner of %s: %s\n", idOf(i).c_str(), error.c_str());
			return false;
		}
		sound.swap(parts.sound);
		parts = riftwii::OpeningBanner();
		const u64 t2 = gettime();
		if (!view.player.Load(bytes, false, error, idOf(i))) {
			logf("Banner of %s: %s\n", idOf(i).c_str(), error.c_str());
			return false;
		}
		view.caption.SetText(view.player.Picture() ? view.player.Name().c_str() : "");
		const u64 t3 = gettime();
		logShown();
		shownId = idOf(i);
		index = i;
		std::vector<std::uint8_t>().swap(bytes);
		pendingSound.swap(sound);
		soundIn = soundDelay;
		// Where a switch's wait goes (a tester: + and - lag).
		logf("Screen: channel %s (read %u ms, checked %u ms, banner %u ms)\n", shownId.c_str(),
			diff_msec(t0, t1), diff_msec(t1, t2), diff_msec(t2, t3));
		return true;
	};
	// Reads game `i`'s banner and puts it up. False when it has none.
	const auto open = [&](int i) {
		const u64 t0 = gettime();
		std::vector<std::uint8_t> bytes;
		if (!load(i, bytes)) return false;
		return show(i, bytes, t0, gettime());
	};
	if (!open(index)) return ChannelChoice::Page;
	(void)firstId;
	view.player.SetWidescreen(riftwii::wii::MenuWidescreen());
	// As the Wii Menu's channel screen: Start plays the game. Settings,
	// where the Wii Menu has its own button, opens the game's page (mods,
	// settings); B or HOME go back to Home (no button of its own: off
	// screen and hidden, for its hotkeys only).
	SkinButton backBtn(skin::pill, skin::pillOver, 4, -2000, -2000, nullptr,
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B | WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, PAD_BUTTON_B,
		WIIDRC_BUTTON_B | WIIDRC_BUTTON_HOME);
	backBtn.button.SetVisible(false);
	// + and - go to the next and previous game, as on the Wii Menu's
	// channel screen (a tester's muscle memory); Start is A on it.
	SkinButton goBtn(skin::pillPrimary, skin::pillPrimaryOver, 4, kPairRightX, 384, tr("Start"),
		WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A | PAD_BUTTON_START, WIIDRC_BUTTON_A);
	goBtn.text.SetColor(skin::kAccentInk);
	// A pill like Start's, where the Wii Menu has its "Wii Menu" one.
	SkinButton pageBtn(skin::pill, skin::pillOver, 4, kPairLeftX, 384, tr("Settings"),
		WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_X, PAD_TRIGGER_R, WIIDRC_BUTTON_Y);
	// The arrows at the screen's sides, level with the banner's middle.
	f32 safeX, safeW;
	Menu_SafeArea(&safeX, &safeW);
	const int wide = safeX < 0 ? static_cast<int>(-safeX) : 0;
	SkinButton prevBtn(skin::arrowLeft, skin::arrowLeftOver, 2, 10 - wide, 155, nullptr,
		WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT | WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, PAD_BUTTON_LEFT,
		WIIDRC_BUTTON_LEFT | WIIDRC_BUTTON_MINUS);
	SkinButton nextBtn(skin::arrowRight, skin::arrowRightOver, 2, 586 + wide, 155, nullptr,
		WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT | WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_RIGHT,
		WIIDRC_BUTTON_RIGHT | WIIDRC_BUTTON_PLUS);
	// Favourite: a small round button in the top right corner (or 1), its
	// star filled while the game shown is one.
	SkinButton starBtn(skin::roundBtn, skin::roundBtnOver, 2, 560 + wide, 24, nullptr,
		WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y, WIIDRC_BUTTON_X, &skin::iconStar, 0.6f);
	GuiImage starOn(skin::iconStarOn.data, skin::iconStarOn.w, skin::iconStarOn.h);
	starOn.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	starOn.SetScale(0.8f);
	const auto showStar = [&] {
		const bool on = riftwii::wii::Settings().favorites.count(shownId) != 0;
		starBtn.button.SetIcon(on ? &starOn : &starBtn.icon);
	};
	showStar();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&view);
	if (count > 1) {
		w.Append(&prevBtn.button);
		w.Append(&nextBtn.button);
	}
	w.Append(&backBtn.button);
	w.Append(&pageBtn.button);
	w.Append(&goBtn.button);
	if (!shownId.empty()) w.Append(&starBtn.button);
	mainWindow->SetState(STATE::DISABLED);
	mainWindow->Append(&w);
	w.SetState(STATE::DEFAULT);
	ResumeGui();
	int choice = -1;  // 0 Start, 1 back, 2 the game's page
	while (choice < 0) {
		usleep(20000);
		if (soundIn >= 0 && soundIn-- == 0) {
			// No sound of its own: the menu's music comes back.
			riftwii::wii::BannerSoundStart(pendingSound);
			std::vector<std::uint8_t>().swap(pendingSound);
		}
		riftwii::wii::BannerSoundUpdate();
		HaltGui();
		ClearStaleButtons({&backBtn.button, &goBtn.button, &pageBtn.button, &prevBtn.button, &nextBtn.button, &starBtn.button});
		// The arrows first: A pointed at one also fires Start (its
		// trigger is A anywhere), which must not open the game then.
		if (prevBtn.Clicked() || nextBtn.Clicked()) {
			const int dir = nextBtn.Clicked() ? 1 : -1;
			prevBtn.button.ResetState();
			nextBtn.button.ResetState();
			goBtn.button.ResetState();
			// The next game round that has a banner; the banner slides to it
			// (Home's page turn, inside the banner only), the buttons stay.
			// The menu's music stays paused in between: it came back for
			// the moment the next banner took to load (a tester).
			riftwii::wii::BannerSoundStop(false);
			f32 vx, vy, vw, vh;
			Menu_VisibleArea(&vx, &vy, &vw, &vh);
			const transition::Kind turn = dir > 0 ? transition::Kind::PageForward : transition::Kind::PageBack;
			const transition::Rect area{vx, vy, vw, kChannelBarTop - vy};
			// A banner not on the card yet is read from the game's image,
			// seconds on USB or RVZ: the GUI keeps running meanwhile (the
			// pointer moving, this banner held), and the page turns once
			// the next one is up.
			transition::Hold(turn, area);
			for (int step = 1; step < count; ++step) {
				const int i = ((index + dir * step) % count + count) % count;
				std::vector<std::uint8_t> bytes;
				const u64 t0 = gettime();
				ResumeGui();
				const bool read = load(i, bytes);
				HaltGui();
				soundDelay = 12;
				if (read && show(i, bytes, t0, gettime())) break;
			}
			transition::Begin(turn, area);
			showStar();
		} else if (starBtn.Clicked()) {
			starBtn.button.ResetState();
			goBtn.button.ResetState();  // A on the star fires Start too
			std::set<std::string>& favorites = riftwii::wii::Settings().favorites;
			const bool on = favorites.count(shownId) == 0;
			if (on) favorites.insert(shownId);
			else favorites.erase(shownId);
			riftwii::wii::SaveSettings();
			g_favoritesChanged = true;
			logf("Favourite %s: %s\n", shownId.c_str(), on ? "on" : "off");
			showStar();
		} else if (backBtn.Clicked()) choice = 1;
		else if (pageBtn.Clicked()) {
			goBtn.button.ResetState();  // A on Settings fires Start too
			choice = 2;
		} else if (goBtn.Clicked()) {
			// While pointing, a Remote's A starts the game only on Start
			// itself, as on the Wii Menu: A that misses an arrow does
			// nothing. A (or Start) on a Classic Controller, a GameCube
			// controller or the GamePad points at nothing, so it always does.
			bool on = false, known = false;
			for (int i = 0; i < 4; ++i) {
				const WPADData* p = userInput[i].wpad;
				const u32 wii = p ? (p->btns_d | p->btns_h) : 0;
				const bool otherA = (wii & WPAD_CLASSIC_BUTTON_A) ||
					((userInput[i].pad.btns_d | userInput[i].pad.btns_h) & (PAD_BUTTON_A | PAD_BUTTON_START)) ||
					((userInput[i].wiidrcdata.btns_d | userInput[i].wiidrcdata.btns_h) & WIIDRC_BUTTON_A);
				if (otherA) {
					known = on = true;
				} else if (wii & WPAD_BUTTON_A) {
					known = true;
					on = on || !p->ir.valid ||
						goBtn.button.IsInside(static_cast<int>(p->ir.x), static_cast<int>(p->ir.y));
				}
			}
			// The press already let go: as before, any pointer on Start.
			if (!known) {
				on = !AnyPointerLive();
				for (int i = 0; i < 4 && !on; ++i) {
					const WPADData* p = userInput[i].wpad;
					on = p && p->ir.valid && goBtn.button.IsInside(static_cast<int>(p->ir.x), static_cast<int>(p->ir.y));
				}
			}
			if (on) choice = 0;
			else goBtn.button.ResetState();
		}
		if (choice < 0) ResumeGui();
	}
	// Start: the banner's sound plays on with it (DropLingerBanner stops it).
	if (choice != 0) riftwii::wii::BannerSoundStop();
	mainWindow->Remove(&w);
	mainWindow->SetState(STATE::DEFAULT);
	logShown();
	if (choice == 0) {
		// Start: the banner plays on by itself, its buttons gone.
		w.Remove(&view);
		// One left from before goes quietly: the sound playing is this one's.
		if (g_lingerBanner) mainWindow->Remove(g_lingerBanner.get());
		view.tendSound = true;
		mainWindow->Append(&view);
		g_lingerBanner = std::move(owned);
	}
	return choice == 0 ? ChannelChoice::Start : choice == 2 ? ChannelChoice::Page : ChannelChoice::Back;
}

void SetHomeNotice(const std::string& text)
{
	g_homeNotice = text;
	g_homeNoticeShown = false;
}

static bool PacksOnUsb(const FrontendState& state)
{
	for (const auto& p : state.model.selections())
		if (p.xml_sd_path.compare(0, 5, "usb:/") == 0) return true;
	return false;
}

// Shown once before a launch that uses what is still experimental: an
// RVZ game (the catalog's note), packs on the USB drive. What cannot work
// at all is refused instead (ModPlaceProblem).
// A pack that swaps the game's executable for a Homebrew Channel app
// (CTGP Revolution 1.03's CTGP-R Channel): started as the Homebrew Channel
// starts it (wii/launch/modplan.hpp), but it does not work from RiftWii yet (a
// black screen from a disc, a green one from USB, on testers' consoles).
static bool HomebrewAppPackOn(const FrontendState& state)
{
	if (state.game_id.empty()) return false;
	std::string app, dropped;
	return riftwii::wii::homebrew_app_stand_in(state.model.selections(), state.game_id, app, dropped);
}

static bool CursedGame(const FrontendState& state);
static bool CursedEngineGame(const FrontendState& state);

static std::string LaunchNoteTitle(const FrontendState& state)
{
	if (CursedGame(state)) return tr("This game is cursed");
	if (CursedEngineGame(state)) return tr("This game may be cursed");
	return tr("Before you play");
}

static const char* const kHomebrewAppWarning =
	"CTGP Revolution (a pack that starts a Homebrew Channel app) does not work from RiftWii yet: it stops on a black or green screen. Start CTGP from the Homebrew Channel instead.";

// Cars 2 (SCY?4Q): testers saw it go black with the GameCube adapter in
// game, and again with screenshots on; it seems to refuse any change to it.
// Toy Story 3 (STS?4Q) and Disney Infinity (SQI?4Q) share its engine: not
// seen failing yet, so their note says they may do the same.
static bool GameIdIs(const FrontendState& state, const char* three)
{
	return state.game_id.size() >= 6 && state.game_id.compare(0, 3, three) == 0 &&
	       state.game_id.compare(4, 2, "4Q") == 0;
}

static bool CursedGame(const FrontendState& state)
{
	return GameIdIs(state, "SCY");
}

static bool CursedEngineGame(const FrontendState& state)
{
	return GameIdIs(state, "STS") || GameIdIs(state, "SQI");
}

static std::string LaunchNote(const FrontendState& state)
{
	std::string note = state.launch_warning;
	if (CursedGame(state)) {
		if (!note.empty()) note += " ";
		note += tr("Cars 2 is cursed: it goes to a black screen if RiftWii changes anything in it. Turn off the GameCube adapter (Settings), screenshots, cheats and mods for it. If it still won't boot, send a problem report.");
	} else if (CursedEngineGame(state)) {
		if (!note.empty()) note += " ";
		note += tr("This game may be cursed: it runs on Cars 2's engine, which goes to a black screen if RiftWii changes anything in it. If it won't boot, turn off the GameCube adapter (Settings), screenshots, cheats and mods for it, and send a problem report.");
	}
	if (HomebrewAppPackOn(state)) {
		if (!note.empty()) note += " ";
		note += tr(kHomebrewAppWarning);
	}
	// An RVZ's reader keeps a block as stored and as decoded, and the
	// decoder's 48 KB, in the game's MEM2: about 300 KB with Dolphin's
	// default 128 KiB blocks, 112 KB with 32 KiB ones.
	{
		const riftwii::wii::LaunchSource source = riftwii::wii::SelectedSource(state);
		const std::uint32_t block_kib = source.game.rvz_chunk_bytes / 1024;
		if (source.kind != riftwii::wii::LaunchSource::Kind::Disc &&
		    source.game.format == riftwii::UsbImageFormat::Rvz && block_kib > 32 &&
		    riftwii::game_needs_most_of_mem2(state.game_id)) {
			if (!note.empty()) note += " ";
			note += tr("This game needs nearly all of the Wii's memory, and this RVZ's {1} KiB blocks take about {2} KB of it in game: Newer Super Mario Bros. Wii crashed that way. If it crashes, convert the RVZ again in Dolphin with a block size of 32 KiB.",
			           {std::to_string(block_kib), std::to_string(2 * block_kib + 48)});
		}
	}
	if (PacksOnUsb(state)) {
		if (!note.empty()) note += " ";
		note += tr("Packs on USB are experimental; if it fails, copy them to SD.");
	}
	// A game image runs under d2x, whose USB reaches only a Wii U's rear
	// ports in game (the menu's IOS 58 reaches all four). Which port the
	// adapter is in cannot be told from IOS's device ID yet, so any
	// adapter the menu is using gets the note.
	if ((state.use_usb || state.use_sd) && riftwii::wii::GcAdapterRunning() &&
	    riftwii::wii::Settings().gc_adapter != "off" && riftwii::wii::is_wii_u()) {
		if (!note.empty()) note += " ";
		note += tr("On a Wii U the GameCube adapter may not work in game from the front USB ports; the rear ones work.");
	}
	return note;
}

// Home's count and button line on the Wii Menu's bar, which otherwise holds
// the date alone: shown until the player has used 1 (the view) or 2
// (Settings) once, so a first-time player learns them and the look stays
// clean after. Kept in settings.txt as home_hints = learned.
static bool HomeHintsLearned()
{
	const auto& other = riftwii::wii::Settings().other;
	const auto it = other.find("home_hints");
	return it != other.end() && it->second == "learned";
}
static void LearnHomeHints()
{
	if (HomeHintsLearned()) return;
	riftwii::wii::Settings().other["home_hints"] = "learned";
	riftwii::wii::SaveSettings();
}

static std::string HomeStatusLines(const FrontendState& state, const std::vector<GridItem>& items);

// Home's line under the clock. The Wii Menu's bar has room for one line
// inside a TV's overscan: two become one there; and the button hints show
// until 1 or 2 has been used, the game count after.
static std::string HomeStatus(const FrontendState& state, const std::vector<GridItem>& items)
{
	std::string text = HomeStatusLines(state, items);
	if (skin::BarBump()) return text;
	const std::size_t newline = text.find('\n');
	if (newline == std::string::npos) return text;
	const std::string first = text.substr(0, newline), second = text.substr(newline + 1);
	if (g_search.empty() && g_homeNotice.empty()) return HomeHintsLearned() ? first : second;
	return first + "   " + second;
}

static std::string HomeStatusLines(const FrontendState& state, const std::vector<GridItem>& items)
{
	if (!g_homeNotice.empty()) return g_homeNotice;
	// The games shown: the disc drive's tile, when it is there, is not one,
	// and a game's tiles (its own and its profiles') are one game. A view
	// showing only a game's profiles is not empty.
	std::set<std::string> shownGames;
	for (const GridItem& item : items)
		if (item.badge != "DISC") shownGames.insert(item.id);
	const std::size_t games = shownGames.size();
	std::string status;
	for (const std::string& p : {ShortSourceProblem("SD", state.sd_catalog), ShortSourceProblem("USB", state.usb_catalog)}) {
		if (p.empty()) continue;
		status += (status.empty() ? "" : "   ") + p;
	}
	if (!state.usb_catalog.cios_note.empty() || !state.sd_catalog.cios_note.empty())
		status += std::string(status.empty() ? "" : "   ") + "No d2x cIOS in 249-251: games cannot boot yet";
	if (!status.empty()) return status;
	if (!g_search.empty()) {
		if (games == 0) return tr("No game matches \"{1}\". Press 1 for all games.", {g_search});
		const std::string count = games == 1 ? std::string(tr("1 game")) : tr("{1} games", {std::to_string(games)});
		return tr("Search \"{1}\"", {g_search}) + ": " + count + "\n" + tr("1: all games");
	}
	if (g_filter == Filter::Mods && games == 0) return "No game here has packs in sd:/riivolution yet. Press 1 for all games.";
	if (g_filter == Filter::Recent && games == 0) return "No game on these drives was played from RiftWii yet. Press 1 for all games.";
	if (g_filter == Filter::Favorites && games == 0)
		return tr("No favourite is on these drives. Mark games on their page. Press 1 for all games.");
	if (games == 0) return "No games found (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)";
	const std::string count = games == 1 ? std::string(tr("1 game")) : tr("{1} games", {std::to_string(games)});
	// What B does here: letters in A to Z order (GuiGameGrid::LetterKeys),
	// and with A it picks a game up where games can be moved.
	const bool letters = riftwii::wii::Settings().home_sort == "az";
	const bool moving = HomeCanMove();
	const char* keys = letters && moving ? tr("1: view  2: settings  B: A-Z  B+A: move")
		: letters ? tr("1: view   2: settings   -/+: pages   B: A to Z")
		: moving ? tr("1: view   2: settings   -/+: pages   B+A: move")
		: tr("1: view   2: settings   -/+: pages");
	return std::string(tr(FilterLabel(g_filter))) + ": " + count + "\n" + keys;
}

class Panel : public GuiElement {
public:
	// `extra` widens it that much on each side: its ends drawn as they
	// are, the middle stretched (a widescreen popup).
	Panel(const skin::Tex& tex, int x, int y, int extra = 0) : tex(tex), x(x), y(y), extra(extra) {}
	void Draw() override
	{
		if (extra <= 0 || !tex.data) {
			skin::Draw(tex, x - 4.0f, y - 4.0f);
			return;
		}
		const f32 end = 48;  // the corner, its shadow and a little more
		const f32 left = x - 4.0f - extra, w = tex.w + 2.0f * extra, h = tex.h, top = y - 4.0f;
		const f32 u = end / tex.w;
		const u16 tw = static_cast<u16>(tex.w), th = static_cast<u16>(tex.h);
		Menu_DrawImgPart(left, top, end, h, tw, th, tex.data, 0, 0, u, 1, 255);
		Menu_DrawImgPart(left + end, top, w - 2 * end, h, tw, th, tex.data, u, 0, 1 - u, 1, 255);
		Menu_DrawImgPart(left + w - end, top, end, h, tw, th, tex.data, 1 - u, 0, 1, 1, 255);
	}
private:
	const skin::Tex& tex;
	int x, y, extra;
};

// The help under a page's list, in the same place on every page (inside a
// TV's overscan, over the prompts): plain text after a faint info sign, as
// a settings list's footer, never framed like something to press.
class NoteCard : public GuiElement {
public:
	static constexpr int kX = 40, kY = 316, kW = 560, kH = 86;  // four lines end by y 410, above the prompts (y 418)
	static constexpr int kTextX = kX + 36, kTextY = kY + 10, kTextW = kW - 40, kLines = 4;
	void Draw() override {
		skin::Draw(skin::iconInfo, kX + 2, kY + 7, GetAlpha() / 2);
	}
};

// The icon left of a page's title: the one on the tile that opened it.
class PageIcon : public GuiElement {
public:
	explicit PageIcon(const skin::Tex* icon) : icon(icon) {}
	void Draw() override {
		if (icon && icon->data) skin::Draw(*icon, 40, 32, GetAlpha());
	}
private:
	const skin::Tex* icon;
};

// How much wider a popup (and the launch screen's card) is on each side:
// on a widescreen menu, up to 80 of the room the TV has past the 4:3
// middle; none on a 4:3 one.
int PopupExtra()
{
	f32 safeX, safeW;
	Menu_SafeArea(&safeX, &safeW);
	const int room = safeX < 0 ? static_cast<int>(-safeX) - 24 : 0;
	return room <= 0 ? 0 : room < 80 ? room : 80;
}

// A box over the current page: a title, some text and up to two buttons
// (A for the first, B or HOME for the second). The page underneath takes
// no input while it is up. Built and closed with the GUI halted.
class Dim : public GuiElement {
public:
	void Draw() override { Menu_FillWholeScreen((GXColor){0, 0, 0, 185}); }
};

// A window that comes in from a little smaller (or from above or below)
// the first time it is drawn: a popup pops, the HOME Menu's bands slide
// in. What it holds keeps its own places; only the camera moves.
class ShiftWindow : public GuiWindow {
public:
	ShiftWindow(float fromScale, float fromY, bool overshoot, float ms = 260)
		: GuiWindow(screenwidth, screenheight), fromScale(fromScale), fromY(fromY), overshoot(overshoot), ms(ms) {}
	void Draw() override
	{
		if (start == 0) start = gettime();
		const float t = ticks_to_microsecs(gettime() - start) / 1000.0f / (ms * transition::SlowMotion());
		if (t >= 1) {
			GuiWindow::Draw();
			return;
		}
		const float e = overshoot ? transition::EaseBack(t) : transition::EaseOut(t);
		Menu_PushCamera(fromScale + (1 - fromScale) * e, 320, 240, 0, fromY * (1 - e));
		GuiWindow::Draw();
		Menu_PopCamera();
	}
private:
	float fromScale, fromY;
	bool overshoot;
	float ms;
	u64 start = 0;
};

// The box behind a hover name, so it reads over covers: a rounded card
// with a soft shadow in the theme's colours (skin::HintBox), sized to the
// text and fading with it. `anchorX` is the text's centre (centred) or
// its right end, `top` the text's top, both on screen.
class HintChip : public GuiElement {
public:
	HintChip(GuiText& text, int anchorX, int top, bool centred)
		: text_(text), anchorX_(anchorX), top_(top), centred_(centred) {}
	void Draw() override {
		if (!text_.IsVisible() || text_.GetAlpha() <= 0) return;
		const int w = (text_.GetTextWidth() + 24 + 3) & ~3, h = 28;
		const skin::Tex box = skin::HintBox(w, h);
		if (box.data == nullptr) return;
		int x = centred_ ? anchorX_ - w / 2 : anchorX_ - w + 12;
		if (centred_) {
			// A long name by a corner button stays on the screen (8 in from
			// its edges, a widescreen menu's too), and its text with it.
			f32 safeX, safeW;
			Menu_SafeArea(&safeX, &safeW);
			const int left = static_cast<int>(safeX) + 8, right = static_cast<int>(safeX + safeW) - 8;
			x = std::max(left, std::min(x, right - w));
			text_.SetPosition(x + w / 2 - 320, top_);
		}
		Menu_DrawImg(x - riftwii::kHintBoxMargin, top_ - 5 - riftwii::kHintBoxMargin, box.w, box.h, box.data, 0, 1, 1,
			static_cast<u8>(text_.GetAlpha()));
	}

private:
	GuiText& text_;
	int anchorX_, top_;
	bool centred_;
};

// Home with no games on either drive: where to put them, on a card over
// the empty tiles of the second row.
class EmptyHomeNote : public GuiElement {
public:
	static constexpr int kW = 440, kH = 146, kTop = 136;
	EmptyHomeNote()
		: words(std::string(tr("No games found yet. Put your games (WBFS, ISO or RVZ) in a folder named wbfs or games at the top of the SD card or USB drive, then pick Look for games again in Settings.")) +
			" " + tr("A GameCube game goes on the SD card, as sd:/games/<its ID>/game.iso.")),
		  text(words.c_str(), 16, skin::kInk) {
		// Without a parent, x is where the centre goes on the screen.
		text.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		text.SetWrap(true, kW - 40, 6);
	}
	void Draw() override {
		if (!IsVisible()) return;
		const skin::Tex box = skin::HintBox(kW, kH);
		if (box.data)
			Menu_DrawImg(320 - kW / 2 - riftwii::kHintBoxMargin, kTop - riftwii::kHintBoxMargin, box.w, box.h, box.data, 0, 1,
				1, 255);
		text.SetPosition(320, kTop + 12);
		text.Draw();
	}

private:
	std::string words;  // first: the text is made from it
	GuiText text;
};

// A page's line of help or status above the prompts along the bottom. The
// lines that reach down to the prompts' cards are cut short of the leftmost
// card (measured, so a longer translation or a third prompt moves it),
// so the text never runs under them; the lines above them keep the full
// width. Past the last line the text ends in "..." as GuiText's own wrap
// does. `x`, `y`: the top left of the first line on the screen.
class PromptsNote : public GuiElement {
public:
	PromptsNote(GuiPrompts& prompts, int x, int y, int fullWidth, int maxLines, int fontSize, GXColor color)
		: prompts_(prompts), x_(x), y_(y), fullWidth_(fullWidth), fontSize_(fontSize), meter_("", fontSize, color) {
		for (int i = 0; i < maxLines; ++i) {
			lines_.emplace_back(new GuiText("", fontSize, color));
			lines_.back()->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		}
	}
	void SetText(const std::string& text) {
		text_ = text;
		laidOutFor_ = -1;  // laid out again at the next draw
	}
	void Draw() override {
		if (!IsVisible()) return;
		riftwii::wii::DrawTrace trace("the status line");
		const int pillsLeft = prompts_.LeftEdge();
		if (pillsLeft != laidOutFor_) Layout(pillsLeft);
		for (std::size_t i = 0; i < lines_.size(); ++i) {
			if (shown_[i].empty()) continue;
			lines_[i]->SetAlpha(GetAlpha());
			lines_[i]->SetPosition(x_, y_ + static_cast<int>(i) * LineHeight());
			lines_[i]->Draw();
		}
	}

private:
	int LineHeight() const { return fontSize_ + 6; }  // GuiText's own spacing

	// The width line `index` may use: the whole of it, or what is left of
	// the leftmost card when its text would reach the cards' top.
	int WidthOfLine(int index, int pillsLeft) const {
		const int textBottom = y_ + index * LineHeight() + fontSize_;
		if (textBottom <= GuiPrompts::CardTop()) return fullWidth_;
		const int clearOfShadow = 16;
		return std::max(60, std::min(fullWidth_, pillsLeft - clearOfShadow - x_));
	}

	// Byte offsets in `s` where a line may end: before a space (the space is
	// dropped), after a CJK character (Japanese has no spaces), and at a
	// newline.
	static std::vector<std::size_t> BreakPoints(const std::string& s) {
		std::vector<std::size_t> points;
		std::size_t i = 0;
		while (i < s.size()) {
			const unsigned char lead = static_cast<unsigned char>(s[i]);
			const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
			const std::size_t next = std::min(s.size(), i + length);
			if (lead == ' ' || lead == '\n') points.push_back(i);
			if (lead == '\n') break;
			const bool cjk = length == 3 && i + 1 < s.size() && (((lead & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6)) >= 0x2E80;
			if (cjk) points.push_back(next);
			i = next;
		}
		return points;
	}

	int TextWidth(const std::string& s) {
		meter_.SetText(s.c_str());
		return meter_.GetTextWidth();
	}

	void Layout(int pillsLeft) {
		laidOutFor_ = pillsLeft;
		shown_.assign(lines_.size(), std::string());
		std::string rest = text_;
		for (std::size_t i = 0; i < lines_.size() && !rest.empty(); ++i) {
			const int width = WidthOfLine(static_cast<int>(i), pillsLeft);
			std::string line = rest;
			const bool lastLine = i + 1 == lines_.size();
			if (!lastLine) {
				// The longest start of the rest that fits this line.
				std::size_t cut = std::string::npos;
				for (const std::size_t point : BreakPoints(rest)) {
					if (point == 0) continue;
					if (cut != std::string::npos && TextWidth(rest.substr(0, point)) > width) break;
					cut = point;
				}
				const bool wholeFits = rest.find('\n') == std::string::npos && TextWidth(rest) <= width;
				if (!wholeFits && cut != std::string::npos) {
					line = rest.substr(0, cut);
					std::size_t skip = cut;
					if (skip < rest.size() && (rest[skip] == ' ' || rest[skip] == '\n')) ++skip;
					rest = rest.substr(skip);
				} else {
					// All of it fits, or it is one word too long for the line,
					// which GuiText ends in "...".
					rest.clear();
				}
			} else {
				rest.clear();
			}
			shown_[i] = line;
			lines_[i]->SetText(line.c_str());
			lines_[i]->SetWrap(true, width, 1);
		}
	}

	GuiPrompts& prompts_;
	int x_, y_, fullWidth_, fontSize_;
	GuiText meter_;
	std::vector<std::unique_ptr<GuiText>> lines_;
	std::vector<std::string> shown_;
	std::string text_;
	int laidOutFor_ = -1;
};

// A QR code on screen: a white quiet zone of two modules, then the dark
// modules, a run of them on a row as one rectangle.
class QrImage : public GuiElement {
public:
	QrImage(const riftwii::QrCode& code, int x, int y, int module) : code(code), x(x), y(y), module(module) {}
	static int Side(const riftwii::QrCode& code, int module) { return (code.size + 4) * module; }
	void Draw() override
	{
		if (code.size == 0) return;
		const int quiet = 2 * module;
		Menu_DrawRectangle(x, y, Side(code, module), Side(code, module), skin::kWhite, 1);
		for (int r = 0; r < code.size; ++r) {
			for (int c = 0; c < code.size;) {
				if (!code.at(c, r)) { ++c; continue; }
				int end = c;
				while (end < code.size && code.at(end, r)) ++end;
				Menu_DrawRectangle(x + quiet + c * module, y + quiet + r * module, (end - c) * module, module,
					(GXColor){0, 0, 0, 255}, 1);
				c = end;
			}
		}
	}
private:
	riftwii::QrCode code;
	int x, y, module;
};

// When the last popup closed: one opening straight after it (the
// tutorial's next page, a note after What's new) takes its place at once,
// as the Wii's message box changes its text, instead of popping over the
// old one's fading picture (both boxes' text and buttons showed at once).
static u64 g_popupClosedAt = 0;
static bool PopupJustClosed()
{
	return g_popupClosedAt != 0 && diff_msec(g_popupClosedAt, gettime()) < 400;
}

// The popup, as the Wii Menu's message box (skin::popup): its title in the
// darker band across the top, the text below it (centred, as the Wii's,
// unless it is a list or sits beside something), the buttons above the
// band at the bottom, the page darkened behind it.
class PopupBox {
public:
	static constexpr int kTitleY = 110, kBodyY = 158, kButtonY = 304;
	// The body ends a little above the buttons: at 16 points a line is 22.
	static constexpr int kBodyLines = (kButtonY - 10 - kBodyY) / 22;
	PopupBox(const std::string& title, const std::string& body, const std::string& ok = "",
		 const std::string& cancel = "")
		: extra(PopupExtra()), panel(skin::popup, 34, 102, extra), titleTxt(title.c_str(), 24, skin::kInk),
		  bodyTxt(body.c_str(), 16, skin::kInkSoft),
		  okBtn(skin::pill, skin::pillOver, 4, cancel.empty() ? 198 : kPairLeftX, kButtonY, ok.c_str(),
			WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, WIIDRC_BUTTON_A),
		  cancelBtn(skin::pill, skin::pillOver, 4, kPairRightX, kButtonY, cancel.c_str(),
			WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B | WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, PAD_BUTTON_B,
			WIIDRC_BUTTON_B | WIIDRC_BUTTON_HOME),
		  hasOk(!ok.empty()), hasCancel(!cancel.empty()), chained(PopupJustClosed()),
		  w(chained ? 1.0f : 0.86f, 0, !chained)
	{
		Place(titleTxt, 0, kTitleY, true);
		titleTxt.SetMaxWidth(528 + 2 * extra);
		// A list (lines of its own) reads better from the left.
		PlaceBody(body.find('\n') == std::string::npos);
		bodyTxt.SetWrap(true, 528 + 2 * extra, kBodyLines);
		w.Append(&dim);
		w.Append(&panel);
		w.Append(&titleTxt);
		w.Append(&bodyTxt);
		if (hasOk) w.Append(&okBtn.button);
		if (hasCancel) w.Append(&cancelBtn.button);
		mainWindow->SetState(STATE::DISABLED);
		transition::Begin(chained ? transition::Kind::Cut : transition::Kind::PopOpen);
		mainWindow->Append(&w);
		w.SetState(STATE::DEFAULT);
	}
	~PopupBox()
	{
		transition::Begin(transition::Kind::PopClose);
		mainWindow->Remove(&w);
		mainWindow->SetState(STATE::DEFAULT);
		g_popupClosedAt = gettime();
	}
	void SetBody(const std::string& body) { bodyTxt.SetText(body.c_str()); }
	// Something more in the box, over its whole width (it must outlive the box).
	void Append(GuiElement* e) { w.Append(e); }
	// The text centred across the top of the room, in at most `lines`
	// lines, for a box with something of its own below it (the code mod's
	// two choices): centred in the middle, it ran under them.
	void BodyAtTop(int lines)
	{
		Place(bodyTxt, 0, kBodyY, true);
		bodyTxt.SetWrap(true, 528 + 2 * extra, lines);
	}
	// The second button alone, in the middle (as the first is when alone).
	void CenterCancel() { cancelBtn.button.SetPosition(198 - 4, kButtonY - 4); }
	// How much wider than 4:3 the box is on each side (PopupExtra).
	int Extra() const { return extra; }
	// After another box was opened over this one and closed: the page
	// underneath takes no input again, this box does.
	void Reactivate()
	{
		mainWindow->SetState(STATE::DISABLED);
		w.SetState(STATE::DEFAULT);
	}
	// While work goes on with the box up (the GUI halted): whether its
	// last button was pressed.
	bool Pressed()
	{
		ClearStaleButtons({&okBtn.button, &cancelBtn.button});
		return hasCancel ? cancelBtn.Clicked() : hasOk && okBtn.Clicked();
	}
	// Something more drawn in the box (it must outlive the box), with the
	// text kept to `width` beside it.
	void Add(GuiElement* e, int width)
	{
		w.Append(e);
		PlaceBody(false);  // beside it: from the left
		bodyTxt.SetWrap(true, width + extra, kBodyLines);  // the box's right part stays where it was
	}
	// Until a button is pressed: 0 the first, 1 the second. A is the first
	// button's hotkey, so A with the pointer on the second clicks both in
	// one frame: the second is asked first, as only a deliberate press
	// (the pointer, B or HOME) clicks it.
	int Wait()
	{
		ResumeGui();
		int choice = -1;
		while (choice < 0) {
			usleep(20000);
			HaltGui();
			choice = Poll();
			if (choice < 0) ResumeGui();
		}
		return choice;
	}
	// For a box that does more while it waits (the GUI halted): Wait's
	// answer once a button was pressed, else -1.
	int Poll()
	{
		ClearStaleButtons({&okBtn.button, &cancelBtn.button});
		if (hasCancel && cancelBtn.Clicked()) return 1;
		if (hasOk && okBtn.Clicked()) return 0;
		return -1;
	}
	void SetTitle(const std::string& title) { titleTxt.SetText(title.c_str()); }
	// A title too long for the band at 24 points (a controller's name on
	// the GameCube controls) is drawn smaller, down to 18, rather than
	// cut short.
	void SetTitleFitted(const std::string& title)
	{
		const int room = 528 + 2 * extra;
		titleTxt.SetText(title.c_str());
		int size = 24;
		titleTxt.SetFontSize(size);
		while (size > 18 && titleTxt.GetTextWidth() > room) {
			size -= 2;
			titleTxt.SetFontSize(size);
		}
		titleTxt.SetMaxWidth(room);  // also drops the text drawn at the old size
	}
private:
	// Centred, it sits in the middle of the room between the title band
	// and the buttons, as the Wii's message does (libwiigui measures
	// MIDDLE from the screen's middle, y 240); a list starts at the top.
	void PlaceBody(bool centred)
	{
		if (centred) {
			bodyTxt.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
			bodyTxt.SetPosition(0, (kBodyY + kButtonY - 10) / 2 - 240);
		} else {
			Place(bodyTxt, 56 - extra, kBodyY);
		}
	}
	int extra;  // first: the panel and the texts use it
	Dim dim;
	Panel panel;
	GuiText titleTxt;
	GuiText bodyTxt;
	SkinButton okBtn;
	SkinButton cancelBtn;
	bool hasOk, hasCancel;
	bool chained;  // before w, which it sets up
	ShiftWindow w;
};

static int ShowPopup(const std::string& title, const std::string& body, const std::string& ok,
	const std::string& cancel = "")
{
	PopupBox box(title, body, ok, cancel);
	return box.Wait();
}

// A code mod is switched on for the next start.
static bool CodeModOn(const FrontendState& state)
{
	for (const riftwii::LaunchPackage& p : state.model.packages)
		if (p.enabled && p.code_build()) return true;
	return false;
}

// Start with a code mod on: with RiftWii's extras or without them, as the
// mod's own loader starts it. Mods differ: Project+ runs with the extras,
// REX went to a black screen on a tester's Wii U (2610-412) and starts
// from its own loader there. False: B, back to the game page.
static bool PickCodeModStart(FrontendState& state)
{
	state.code_build_plain = false;
	if (!CodeModOn(state)) return true;
	PopupBox box(tr("How should this code mod start?"),
		tr("Code mods differ. Most, like Project+, run well with RiftWii's extras. If a mod stays on a black screen, start it without them, as its own loader would."),
		"", tr("Back"));
	box.CenterCancel();
	// The text's three lines at most (from y 158, 22 apart) end at 224,
	// above the choices; the choices end above Back's pill (y 300).
	box.BodyAtTop(3);
	std::vector<TileSpec> tiles(2);
	tiles[0].x = 56;
	tiles[1].x = 332;
	for (TileSpec& t : tiles) {
		t.y = 230;
		t.w = 252;
		t.h = 62;
	}
	tiles[0].label = tr("With extras");
	tiles[0].value = tr("Crash reports, back to RiftWii");
	tiles[1].label = tr("Without extras");
	tiles[1].value = tr("As the mod's own loader");
	GuiTiles choices;
	choices.SetTiles(&tiles);
	choices.Select(0);
	box.Append(&choices);
	ResumeGui();
	int picked = -1;
	bool back = false;
	while (picked < 0 && !back) {
		usleep(20000);
		HaltGui();
		picked = choices.GetClicked();
		back = picked < 0 && box.Pressed();  // Back, B or HOME
		if (picked < 0 && !back) ResumeGui();
	}
	if (back) return false;
	state.code_build_plain = picked == 1;
	logf("Start: the code mod starts %s RiftWii's extras\n", state.code_build_plain ? "without" : "with");
	return true;
}

// Packs switched on with every option off change nothing, and the launch
// refuses them ("no patches selected"): asks to turn them off and play
// (a tester expected the game to start as it is). False: stay here.
static bool TurnOffEmptyPacks(FrontendState& state)
{
	const std::vector<std::size_t> empty = state.model.packs_with_nothing_picked();
	if (empty.empty()) return true;
	std::string names;
	for (std::size_t i : empty)
		names += (names.empty() ? "" : ", ") + PackLabel(state.model.packages[i]);
	if (ShowPopup(tr("Nothing picked in this mod"),
		    tr("{1} is switched on, but none of its options are picked, so it would change nothing. Turn it off and start the game?",
			    {FlatCapped(names, 80)}),
		    tr("Play"), tr("Cancel")) != 0)
		return false;
	for (std::size_t i : empty) state.model.set_enabled(i, false);
	logf("Start: %s switched off (nothing picked)\n", names.c_str());
	return true;
}

// How far a long job is, as a bar in a popup.
class ProgressBar : public GuiElement {
public:
	ProgressBar(int x, int y, int width) : x(x), y(y), width(width) {}
	void Set(double done) { fraction = done < 0 ? 0 : done > 1 ? 1 : done; }
	void Draw() override
	{
		Menu_DrawRectangle(x, y, width, 14, (GXColor){0, 0, 0, 40}, 1);
		const int filled = static_cast<int>(fraction * width);
		if (filled > 0) Menu_DrawRectangle(x, y, filled, 14, skin::kAccent, 1);
	}
private:
	int x, y, width;
	double fraction = 0;
};

// A game's small cover at (x, y), `w` x `h`: its cover when one is kept,
// else its colour with a disc on it. Drawn in a popup, over the page.
class SmallCover : public GuiElement {
public:
	SmallCover(std::string id, int x, int y, int w, int h) : id(std::move(id)), x(x), y(y), w(w), h(h) {}
	void Draw() override
	{
		skin::DrawNine(skin::card9, x - 4, y - 4, w + 8, h + 8, 12);
		if (const u8* cover = id.empty() ? nullptr : riftwii::wii::CoverTexture(id)) {
			skin::DrawRgb5a3Part(cover, riftwii::kCoverWidth, riftwii::kCoverHeight, 0.0f, 1.0f, x, y, w, h);
			return;
		}
		Menu_DrawRectangle(x, y, w, h, skin::HueFor(id), 1);
		skin::Draw(skin::iconDisc, x + (w - 40) / 2, y + (h - 40) / 2);
	}
private:
	std::string id;
	int x, y, w, h;
};

// One of the skin's pictures at (x, y), `scale`d, in a popup.
class SkinMark : public GuiElement {
public:
	SkinMark(const skin::Tex& tex, int x, int y, float scale) : tex(tex), x(x), y(y), scale(scale) {}
	void Draw() override
	{
		if (tex.data) skin::Draw(tex, x, y, 255, scale);
	}
private:
	const skin::Tex& tex;
	int x, y;
	float scale;
};

// A long job's progress in a popup box (making a GameCube game, an SD
// image): the game's cover on the left; beside it the share done, large,
// the bar under it, the time left under the bar and, last, what is being
// done now. The box's own text stays empty.
class ProgressPanel {
public:
	ProgressPanel(PopupBox& box, const std::string& coverId)
		: left(56 - box.Extra()), right(584 + box.Extra()),
		  cover(coverId, left, PopupBox::kBodyY, kCoverW, kCoverH), percentTxt("0%", 36, skin::kInk),
		  bar(left + kCoverW + 24, PopupBox::kBodyY + 46, right - (left + kCoverW + 24)), timeTxt("", 15, skin::kInkSoft),
		  stageTxt("", 15, skin::kInkSoft), start(gettime())
	{
		// From the body's top down; the stage's two lines end above the
		// buttons (PopupBox::kButtonY), which they touched at 4:3.
		const int textX = left + kCoverW + 24;
		Place(percentTxt, textX, PopupBox::kBodyY);
		Place(timeTxt, textX, PopupBox::kBodyY + 66);
		Place(stageTxt, textX, PopupBox::kBodyY + 90);
		stageTxt.SetWrap(true, right - textX, 2);
		box.Append(&cover);
		box.Append(&percentTxt);
		box.Append(&bar);
		box.Append(&timeTxt);
		box.Append(&stageTxt);
	}
	// `done` of `total`; the time left at the rate so far, once there is
	// one (ten seconds in), and what is left in bytes before that.
	void Set(std::uint64_t done, std::uint64_t total)
	{
		const double fraction = total ? static_cast<double>(std::min(done, total)) / static_cast<double>(total) : 0;
		bar.Set(fraction);
		percentTxt.SetText((std::to_string(static_cast<int>(fraction * 100)) + "%").c_str());
		const u64 ms = diff_msec(start, gettime());
		// Until there is a rate, a placeholder: an empty line looked unfinished.
		std::string time = tr("Working out the time left...");
		if (ms > 10000 && done > 0 && total > done) {
			const std::uint64_t secs = (total - done) * ms / done / 1000;
			time = secs >= 90 ? tr("{1} minutes left", {std::to_string((secs + 59) / 60)})
					 : tr("{1} seconds left", {std::to_string(secs)});
		}
		timeTxt.SetText(time.c_str());
	}
	void SetStage(const std::string& stage) { stageTxt.SetText(stage.c_str()); }
private:
	static constexpr int kCoverW = 80, kCoverH = 112;
	int left, right;
	SmallCover cover;
	GuiText percentTxt;
	ProgressBar bar;
	GuiText timeTxt;
	GuiText stageTxt;
	u64 start;
};

// Where the player leaves to, for wii/main.cpp's ExitApp: 1 the loader
// that started RiftWii (the Homebrew Channel), 2 the Wii Menu,
// 3 Priiloader, 4 power off.
static int g_leave = 1;

// The HOME Menu, as the Wii's: black bands across the top (its name and
// Close) and the bottom (the Remotes' batteries), each edged with a fine
// light line towards the middle, and the screen between them darkened
// with fine lines across it.
static constexpr int kHomeBandTop = 76, kHomeBandBottom = 392;
// A theme with home_menu = ios6 (Bookshelf): glossy bars in the bar's
// colour, light at the top and shaded down, as iOS 6's navigation bar
// and toolbar; dark linen between them.
static GXColor ScaledColor(GXColor c, float k)
{
	const auto ch = [k](u8 v) { return static_cast<u8>(std::min(255.0f, v * k)); };
	return (GXColor){ch(c.r), ch(c.g), ch(c.b), c.a};
}
static void DrawIosBar(int y0, int y1)
{
	const GXColor light = ScaledColor(skin::kBar, 1.04f), dark = ScaledColor(skin::kBar, 0.66f);
	for (int y = y0; y < y1; y += 2) {
		const float k = static_cast<float>(y - y0) / std::max(1, y1 - y0);
		// The top half lighter, with a hard step at the middle (the gloss).
		const float g = k < 0.5f ? k * 0.6f : 0.45f + (k - 0.5f) * 1.1f;
		const GXColor c = {static_cast<u8>(light.r + (dark.r - light.r) * g), static_cast<u8>(light.g + (dark.g - light.g) * g),
			static_cast<u8>(light.b + (dark.b - light.b) * g), 255};
		Menu_FillScreen(y, std::min(2, y1 - y), c);
	}
	Menu_FillScreen(y0, 1, (GXColor){255, 255, 255, 110});
}
class HomeBand : public GuiElement {
public:
	explicit HomeBand(bool bottom) : bottom(bottom) {}
	void Draw() override {
		if (skin::HomeIos6()) {
			if (bottom) {
				Menu_FillScreen(kHomeBandBottom - 4, 4, (GXColor){0, 0, 0, 50});
				Menu_FillScreen(kHomeBandBottom - 1, 1, (GXColor){0, 0, 0, 200});
				DrawIosBar(kHomeBandBottom, 480);
				Menu_FillScreen(480, 1000, ScaledColor(skin::kBar, 0.66f));
			} else {
				Menu_FillScreen(-1000, 1000, ScaledColor(skin::kBar, 1.04f));
				DrawIosBar(0, kHomeBandTop);
				Menu_FillScreen(kHomeBandTop - 1, 1, (GXColor){0, 0, 0, 200});
				Menu_FillScreen(kHomeBandTop, 5, (GXColor){0, 0, 0, 60});
			}
			return;
		}
		const GXColor body = {0, 0, 0, 255}, edge = {200, 200, 200, 255};
		if (bottom) {
			Menu_FillScreen(kHomeBandBottom, 1000, body);
			Menu_FillScreen(kHomeBandBottom, 2, edge);
		} else {
			Menu_FillScreen(-1000, 1000 + kHomeBandTop, body);  // from the screen's top, whatever its size
			Menu_FillScreen(kHomeBandTop - 2, 2, edge);
		}
	}
private:
	bool bottom;
};
class HomeDim : public GuiElement {
public:
	void Draw() override {
		if (skin::HomeIos6() && skin::linen.data) {
			Menu_FillWholeScreen((GXColor){0, 0, 0, 110});
			f32 vx, vy, vw, vh;
			Menu_VisibleArea(&vx, &vy, &vw, &vh);
			const skin::Tex& t = skin::linen;
			for (int y = kHomeBandTop; y < kHomeBandBottom; y += t.h) {
				const int h = std::min(t.h, kHomeBandBottom - y);
				for (float x = vx; x < vx + vw; x += t.w)
					Menu_DrawImgPart(x, y, t.w, h, static_cast<u16>(t.w), static_cast<u16>(t.h), t.data, 0, 0, 1,
						static_cast<float>(h) / t.h, 255);
			}
			// The bars' shade falling on it.
			for (int i = 0; i < 6; ++i) {
				Menu_FillScreen(kHomeBandTop + i * 2, 2, (GXColor){0, 0, 0, static_cast<u8>(70 - i * 11)});
				Menu_FillScreen(kHomeBandBottom - 2 - i * 2, 2, (GXColor){0, 0, 0, static_cast<u8>(60 - i * 9)});
			}
			return;
		}
		Menu_FillWholeScreen((GXColor){0, 0, 0, 200});
		for (int y = kHomeBandTop; y < kHomeBandBottom; y += 5) Menu_FillScreen(y, 2, (GXColor){255, 255, 255, 14});
	}
};

// The Wii Remotes' batteries, as the Wii's own HOME Menu shows them: P1
// to P4 in a capsule across the bottom band's edge, each with a battery
// that fills in four bars (empty and dim for a Remote not connected).
class HomeBatteries : public GuiElement {
public:
	HomeBatteries() {
		for (int i = 0; i < 4; ++i) {
			const std::string name = "P" + std::to_string(i + 1);
			label[i] = new GuiText(name.c_str(), 22, skin::kWhite);
			label[i]->SetParent(this);
			label[i]->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		}
	}
	~HomeBatteries() override {
		for (GuiText* l : label) delete l;
	}
	void Draw() override {
		const int segW = 130, h = 36, left = 320 - 2 * segW, top = kHomeBandBottom - h / 2;
		skin::DrawNine(skin::capsule9, left - 4, top - 4, 4 * segW + 8, h + 8, riftwii::kCapsuleCorner);
		for (int i = 0; i < 4; ++i) {
			u32 type = 0;
			const bool on = WPAD_Probe(i, &type) == WPAD_ERR_NONE;
			const int x = left + i * segW;
			if (i > 0) Menu_DrawRectangle(x, top + 2, 2, h - 4, (GXColor){200, 200, 200, 255}, 1);
			label[i]->SetColor(on ? skin::kWhite : (GXColor){120, 120, 120, 255});
			label[i]->SetPosition(x + 22, top + 5);
			label[i]->Draw();
			// The battery: an outline and its nub, filled bar by bar. The
			// status report's level follows the batteries' voltage: the
			// Wii Menu's bars step at about 14, 40, 66 and 92 (Dolphin's fit
			// of it, charge = level * 2.46 / 255 - 0.013).
			const int bx = x + 62, by = top + 10;
			const GXColor line = on ? (GXColor){220, 220, 220, 255} : (GXColor){110, 110, 110, 255};
			// The outline is four filled strips, not GX lines: a line lands
			// on the pixel edge and rounds a pixel either way, which left the
			// bars nearer one side. Inside it is 40 x 12.
			const int bodyW = 44, bodyH = 16, edge = 2;
			Menu_DrawRectangle(bx, by, bodyW, edge, line, 1);
			Menu_DrawRectangle(bx, by + bodyH - edge, bodyW, edge, line, 1);
			Menu_DrawRectangle(bx, by, edge, bodyH, line, 1);
			Menu_DrawRectangle(bx + bodyW - edge, by, edge, bodyH, line, 1);
			Menu_DrawRectangle(bx + bodyW, by + 4, 4, 8, line, 1);
			if (!on) continue;
			const int level = WPAD_BatteryLevel(i);
			const int bars = level >= 92 ? 4 : level >= 66 ? 3 : level >= 40 ? 2 : level >= 14 ? 1 : 0;
			// Four 7 x 6 bars 2 apart: 34 wide, so 3 pixels of space on
			// every side of the inside.
			const int barW = 7, barH = 6, gap = 2;
			const int barsX = bx + edge + (bodyW - 2 * edge - (4 * barW + 3 * gap)) / 2;
			const int barsY = by + edge + (bodyH - 2 * edge - barH) / 2;
			for (int k = 0; k < bars; ++k)
				Menu_DrawRectangle(barsX + k * (barW + gap), barsY, barW, barH,
					bars == 1 ? (GXColor){222, 72, 72, 255} : skin::kAccent, 1);
		}
	}
private:
	GuiText* label[4];
};

// Without a pointer, the D-pad moves between the HOME Menu's buttons
// (two columns, Close above them in the top band) and A presses the one
// lit.
class HomeFocus : public GuiElement {
public:
	HomeFocus(GuiButton* const (&b)[5]) {
		for (int i = 0; i < 5; ++i) buttons[i] = b[i];
	}
	void Draw() override {}
	void Update(GuiTrigger* t) override {
		if (!t || AnyPointerLive()) {
			shown = -1;
			return;
		}
		int to = focus;
		if (t->Up()) to = focus == 4 ? 4 : focus >= 2 ? focus - 2 : 4;
		else if (t->Down()) to = focus == 4 ? 1 : focus < 2 ? focus + 2 : focus;
		else if (t->Left() && focus < 4) to = focus & ~1;
		else if (t->Right() && focus < 4) to = focus | 1;
		if (to != focus && soundOver) soundOver->Play();
		focus = to;
		if (focus == shown) return;
		shown = focus;
		for (int i = 0; i < 5; ++i) buttons[i]->SetState(i == focus ? STATE::SELECTED : STATE::DEFAULT);
	}
private:
	GuiButton* buttons[5];
	int focus = 4;  // Close first: a HOME pressed by mistake costs nothing
	int shown = -1;
};

// The HOME Menu, like the Wii's own: leave to the Homebrew Channel, the
// Wii Menu or Priiloader, or turn the Wii off. Close, B or HOME goes
// back. 0 when closed, else where to go (g_leave). Built and closed with
// the GUI halted.
static int ShowHomeMenu()
{
	HomeDim dim;
	HomeBand band(false), lowBand(true);
	// The name and Close at the band's ends, out to the TV's sides on a widescreen menu.
	f32 safeX, safeW;
	Menu_SafeArea(&safeX, &safeW);
	const int wide = safeX < 0 ? static_cast<int>(-safeX) : 0;
	const bool ios = skin::HomeIos6();
	GuiText titleTxt(tr("HOME Menu"), 32, skin::kWhite);
	// iOS 6's bar: the title in the middle, embossed (a dark edge above it).
	GuiText titleShade(tr("HOME Menu"), 32, (GXColor){0, 0, 0, 120});
	if (ios) {
		Place(titleTxt, 0, 21, true);
		Place(titleShade, 0, 19, true);
	} else {
		Place(titleTxt, 34 - wide, 18);
	}
	HomeBatteries batteries;
	// The four in the middle, between the bands.
	const int row1 = (kHomeBandTop + kHomeBandBottom) / 2 - 85, row2 = row1 + 98;
	SkinButton hbcBtn(skin::homeBtn, skin::homeBtnOver, 8, 60, row1, tr("Homebrew Channel"), 0, 0, 0);
	SkinButton menuBtn(skin::homeBtn, skin::homeBtnOver, 8, 332, row1, tr("Wii Menu"), 0, 0, 0);
	SkinButton priiBtn(skin::homeBtn, skin::homeBtnOver, 8, 60, row2, "Priiloader", 0, 0, 0);
	// iOS 6: Power off is the red one, as an action sheet's destructive button.
	SkinButton offBtn(ios ? skin::homeBtnDanger : skin::homeBtn, ios ? skin::homeBtnDangerOver : skin::homeBtnOver, 8, 332,
		row2, tr("Power off"), 0, 0, 0);
	for (SkinButton* b : {&hbcBtn, &menuBtn, &priiBtn, &offBtn}) {
		b->text.SetColor(ios ? skin::kInk : ScaledColor(skin::kAccent, 0.42f));  // dark on the pale buttons, any theme
		b->text.SetFontSize(24);
	}
	if (ios) offBtn.text.SetColor(skin::kWhite);
	// The Wii's: a small pill; iOS 6's: a bar button.
	const float closeScale = ios ? 1.0f : 0.62f;
	const int closeW = ios ? 120 : static_cast<int>(244 * closeScale);
	SkinButton closeBtn(ios ? skin::iosClose : skin::pill, ios ? skin::iosCloseOver : skin::pillOver, 4,
		600 + wide - closeW, ios ? 24 : 27, tr("Close"),  // inside a TV's overscan (x 608, y 24)
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B | WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, PAD_BUTTON_B | PAD_BUTTON_START,
		WIIDRC_BUTTON_B | WIIDRC_BUTTON_HOME, nullptr, closeScale);
	closeBtn.text.SetFontSize(20);
	closeBtn.text.SetColor(ios ? skin::kWhite : skin::kInkSoft);
	GuiButton* const buttons[5] = {&hbcBtn.button, &menuBtn.button, &priiBtn.button, &offBtn.button, &closeBtn.button};
	HomeFocus focus(buttons);

	GuiWindow w(screenwidth, screenheight);
	ShiftWindow top(1.0f, -90, false, 300), bottom(1.0f, 130, false, 300), middle(0.9f, 0, true, 300);
	w.Append(&dim);
	top.Append(&band);
	if (ios) top.Append(&titleShade);
	top.Append(&titleTxt);
	top.Append(&closeBtn.button);
	bottom.Append(&lowBand);
	bottom.Append(&batteries);
	middle.Append(&focus);
	for (int i = 0; i < 4; ++i) middle.Append(buttons[i]);
	w.Append(&top);
	w.Append(&bottom);
	w.Append(&middle);
	mainWindow->SetState(STATE::DISABLED);
	transition::Begin(transition::Kind::PopOpen);
	mainWindow->Append(&w);
	w.SetState(STATE::DEFAULT);
	logf("HOME Menu: open\n");
	// A sound as it opens and closes, as the Wii's own (NGT's suggestion):
	// RiftWii's own, made by tools/make_sounds.py; Menu sounds sets its level.
	static GuiSound openSound(home_open_pcm, home_open_pcm_size, SOUND::PCM);
	static GuiSound closeSound(home_close_pcm, home_close_pcm_size, SOUND::PCM);
	openSound.Play();
	ResumeGui();
	int choice = -1;
	while (choice < 0) {
		usleep(20000);
		HaltGui();
		if (hbcBtn.Clicked()) choice = 1;
		else if (menuBtn.Clicked()) choice = 2;
		else if (priiBtn.Clicked()) choice = 3;
		else if (offBtn.Clicked()) choice = 4;
		else if (closeBtn.Clicked()) choice = 0;
		if (choice < 0) ResumeGui();
	}
	static const char* const kWhere[] = {"closed", "Homebrew Channel", "Wii Menu", "Priiloader", "power off"};
	logf("HOME Menu: %s\n", kWhere[choice]);
	if (choice == 0) closeSound.Play();  // leaving RiftWii: its own sounds stop anyway
	transition::Begin(transition::Kind::PopClose);
	mainWindow->Remove(&w);
	mainWindow->SetState(STATE::DEFAULT);
	if (choice > 0) g_leave = choice;
	return choice;
}

// The drive answered but would not read the disc's ID: a burned disc
// under the Wii's own IOS, or one the drive cannot read at all. Under
// IOS 58 the menu offers to restart under d2x for this session, which
// reads burned discs on older Wiis; under d2x it says the drive cannot.
// `error` becomes what Home's status line shows.
static void OfferBurnedDisc(std::string& error)
{
	if (riftwii::wii::running_in_dolphin()) return;
	if (riftwii::wii::MenuCiosSlot() != 0) {
		error = tr("The drive cannot read this disc, even through d2x. Later Wii drives read only Nintendo discs, never burned ones; on an older Wii, the burn may be bad.");
		return;
	}
	const int slot = riftwii::wii::BurnedDiscSlot();
	if (slot == 0) {
		error = tr("The drive cannot read this disc. If it is a burned disc, RiftWii needs a d2x cIOS to read it.");
		return;
	}
	const std::string ios = std::to_string(slot);
	if (ShowPopup(tr("Is this a burned disc?"),
		    tr("The drive could not read this disc. If it is a burned disc, RiftWii can read it through d2x on older Wiis (later Wii drives read only Nintendo discs). Burned discs can wear out the disc drive sooner: use them at your own risk. The menu then restarts under IOS{1} for this session.", {ios}),
		    tr("Try with d2x"), tr("Cancel")) != 0) {
		return;
	}
	logf("Home: restarting under IOS%d for a burned disc\n", slot);
	riftwii::wii::WarmRestart(riftwii::wii::RestartKind::BurnedDisc,
		tr("The menu runs under IOS{1} for this session, to read burned discs. Pick the disc.", {ios}));
	error = tr("RiftWii could not restart. Start it again from the Homebrew Channel.");
}

// Downloads and installs a newer release (riftwii::wii::InstallUpdate),
// then offers to leave so the Homebrew Channel starts it.
static void RunUpdate(const std::string& latest)
{
	std::string where, error;
	bool ok;
	{
		PopupBox box(tr("Updating RiftWii"),
			tr("RiftWii {1} is out. Downloading and installing it now; this takes a minute...", {latest}));
		ProgressBar bar(56, 280, 528);
		box.Add(&bar, 528);
		ResumeGui();
		int shown = -1;
		ok = riftwii::wii::InstallUpdate(latest, where, error, [&](double done) {
			// Only whole percents, so the GUI is not halted for every chunk.
			const int percent = static_cast<int>(done * 100);
			if (percent == shown) return;
			shown = percent;
			HaltGui();
			bar.Set(done);
			ResumeGui();
		});
		HaltGui();
	}
	if (!ok) {
		logf("Update: %s\n", error.c_str());
		ShowPopup(tr("Update failed"),
			tr("RiftWii {1} could not be installed: {2}. This version keeps working; the new one is at {3}",
				{latest, FlatCapped(error, 140), riftwii::wii::kReleasesPage}),
			tr("OK"));
		return;
	}
	if (ShowPopup(tr("RiftWii updated"),
		    tr("RiftWii {1} is installed ({2}). It runs the next time RiftWii starts. Leave to the Homebrew Channel now and start it again?",
			    {latest, where}),
		    tr("Leave"), tr("Later")) == 0) {
		ExitRequested = 1;
		ResumeGui();
		while (1) usleep(THREAD_SLEEP);
	}
	SetHomeNotice(tr("RiftWii {1} is installed. Start RiftWii again to use it.", {latest}));
}

// Settings > Install WAD files: every .wad in sd:/wad into the emulated NAND
// the channels are listed from (wii/storage/wadinstall.hpp), asked first,
// with a progress bar. True when any was installed (Home reads them again).
static bool InstallWads()
{
	const std::vector<std::string> files = riftwii::wii::list_wad_files();
	if (files.empty()) {
		ShowPopup(tr("Install WAD files"), tr("No .wad files in the wad folder of the SD card."), tr("OK"));
		return false;
	}
	const riftwii::wii::SharedNandLookup nand = riftwii::wii::look_up_channel_nand();
	if (!nand.usable) {
		ShowPopup(tr("Install WAD files"), tr("The emulated NAND folder can't be used: {1}", {FlatCapped(nand.error, 120)}), tr("OK"));
		return false;
	}
	std::uint8_t key[16];
	if (!riftwii::wii::ConsoleCommonKey(key)) {
		ShowPopup(tr("Install WAD files"), tr("This Wii's key could not be read, so WAD files can't be installed."), tr("OK"));
		return false;
	}
	if (ShowPopup(tr("Install WAD files"), tr("Install {1} WAD file(s) into {2}? The Wii itself is not changed.", {std::to_string(files.size()), nand.folder}),
		    tr("Install"), tr("Cancel")) != 0) {
		std::memset(key, 0, sizeof key);
		return false;
	}
	std::size_t installed = 0;
	std::string failed;
	for (std::size_t i = 0; i < files.size(); ++i) {
		const std::string& path = files[i];
		const std::string name = path.substr(path.rfind('/') + 1);
		std::string code, error;
		bool ok;
		{
			PopupBox box(tr("Installing WAD files"),
				tr("{1} of {2}: {3}", {std::to_string(i + 1), std::to_string(files.size()), FlatCapped(name, 40)}));
			ProgressBar bar(56, 280, 528);
			box.Add(&bar, 528);
			ResumeGui();
			int shown = -1;
			ok = riftwii::wii::install_wad(path, nand.folder, key, [&](double done) {
				const int percent = static_cast<int>(done * 100);
				if (percent == shown) return;
				shown = percent;
				HaltGui();
				bar.Set(done);
				ResumeGui();
			}, code, error);
			HaltGui();
		}
		if (ok) {
			++installed;
		} else {
			logf("WADs: %s not installed: %s\n", path.c_str(), error.c_str());
			failed += (failed.empty() ? "" : ", ") + name;
		}
	}
	std::memset(key, 0, sizeof key);
	// Nothing installed: only what was not, and where the reasons are.
	std::string summary;
	if (installed != 0) summary = tr("Installed {1} of {2}. They are on Home now.", {std::to_string(installed), std::to_string(files.size())});
	if (!failed.empty()) {
		if (!summary.empty()) summary += " ";
		summary += tr("Not installed: {1}. The reasons are in sd:/riftwii/session.log.", {FlatCapped(failed, 120)});
	}
	ShowPopup(tr("Install WAD files"), summary, tr("OK"));
	return installed != 0;
}

// Sends a problem report (wii/online/reportsend.hpp) and shows its link, with a
// QR code of it for a phone.
static void SendReport(const std::string& reason)
{
	riftwii::wii::ReportOutcome r;
	{
		PopupBox box(tr("Sending a report"),
			tr("Gathering the logs and sending them. This can take half a minute..."));
		ResumeGui();
		r = riftwii::wii::SendProblemReport(reason);
		HaltGui();
	}
	if (!r.sent) {
		ShowPopup(tr("Report not sent"),
			r.saved ? tr("It could not be sent: {1}. It is saved on the SD card as sd:/riftwii/report.txt: send that file instead.",
					{FlatCapped(r.error, 120)})
				: tr("It could not be sent or saved: {1}", {FlatCapped(r.error, 120)}),
			tr("OK"));
		return;
	}
	riftwii::QrCode code;
	riftwii::make_qr(r.link, code);
	constexpr int kModule = 4;
	const int side = QrImage::Side(code, kModule);
	QrImage qr(code, 588 - 16 - side, 150, kModule);
	std::string body = std::string(tr("Send this link to whoever is helping you, or scan the code with a phone:")) +
		"\n\n" + r.link;
	if (r.partial) body += std::string("\n\n") + tr("The report was too big, so only its start was kept.");
	PopupBox box(tr("Report sent"), body, tr("OK"));
	if (code.size != 0) box.Add(&qr, 588 - 16 - side - 56 - 16);
	box.Wait();
	// Home's status line said what went wrong; now it says it was sent.
	SetHomeNotice(tr("Report sent: {1}", {r.link}));
}

static bool g_oldCiosReminded = false;  // ShowOldCiosReminder, once a session

// What a report holds and where it goes, said before anything is sent.
static const char* const kReportWhat =
	"It holds RiftWii's logs and settings, the game's choices and packs, and which console, IOS and controllers this is. It goes to paste.rs, or dpaste.com when paste.rs can't be reached, where anyone with its link can read it.";

// After a crash or a failed launch, once: send a report?
static void OfferReport()
{
	static bool asked = false;
	if (asked) return;
	asked = true;
	std::string what;
	if (!riftwii::wii::UnreportedCrash(what)) return;
	const bool game = what == "game";
	const bool crash = what == "crash";
	const int choice = ShowPopup(game ? tr("The game crashed last time") : crash ? tr("RiftWii crashed last time")
			: tr("The last launch failed"),
		std::string(tr("Send a report of what happened?")) + " " + tr(kReportWhat), tr("Send"), tr("Not now"));
	riftwii::wii::NoteCrashAsked();
	const char* what_happened = game ? "the game crashed" : crash ? "RiftWii crashed" : "the launch failed";
	logf("Problem report: %s after the last run (%s)\n", choice == 0 ? "sending" : "declined", what_happened);
	if (choice == 0) SendReport(what_happened);
}

// A newer release found at start: the player picks Update or Not now, and
// Not now is asked once more before it counts (and is logged as a choice).
static bool AgreeToUpdate(const std::string& latest)
{
	if (ShowPopup(tr("Update available"),
		    tr("RiftWii {1} is out (this is {2}). Update now? It takes a minute.", {latest, RIFTWII_VERSION}),
		    tr("Update"), tr("Not now")) == 0)
		return true;
	if (ShowPopup(tr("Are you sure?"),
		    tr("Are you sure you don't want to update? If you had an issue, it could have been fixed in the latest update!"),
		    tr("Update"), tr("Not now")) == 0)
		return true;
	riftwii::wii::NoteUpdateDeclined(latest);
	SetHomeNotice(tr("RiftWii {1} is out. Settings > Check for a new version installs it.", {latest}));
	return false;
}

// d2x v11 before beta3, once a session before a game from the SD card or a
// USB drive: the game still starts. Reports from out-of-date cIOSes are
// harder to tell apart, so it asks for the latest, the guide as a code.
static void ShowOldCiosReminder(int slot, const std::string& name)
{
	static const char* const kGuide = "https://wii.hacks.guide/cios";
	riftwii::QrCode code;
	riftwii::make_qr(kGuide, code);
	constexpr int kModule = 4;
	const int side = QrImage::Side(code, kModule);
	QrImage qr(code, 588 - 16 - side, 150, kModule);
	logf("cIOS: IOS%d is %s, out of date; reminded to update\n", slot, name.c_str());
	PopupBox box(tr("Your cIOS is out of date"),
		tr("IOS{1} is {2}. You're on an out-of-date cIOS, and this makes it harder to find out which bugs are causing what, so please update to the latest cIOS (d2x v11 beta3). Follow this guide: {3}",
			{std::to_string(slot), name, kGuide}),
		tr("OK"));
	if (code.size != 0) box.Add(&qr, 588 - 16 - side - 56 - 16);
	box.Wait();
}

// A drive that is there but cannot be read gets a box; one that is simply
// not inserted does not. Each problem is shown once until it changes.
// What makes a USB drive work, as a checklist with the guide's link as a
// code for a phone. From a drive problem it can also be turned off.
// Returns true when the player chose Don't show again.
static bool ShowUsbHelp(bool offerOff)
{
	// The guide itself: with #troubleshooting the link is longer than a
	// QR code here holds (78 bytes).
	static const char* const kGuide = "https://github.com/KakarottoCake/Riftwii/blob/main/docs/GUIDE.md";
	riftwii::QrCode code;
	riftwii::make_qr(kGuide, code);
	constexpr int kModule = 3;
	const int side = QrImage::Side(code, kModule);
	QrImage qr(code, 588 - 16 - side, 150, kModule);
	PopupBox box(tr("Getting a USB drive working"),
		std::string(tr("1. Use the USB port nearest the edge.")) + "\n" +
			tr("2. Big drives: a Y-cable or their own power.") + "\n" +
			tr("3. FAT32 or NTFS (or a WBFS drive).") + "\n" +
			tr("4. Games in a wbfs or games folder.") + "\n" +
			tr("5. A d2x cIOS in slot 249, 250 or 251.") + "\n" +
			tr("Scan the code for the guide."),
		tr("OK"), offerOff ? std::string(tr("Don't show again")) : std::string());
	if (code.size != 0) box.Add(&qr, 588 - 16 - side - 56 - 16);
	return box.Wait() == 1;
}

static void WarnAboutDrives(const std::string& sdError, const std::string& usbError)
{
	static std::string shown;
	std::string text;
	const auto missing = [](const std::string& e) { return e.find("is inserted") != std::string::npos; };
	const bool usbProblem = !usbError.empty() && !missing(usbError);
	if (!sdError.empty() && !missing(sdError)) text += tr("SD card: {1}", {FlatCapped(sdError, 260)}) + "\n";
	if (usbProblem) text += tr("USB drive: {1}", {FlatCapped(usbError, 260)}) + "\n";
	if (text.empty() || text == shown) return;
	shown = text;
	// A USB problem offers the checklist, unless it was turned off.
	auto& other = riftwii::wii::Settings().other;
	const auto off = other.find("usb_help");
	const bool help = usbProblem && (off == other.end() || off->second != "off");
	const int choice = ShowPopup(tr("Drive problem"),
		text + tr("Games on that drive are not listed. Check the drive on a computer; details are in sd:/riftwii/session.log."),
		tr("OK"), help ? std::string(tr("Help")) : std::string());
	if (choice == 1 && ShowUsbHelp(true)) {
		other["usb_help"] = "off";
		riftwii::wii::SaveSettings();
		logf("USB help: turned off\n");
	}
}

// A Wii U's own storage drive (encrypted; usb_only_wiiu_drive): players on a
// Wii U often leave it plugged in. Said once, the first time it is seen,
// then only Home's status line names it.
static std::string WiiUDriveStatus()
{
	return tr("USB: a Wii U drive (RiftWii leaves it alone)");
}
static void NoteWiiUDrive()
{
	auto& other = riftwii::wii::Settings().other;
	const auto seen = other.find("wiiu_drive_note");
	if (seen != other.end() && seen->second == "shown") return;
	ShowPopup(tr("Wii U drive"),
		tr("The USB drive plugged in is a Wii U's own storage drive. RiftWii can't read it, so it leaves it alone. For Wii games on USB, use a drive formatted FAT32 or NTFS on a computer."),
		tr("OK"));
	other["wiiu_drive_note"] = "shown";
	riftwii::wii::SaveSettings();
}

// What the last game's card log noted (wii/launch/boot.hpp TakeCardLog): into
// the session log and sd:/riftwii/cardlog.txt, with a popup. Once per run.
static void ReportCardLog()
{
	static bool done = false;
	if (done) return;
	done = true;
	const std::vector<std::string> lines = riftwii::wii::TakeCardLog();
	if (lines.empty()) return;
	FILE* f = std::fopen("sd:/riftwii/cardlog.txt", "a");
	for (const std::string& line : lines) {
		logf("%s\n", line.c_str());
		if (f) std::fprintf(f, "%s\n", line.c_str());
	}
	if (f) {
		std::fprintf(f, "\n");
		std::fclose(f);
	}
	ShowPopup(tr("SD card problems"),
		tr("The SD card had trouble while the last game was saving. The details are in sd:/riftwii/cardlog.txt; please send that file to the RiftWii developers."),
		tr("OK"));
}

// A short tour for a new SD card: five pages in the popup box, Next and
// Back (B) between them. Settings > Tutorial shows it again.
static void ShowTutorial()
{
	struct Page { const char* title; const char* body; };
	static const Page kPages[] = {
		{"Welcome to RiftWii",
		 "RiftWii starts your Wii games with Riivolution-format mods, from the disc, a USB drive or the SD card. Your game files are never changed. This short tour shows the basics."},
		{"Your games",
		 "Put games in the wbfs or games folder at the top of the SD card or the USB drive (WBFS, ISO or RVZ). A disc in the drive shows up too. Home lists games that have mods first: press 1, or the round button at the bottom left, to see all your games."},
		{"Mods",
		 "Put mod packs (the XML file and the folders that come with it) in sd:/riivolution or usb:/riivolution. Pick a game, open Mods, switch a pack on and choose its options. Start (or +) plays the game with them."},
		{"Buttons",
		 "Point with the Wii Remote and press A, or move with the D-pad; in the games list, - and + turn the pages. B goes back, 2 opens Settings and HOME opens the HOME Menu. The Classic Controller and GameCube controllers work too, with the same buttons."},
		{"You're all set",
		 "Settings has the video, language, online and update options. For more help, see the guide on RiftWii's GitHub page or join the Discord. Settings > Tutorial shows this tour again."},
	};
	constexpr int kCount = sizeof(kPages) / sizeof(kPages[0]);
	int page = 0;
	while (page >= 0 && page < kCount) {
		const std::string title = std::string(tr(kPages[page].title)) + "  (" + std::to_string(page + 1) + "/" + std::to_string(kCount) + ")";
		const std::string next = page + 1 == kCount ? tr("Let's go") : tr("Next");
		const std::string back = page == 0 ? tr("Skip") : tr("Back");
		page += ShowPopup(title, tr(kPages[page].body), next, back) == 0 ? 1 : -1;
	}
	logf("Tutorial: %s\n", page >= kCount ? "finished" : "skipped");
}

static std::string DiscTileNote()
{
	return tr("The Disc drive's tile on Home, for playing from a disc. A disc still plays from it when it is off: switch it back on here.");
}

// The tour, once per SD card (sd:/riftwii/tutorial_done.txt remembers).
// A card that has been used before (settings, play history, covers, the
// channel offer answered) counts as seen: an update does not show it.
// What's new: this release's main changes, one short line each (the box
// holds six lines), for the release script to keep up to date. Shown
// once when they change; Settings > What's new shows them again.
static const char* const kWhatsNew[] = {
	"GameCube games play in Wii mode (trial)",
	"WiiWare and Virtual Console games on Home",
	"A game can have more Home tiles, each with its own mods",
	"Put the games on Home in your own order",
	"Indigo: a third built-in theme",
	"Saves can be shared with USB Loader GX's emulated NAND",
};

static std::string WhatsNewText()
{
	std::string text;
	for (const char* line : kWhatsNew) text += std::string("\u2022 ") + tr(line) + "\n";
	if (!text.empty()) text.pop_back();
	return text;
}

static void ShowWhatsNew()
{
	ShowPopup(tr("What's new in RiftWii {1}", {RIFTWII_VERSION}), WhatsNewText(), tr("OK"));
}

// Once per list (sd:/riftwii/whatsnew_seen.txt keeps a number made from
// it), not on a card that was just set up (it had the tour).
static void ShowWhatsNewOnce(bool newCard)
{
	static const char* const kMarker = "sd:/riftwii/whatsnew_seen.txt";
	struct stat st;
	if (stat("sd:/", &st) != 0) return;
	std::string list;
	for (const char* line : kWhatsNew) list += std::string(line) + "\n";
	unsigned key = 2166136261u;  // FNV-1a
	for (unsigned char c : list) key = (key ^ c) * 16777619u;
	unsigned seen = 0;
	if (FILE* f = std::fopen(kMarker, "r")) {
		if (std::fscanf(f, "%u", &seen) != 1) seen = 0;
		std::fclose(f);
	}
	if (seen == key) return;
	if (!newCard) {
		logf("What's new: shown\n");
		ShowWhatsNew();
	}
	mkdir("sd:/riftwii", 0777);
	if (FILE* f = std::fopen(kMarker, "w")) {
		std::fprintf(f, "%u\n", key);
		std::fclose(f);
	}
}

// True when the tour was shown now (a new card).
static bool ShowTutorialOnce()
{
	static const char* const kMarker = "sd:/riftwii/tutorial_done.txt";
	struct stat st;
	// Without a card nothing could remember it: it would show every start.
	if (stat("sd:/", &st) != 0 || stat(kMarker, &st) == 0) return false;
	const bool used = stat("sd:/riftwii/settings.txt", &st) == 0 || stat("sd:/riftwii/history.txt", &st) == 0 ||
			  stat("sd:/riftwii/covers", &st) == 0 || stat("sd:/riftwii/channel_offered.txt", &st) == 0;
	if (!used) {
		ShowTutorial();
		// Plenty never play from a disc: the Disc drive's tile is asked for.
		const bool disc = ShowPopup(tr("Disc Channel"), tr("Would you like the Disc Channel to appear on the home screen?"),
			tr("Yes"), tr("No")) == 0;
		riftwii::wii::Settings().home_disc = disc ? "on" : "off";
		riftwii::wii::SaveSettings();
		logf("Disc Channel on Home: %s\n", disc ? "yes" : "no");
		if (!disc)
			ShowPopup(tr("Disc Channel"), tr("You can bring it back any time in Settings > Disc Channel."), tr("OK"));
	}
	mkdir("sd:/riftwii", 0777);
	if (FILE* f = std::fopen(kMarker, "w")) {
		std::fprintf(f, "%s\n", used ? "used before" : "shown");
		std::fclose(f);
	}
	return !used;
}

// Hidden files macOS writes on a FAT card ("._name" beside every copied
// file): counted in the folders RiftWii reads. The game scan and the mod
// folders skip them; this only suggests dot_clean. "Don't show again"
// leaves sd:/riftwii/mac_files_noted.txt.
static void SuggestDotClean()
{
	static const char* const kMarker = "sd:/riftwii/mac_files_noted.txt";
	static bool asked = false;
	struct stat st;
	if (asked || stat(kMarker, &st) == 0) return;
	asked = true;
	static const char* const kDirs[] = {"sd:/", "sd:/riivolution", "sd:/wbfs", "sd:/games", "sd:/apps/riftwii"};
	unsigned found = 0;
	for (const char* dir : kDirs) {
		DIR* d = opendir(dir);
		if (!d) continue;
		// Capped: a damaged directory chain can make readdir go round forever.
		unsigned seen = 0;
		while (dirent* e = readdir(d)) {
			if (++seen > 2000) break;
			if (riftwii::is_mac_metadata(e->d_name)) ++found;
		}
		closedir(d);
	}
	if (found == 0) return;
	logf("SD: %u file(s) from macOS (._name, .DS_Store); skipped\n", found);
	if (ShowPopup(tr("Files from a Mac on the SD card"),
		tr("This SD card has hidden files that macOS makes when it copies (names that start with \"._\"). RiftWii skips them, but they fill the card and can confuse other homebrew. To remove them, put the card in your Mac, open Terminal and type: dot_clean -m /Volumes/ followed by the card's name. On Windows, delete the files whose names start with \"._\"."),
		tr("OK"), tr("Don't show again")) == 0) return;
	mkdir("sd:/riftwii", 0777);
	if (FILE* f = std::fopen(kMarker, "w")) {
		std::fprintf(f, "%u\n", found);
		std::fclose(f);
	}
}

// Asked once per SD card (sd:/riftwii/channel_offered.txt remembers the
// answer): the channel can be added, and is not there yet. True to open
// the installer.
static bool OfferChannelOnce()
{
	static const char* const kMarker = "sd:/riftwii/channel_offered.txt";
	struct stat st;
	if (stat(kMarker, &st) == 0) return false;
	if (riftwii::wii::CurrentRestartNote().kind == riftwii::wii::RestartKind::ChannelDone) return false;
	unsigned version = 0;
	std::string why;
	if (riftwii::wii::ChannelInstalled(version) || !riftwii::wii::ChannelCanInstall(why)) return false;
	const bool add = ShowPopup(tr("Add RiftWii to the Wii Menu?"),
		tr("RiftWii can have a channel on the Wii Menu, so it starts without the Homebrew Channel. The channel only starts RiftWii from your SD card: RiftWii's updates keep working and the channel never needs reinstalling. The channel installer opens, then brings you back here. Settings can open it again later."),
		tr("Open installer"), tr("No thanks")) == 0;
	FILE* f = std::fopen(kMarker, "w");
	if (f) {
		std::fprintf(f, "%s\n", add ? "added" : "declined");
		std::fclose(f);
	}
	logf("Channel: offered, %s\n", add ? "accepted" : "declined");
	return add;
}

// An older channel than this release's installer puts on: RiftWii is not
// used until it is updated (the old ones went black when a game's HOME
// Menu > Wii Menu came back to RiftWii, #62). Every start asks, with no
// "Not now": Update now opens the installer, else RiftWii exits. Not in
// Dolphin (the channel cannot be installed there), and not while an in-app
// update is still bringing the new installer (the next start asks). A
// headless launch never comes here. MENU_CHANNEL, MENU_EXIT or MENU_NONE.
static int RequireChannelUpdate()
{
	unsigned version = 0;
	if (!riftwii::wii::ChannelInstalled(version) || version >= riftwii::wii::ChannelVersionNeeded()) return MENU_NONE;
	if (riftwii::wii::running_in_dolphin()) {
		logf("Channel: version %u installed, %u needed; not asked in Dolphin\n", version, riftwii::wii::kChannelVersion);
		return MENU_NONE;
	}
	if (riftwii::wii::AppsPackPending()) {
		logf("Channel: version %u installed, %u needed; the in-app update brings the installer first\n", version,
			riftwii::wii::kChannelVersion);
		SetHomeNotice(tr("The RiftWii channel needs updating. Its new installer is still downloading; RiftWii asks you to install it at the next start."));
		return MENU_NONE;
	}
	const bool stillOld = riftwii::wii::CurrentRestartNote().kind == riftwii::wii::RestartKind::ChannelDone;
	const std::string have = std::to_string(version), need = std::to_string(riftwii::wii::ChannelVersionNeeded());
	for (;;) {
		std::string why;
		const bool can = riftwii::wii::ChannelCanInstall(why);
		std::string body = tr("Your RiftWii channel on the Wii Menu is version {1}, and this RiftWii needs version {2}: it fixes the black screen when a game's HOME Menu > Wii Menu goes back to RiftWii. RiftWii can't be used until the channel is updated.",
			{have, need});
		if (stillOld)
			body += std::string("\n\n") + tr("The channel is still version {1} after the installer, so the installer on your SD card is an old one. Copy apps/riftwii_channel from the newest RiftWii zip onto the card, then update again.",
				{have});
		if (can) body += std::string("\n\n") + tr("The installer opens, then brings you back here.");
		else body += std::string("\n\n") + tr("RiftWii can't update it from here: {1}.", {why});
		logf("Channel: version %u installed, %u needed; RiftWii waits for the update%s%s\n", version,
			riftwii::wii::kChannelVersion, can ? "" : ": ", can ? "" : why.c_str());
		const int choice = ShowPopup(tr("Update the RiftWii channel"), body, can ? tr("Update now") : tr("Check again"),
			tr("Exit RiftWii"));
		if (choice != 0) {
			logf("Channel: update declined; RiftWii exits\n");
			return MENU_EXIT;
		}
		if (can) {
			logf("Channel: update to %u accepted\n", riftwii::wii::kChannelVersion);
			return MENU_CHANNEL;
		}
		// Check again: the card may have been fixed meanwhile.
		if (!riftwii::wii::ChannelInstalled(version) || version >= riftwii::wii::ChannelVersionNeeded()) return MENU_NONE;
	}
}

static void ScanDrives(FrontendState& state, GuiText& status)
{
	std::string error;
	status.SetText("Reading the SD card...");
	ResumeGui();
	const bool sd = scan_sd_games(state.sd_catalog, error);
	// The GUI thread is drawing: the text changes only while it is halted.
	const std::string net = riftwii::wii::RefreshNetworkPacks([&](const char* line) {
		HaltGui();
		status.SetText(line);
		ResumeGui();
	});
	if (!net.empty()) logf("%s\n", net.c_str());
	HaltGui();
	riftwii::wii::mem::CheckHeap("after the SD scan");
	std::string sdError;
	if (!sd) {
		logf("SD scan failed: %s\n", error.c_str());
		state.sd_catalog.status = "SD: " + (error.empty() ? std::string(tr("scan failed")) : error);
		sdError = error.empty() ? std::string("scan failed") : error;
	}
	error.clear();
	status.SetText("Reading the USB drive... (a big drive takes a moment)");
	ResumeGui();
	const bool usb = scan_usb_games(state.usb_catalog, error);
	HaltGui();
	riftwii::wii::mem::CheckHeap("after the USB scan");
	{
		std::string channels;
		riftwii::wii::list_nand_channels(state.channels, channels);
		// And those USB Loader GX keeps in an emulated NAND on the card.
		const riftwii::wii::SharedNandLookup emu = riftwii::wii::look_up_channel_nand();
		if (emu.usable) riftwii::wii::list_emu_channels(emu.folder, state.channels, channels);
	}
	// The scan's reads are over: the GameCube adapter may start now, so its
	// pads can answer the drive popup below (a Wii U has no GameCube ports:
	// with only an adapter pad, OK could not be pressed).
	riftwii::wii::GcAdapterMenuAllowStart();
	// A Wii U's own drive is not a broken drive: no "Drive problem", one
	// note the first time, and a calm line on Home after that.
	const bool wiiuDrive = !usb && riftwii::wii::usb_only_wiiu_drive();
	WarnAboutDrives(sdError, usb || wiiuDrive ? std::string() : error.empty() ? std::string("scan failed") : error);
	if (wiiuDrive) NoteWiiUDrive();
	if (sd) ReportCardLog();
	if (wiiuDrive) {
		logf("USB: only a Wii U's own drive is plugged in; left alone\n");
		state.usb_catalog.status = WiiUDriveStatus();
	} else if (!usb) {
		logf("USB scan failed: %s\n", error.c_str());
		state.usb_catalog.status = "USB: " + (error.empty() ? std::string(tr("scan failed")) : error);
		if (riftwii::wii::MenuCiosSlot() != 0 && error.find("more than one") == std::string::npos) {
			state.usb_catalog.status += " " + tr("(the menu runs under IOS {1}; USB drives need a base-58 cIOS for that, or set the menu IOS back to 58)",
				{std::to_string(riftwii::wii::MenuCiosSlot())});
		} else if (riftwii::wii::MenuIosLost() != 0 && error.find("more than one") == std::string::npos) {
			state.usb_catalog.status += " " + tr("(after the failed launch RiftWii came back under IOS {1}, which cannot read the drive here; start RiftWii again from the Homebrew Channel)",
				{std::to_string(riftwii::wii::MenuIosLost())});
		}
	}
	// After the USB scan: packs on the drive count too.
	LoadPackIndex();
	riftwii::wii::mem::CheckHeap("after the pack index");
	OfferReport();
	// A newer release, asked at every start when downloads are on, in the
	// background: the network takes seconds to come up, and the menu used
	// to hold every press until it had (Home answers it, TakeUpdateCheck).
	static bool updateChecked = false;
	if (!updateChecked && riftwii::wii::Settings().online) {
		updateChecked = true;
		riftwii::wii::StartUpdateCheck();
	}
	g_scanned = true;
}

// The start's update check once it has finished: the player is asked
// before a newer release is installed. On Home, with the GUI halted; true
// when the status line may have changed.
static bool TakeUpdateCheck()
{
	bool ok = false, newer = false;
	std::string latest, why;
	if (!riftwii::wii::TakeUpdateCheck(ok, latest, newer, why)) return false;
	riftwii::wii::mem::CheckHeap("after the update check");
	if (!ok) logf("Update check: %s\n", why.c_str());
	else if (newer && riftwii::wii::UpdateInstalled(latest))
		SetHomeNotice(tr("RiftWii {1} is installed. Start RiftWii again to use it.", {latest}));
	else if (newer && AgreeToUpdate(latest))
		RunUpdate(latest);
	return true;
}

// Home's clock as the Wii Menu's: its figures in seven segments (from
// skin::clockDigits), the colon blinking each second, and what follows
// them ("AM", "PM") in the menu's font. A clock text that does not start
// with figures is shown as it is.
class LcdClock : public GuiElement {
public:
	LcdClock(int centreX, int top, int height) : cx(centreX), top(top), h(height), rest("", height * 2 / 5 + 1, skin::kClock) {
		rest.SetParent(this);
		rest.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	}
	void SetClock(const std::string& text) {
		std::size_t n = 0;
		while (n < text.size() && ((text[n] >= '0' && text[n] <= '9') || text[n] == ':')) ++n;
		figures = text.substr(0, n);
		std::string after = text.substr(n);
		while (!after.empty() && after[0] == ' ') after.erase(0, 1);
		rest.SetText(after.c_str());
	}
	void Draw() override {
		const skin::Tex& t = skin::clockDigits;
		if (!IsVisible() || !t.data || figures.empty()) return;
		const float s = static_cast<float>(h) / 44.0f;
		const float digitW = 28 * s, colonW = 14 * s, gap = 10 * s;
		float width = 0;
		for (char c : figures) width += c == ':' ? colonW : digitW;
		const int restW = rest.GetTextWidth();
		// The figures are centred, as on the Wii Menu: AM or PM, small,
		// at their top right (clear of the bar's edge below).
		// Centred on what is lit, not on the cells: a 1 lights only the
		// right of its cell (x 21.5-26.5 of 28, the others from 1.5), so
		// "11:37" sat 9 pixels right of the middle.
		const float inkLeft = (figures[0] == '1' ? 21.5f : 1.5f) * s, inkRight = width - 1.5f * s;
		float x = cx - (inkLeft + inkRight) / 2;
		const bool colonOn = (time(nullptr) & 1) == 0;  // blinks, as the Wii Menu's
		for (char c : figures) {
			if (c == ':') {
				if (colonOn)
					Menu_DrawImgPart(x, top, colonW, h, static_cast<u16>(t.w), static_cast<u16>(t.h), t.data,
						(10 * 28 + 7) / 308.0f, 0, (10 * 28 + 21) / 308.0f, 1, 255);
				x += colonW;
				continue;
			}
			const int d = c - '0';
			Menu_DrawImgPart(x, top, digitW, h, static_cast<u16>(t.w), static_cast<u16>(t.h), t.data,
				d * 28 / 308.0f, 0, (d + 1) * 28 / 308.0f, 1, 255);
			x += digitW;
		}
		if (restW > 0) {
			rest.SetPosition(static_cast<int>(x + gap * 0.6f), top - 1);
			rest.Draw();
		}
	}
private:
	int cx, top, h;
	std::string figures;
	GuiText rest;
};

static void ClockText(std::string& clock, std::string& date)
{
	const std::string& mode = riftwii::wii::Settings().clock;
	const time_t now = time(nullptr);
	struct tm local;
	localtime_r(&now, &local);
	static const char* const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	const int h = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
	char buf[32];
	// {1} the 12-hour hour, {2} the minutes, {3} the 24-hour hour.
	snprintf(buf, sizeof(buf), "%02d", local.tm_min);
	const char* const ampm = local.tm_hour < 12 ? "{1}:{2} AM" : "{1}:{2} PM";
	if (mode == "24") {
		clock = std::to_string(local.tm_hour) + ":" + buf;
	} else if (mode == "12") {
		// The language's own 12-hour way, unless it writes the 24-hour hour
		// ({3}): then the English one, which has AM and PM.
		const std::string own = tr(ampm);
		clock = own.find("{1}") != std::string::npos ? tr(ampm, {std::to_string(h), buf, std::to_string(h)})
			: std::to_string(h) + ":" + buf + (local.tm_hour < 12 ? " AM" : " PM");
	} else {
		clock = tr(ampm, {std::to_string(h), buf, std::to_string(local.tm_hour)});
	}
	date = tr("{1} {2}/{3}", {tr(days[local.tm_wday % 7]), std::to_string(local.tm_mon + 1), std::to_string(local.tm_mday)});
}

// Asks for the words to look for on an on-screen keyboard. Returns false
// when cancelled; `text` holds the entry. Called with the GUI halted and
// returns with it halted, as ShowHomeMenu does: the caller rebuilds the
// grid's items next, which the GUI thread must not be drawing. `typed`
// runs, with the GUI halted, each time the entry changes (Home filters
// its games as the player types, a tester's wish) and returns the line
// shown beside the title, `matches` the games named above the card.
// `typed` gets what is typed so far and fills in the matches' names and the
// characters that still lead to a game when typed next (see
// GuiSearchKeys::SetAvailable); it returns the count line.
static bool AskSearch(std::string& text,
	const std::function<std::string(const std::string&, std::string& matches, std::string& next)>& typed)
{
	GuiSearchKeys keys(text);
	std::string matches, next;
	keys.SetCount(typed(text, matches, next));
	keys.SetMatches(matches);
	keys.SetAvailable(next);
	mainWindow->SetState(STATE::DISABLED);
	mainWindow->Append(&keys);
	keys.SetState(STATE::DEFAULT);
	ResumeGui();
	std::string last = text;
	while (keys.Result() == 0) {
		usleep(20000);
		if (keys.Text() == last) continue;
		HaltGui();
		last = keys.Text();
		keys.SetCount(typed(last, matches, next));
		keys.SetMatches(matches);
		keys.SetAvailable(next);
		ResumeGui();
	}
	HaltGui();
	mainWindow->Remove(&keys);
	mainWindow->SetState(STATE::DEFAULT);
	if (keys.Result() > 0) text = keys.Text();
	return keys.Result() > 0;
}

static int MenuSource(FrontendState& state)
{
	int menu = MENU_NONE;
	LoadFilter();
	std::vector<GridItem> items;
	std::vector<HomeEntry> entries;
	BuildHome(state, items, entries);

	GuiGameGrid grid;
	grid.SetCovers(riftwii::wii::Settings().home_tiles != "names");
	const std::string& homeTiles = riftwii::wii::Settings().home_tiles;
	grid.SetShelf(homeTiles == "shelf" || homeTiles == "carousel" || homeTiles == "covers3d");
	grid.SetCarousel(homeTiles == "carousel");
	grid.SetRing(homeTiles == "covers3d");
	grid.SetRoomy(riftwii::wii::Settings().grid_spacing == "roomy");
	skin::SetOnHome(true);  // the wall for the view now set
	grid.SetChannels(riftwii::wii::Settings().home_tiles == "channels",
		[&items](int index, const GuiGameGrid::IconBox& b) {
			if (index < 0 || static_cast<std::size_t>(index) >= items.size()) return false;
			riftwii::wii::BannerPlayer* icon = IconFor(items[static_cast<std::size_t>(index)].id);
			if (!icon) return false;
			icon->Step();
			riftwii::wii::RoundClip clip;
			clip.x = b.clipX;
			clip.y = b.clipY;
			clip.w = b.clipW;
			clip.h = b.clipH;
			clip.radius = b.radius;
			icon->Draw(b.x, b.y, b.w, b.h, b.alpha, &clip);
			return true;
		});
	// A profile's tile: its picture in the corner (riftwii/profiles.hpp).
	// The loop below reads the page's pictures first; one the grid asks
	// for before that (the first frame, a page just turned) is read here,
	// on the GUI thread, once.
	grid.SetPictureSource([&items](int index) -> const u8* {
		if (index < 0 || static_cast<std::size_t>(index) >= items.size()) return nullptr;
		const riftwii::Profile* profile = riftwii::wii::Profiles().find(items[static_cast<std::size_t>(index)].key);
		return profile ? riftwii::wii::ProfilePicture(profile->key(), profile->art) : nullptr;
	}, riftwii::wii::kProfilePicture);
	grid.SetPagePeek(riftwii::wii::Settings().page_peek != "off");
	grid.SetItems(&items);
	grid.Focus(g_homeFocus);
	HomeBar bar;

	// On a widescreen menu the corners' buttons and words move out by
	// `wide` to the TV's sides, as far in from them as on a 4:3 one; the
	// covers, clock and date stay in the middle.
	f32 safeX, safeW;
	Menu_SafeArea(&safeX, &safeW);
	const int wide = safeX < 0 ? static_cast<int>(-safeX) : 0;
	// The Wii Menu's bar (it dips under the clock): the view and the page
	// on its high sides, under its line; a bar that bumps: above it.
	const bool dip = !skin::BarBump();
	GuiText pageTxt("", dip ? 14 : 15, skin::kInkDim);
	GuiText viewTxt("", dip ? 14 : 16, skin::kAccent);
	// The room each label has beside the dip; 0 on a bar that bumps, where
	// the view's own cut-off below applies and the page has no limit.
	int sideW = 0;
	if (dip) {
		// Clear of the dip (58% of the bar, which is 856 wide on a widescreen menu).
		const int dipLeft = 320 - static_cast<int>(0.29f * (wide > 0 ? 856 : 640));
		// From the overscan's edges inwards (x 36 and 604, 4 inside the safe
		// area), over the round buttons under them; one line, clear of the dip.
		// That leaves 320 - 185 - 10 - 36 = 89 px on a 4:3 menu (about 134 on
		// a widescreen one). "Page 1 of 28" at 14 px is wider than that, so
		// the old cut ended it in "...".
		sideW = dipLeft - 10 - (36 - wide);  // a widescreen menu's start further out, at its own edge
		viewTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		viewTxt.SetPosition(36 - wide, 354);
		viewTxt.SetMaxWidth(sideW);
		pageTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
		pageTxt.SetPosition(-36 + wide, 354);
	} else {
		Place(pageTxt, 40 - wide, 300);
		// The view in use, always on screen (the status line below gives way to notices).
		Place(viewTxt, 40 - wide, 324);
		viewTxt.SetMaxWidth(190 + wide);  // ends by x 230, clear of the clock; a longer one ends in "..."
	}
	// A name over a round button while the pointer rests on it.
	GuiText filterHint(tr("View"), 17, skin::kInk), settingsHint(tr("Settings"), 17, skin::kInk),
		searchHint(tr("Search"), 17, skin::kInk);
	// The filter's and the gear's names go above them (80 square, y 384-464),
	// centred on each; the small search button's to its left, level with it
	// (the pointer's hand covers what is below or right of the button).
	filterHint.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	filterHint.SetPosition(72 - 320 - wide, 344);
	settingsHint.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	settingsHint.SetPosition(568 - 320 + wide, 344);
	// The search button: in the top right corner, above the page arrow; in
	// Channels, whose four icons at their own size reach the corner, on the
	// bar left of the gear.
	const bool channelsView = riftwii::wii::Settings().home_tiles == "channels";
	const int searchX = channelsView ? 484 + wide : 568 + wide, searchY = channelsView ? 396 : 24;
	const int searchHintEnd = searchX - 18, searchHintTop = searchY + 9;  // its box clear of the button
	searchHint.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	searchHint.SetPosition(searchHintEnd - 640, searchHintTop);
	// Each on a box of its own: the search's sits over the covers.
	HintChip filterChip(filterHint, 72 - wide, 344, true), settingsChip(settingsHint, 568 + wide, 344, true),
		searchChip(searchHint, searchHintEnd, searchHintTop, false);
	int hintAlpha[3] = {0, 0, 0};  // the hover names' fades
	for (GuiText* hint : {&filterHint, &settingsHint, &searchHint}) {
		hint->SetAlpha(0);
		hint->SetVisible(false);
	}
	std::string clock, date;
	ClockText(clock, date);
	// The Wii Menu's way (a bar that dips): the clock in the dip, the date
	// under it on the bar, notices under that. A bar that bumps: the clock
	// above the bump, the date in it.
	GuiText clockTxt(clock.c_str(), 34, skin::kClock);
	Place(clockTxt, 0, 296, true);
	LcdClock lcd(320, 351, 40);
	lcd.SetClock(clock);
	if (dip) clockTxt.SetVisible(false);
	else lcd.SetVisible(false);
	GuiText dateTxt(date.c_str(), dip ? 22 : 18, skin::kInkSoft);
	Place(dateTxt, 0, dip ? 407 : 359, true);
	// Ending by y 456, inside a TV's overscan: one line under the date on a
	// bar that dips, three above the bottom on one that bumps.
	GuiText statusTxt("", dip ? 15 : 17, skin::kInkSoft);
	Place(statusTxt, 0, dip ? 434 : 392, true);
	// Between the round buttons (x 110 and 530 on 4:3), and clear of the
	// search button when it is on the bar (from x 484).
	statusTxt.SetWrap(true, (channelsView ? 330 : 400) + 2 * wide, dip ? 1 : 3);
	EmptyHomeNote emptyNote;
	emptyNote.SetVisible(false);

	// Inside a TV's overscan: 5% in from the sides and the bottom.
	SkinButton filterBtn(skin::roundBtn, skin::roundBtnOver, 2, 34 - wide, 376, nullptr,
		WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y, WIIDRC_BUTTON_X, &skin::iconDrives);
	SkinButton settingsBtn(skin::roundBtn, skin::roundBtnOver, 2, 530 + wide, 376, nullptr,
		WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_X, PAD_TRIGGER_R, WIIDRC_BUTTON_Y, &skin::iconGear);
	// Search: Z on a GameCube controller, ZL on a Classic Controller (a
	// Wii Remote has no button left; point and press A). Small, in the top
	// right corner above the page arrow, inside the overscan (ends at x 606,
	// starts at y 24), so the gear's name has room; in Channels on the bar.
	SkinButton searchBtn(skin::roundBtn, skin::roundBtnOver, 2, searchX, searchY, nullptr,
		WPAD_CLASSIC_BUTTON_ZL, PAD_TRIGGER_Z, 0, &skin::iconSearch, 0.5f);
	// Minus and Plus turn the grid's pages (in GuiGameGrid). Rescan is in
	// Settings, and on X of a GameCube controller, which has neither. Its
	// Wii Remote and Classic buttons are bits neither ever sends (a 0 would
	// match any press that leaves that half empty).
	constexpr u32 kNoWpadButton = 0x0020 | (0x0100u << 16);
	// B (L on a GameCube controller) and the letters are the grid's own
	// (GuiGameGrid::LetterKeys).
	GuiTrigger trigRescan, trigExit;
	trigRescan.SetButtonOnlyTrigger(-1, kNoWpadButton, PAD_BUTTON_X, 0);
	trigExit.SetButtonOnlyTrigger(-1, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, PAD_BUTTON_START, WIIDRC_BUTTON_HOME);
	GuiButton rescanBtn(0, 0), exitBtn(0, 0);  // hotkeys only
	rescanBtn.SetTrigger(&trigRescan);
	exitBtn.SetTrigger(&trigExit);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&grid);
	w.Append(&emptyNote);
	w.Append(&bar);
	w.Append(&pageTxt);
	w.Append(&viewTxt);
	w.Append(&filterChip);
	w.Append(&settingsChip);
	w.Append(&searchChip);
	w.Append(&filterHint);
	w.Append(&settingsHint);
	w.Append(&searchHint);
	w.Append(&clockTxt);
	w.Append(&lcd);
	w.Append(&dateTxt);
	w.Append(&statusTxt);
	w.Append(&filterBtn.button);
	w.Append(&settingsBtn.button);
	w.Append(&searchBtn.button);
	w.Append(&rescanBtn);
	w.Append(&exitBtn);
	Toast toast;
	w.Append(&toast);
	mainWindow->Append(&w);
	// News left for Home (a report sent, a new version): a notice as well,
	// once. Home is built again each time Settings or a game's page is
	// left, and a tester saw "Menu font: Wii Menu" pop up every time; the
	// status line keeps the news until a game is opened.
	if (!g_homeNotice.empty() && !g_homeNoticeShown) {
		toast.Show(g_homeNotice, false);
		g_homeNoticeShown = true;
	}

	// The page number is never cut: a page count has to be read whole. When
	// "Page 1 of 28" (or its translation) is wider than the room beside the
	// dip, it is shown as "1 / 28", which needs no translation.
	const auto setPageText = [&](int page, int pages) {
		const std::string full = tr("Page {1} of {2}", {std::to_string(page), std::to_string(pages)});
		pageTxt.SetText(full.c_str());
		if (sideW > 0 && pageTxt.GetTextWidth() > sideW) {
			const std::string compact = std::to_string(page) + " / " + std::to_string(pages);
			pageTxt.SetText(compact.c_str());
		}
	};
	// The view's name, shrunk a size at a time (never below 12 px) before
	// it would be cut: "Todos los juegos" is wider than the 89 px of a
	// 4:3 menu at 14 px, and a name reads better small than ended in "...".
	const auto setViewText = [&](const std::string& text) {
		viewTxt.SetText(text.c_str());
		if (sideW <= 0) return;
		for (int size = 14; size >= 12; --size) {
			viewTxt.SetFontSize(size);
			if (viewTxt.GetTextWidth() <= sideW) break;
		}
	};
	const auto showView = [&] {
		// A search lists every game whatever the filter, so it is the view.
		// Over the view's own round button on a bar that dips: its name is enough.
		const std::string name = g_search.empty() ? std::string(tr(dip ? FilterShortLabel(g_filter) : FilterLabel(g_filter)))
			: tr("Search \"{1}\"", {g_search});
		setViewText(dip ? name : std::string(tr("View")) + ": " + name);
		// While a search is on, the round button's first press ends it.
		filterHint.SetText(g_search.empty() ? tr("View") : tr("Clear search"));
	};
	showView();
	// Channels: the page whose banners were all looked at, by its first
	// tile. Other games take those tiles after a refresh (a search, another
	// view): their banners are looked for again.
	int bannerPageDone = -1;
	const auto refresh = [&](bool keepFocus) {
		// Another view, search or order: crossfaded (unless the screen is
		// already changing some other way).
		transition::BeginAuto();
		showView();
		const int focus = keepFocus ? grid.FocusedIndex() : 0;
		BuildHome(state, items, entries);
		bannerPageDone = -1;
		grid.SetItems(&items);
		SetHomeMovable(grid, entries);
		grid.Focus(std::min(focus, std::max(0, static_cast<int>(items.size()) - 1)));
		statusTxt.SetText(HomeStatus(state, items).c_str());
		emptyNote.SetVisible(state.sd_catalog.games.empty() && state.usb_catalog.games.empty() && state.channels.empty());
	};
	if (!g_scanned) {
		ScanDrives(state, statusTxt);  // the GameCube adapter may start from here on
		// An old channel: nothing of Home until it is updated (the popup
		// returns with the GUI halted, as this function's end has it).
		// After the scan, so an adapter's pads can answer it too. Home is
		// taken off the screen, not hidden: GuiWindow::SetVisible(true)
		// shows every element again, the ones Home keeps hidden too (the
		// plain clock under the Wii look's LCD one).
		mainWindow->Remove(&w);
		menu = RequireChannelUpdate();
		if (menu != MENU_NONE) return menu;
		mainWindow->Append(&w);
		ShowWhatsNewOnce(ShowTutorialOnce());
		SuggestDotClean();
		if (OfferChannelOnce()) menu = MENU_CHANNEL;
		refresh(true);
		// The first time Home shows, it opens on the last game played.
		const std::vector<std::string> recent = riftwii::wii::History().recent(1);
		if (!g_focusRestored && !recent.empty()) {
			for (std::size_t i = 0; i < items.size(); ++i) {
				if (items[i].key != recent[0]) continue;
				grid.Focus(static_cast<int>(i));
				break;
			}
		}
		g_focusRestored = true;
	} else {
		statusTxt.SetText(HomeStatus(state, items).c_str());
	}
	ResumeGui();

	// Covers still to fetch, one per turn of the loop while the GUI
	// thread keeps drawing: the page on screen first.
	std::vector<std::string> coverQueue;
	const auto queueCovers = [&]() {
		coverQueue.clear();
		if (g_coversOff || riftwii::wii::Settings().home_tiles == "names" || !riftwii::wii::Settings().online) return;
		for (const GridItem& item : items) {
			if (g_coversChecked.insert(item.id).second && item.id != riftwii::wii::CoverFetchGame() &&
			    riftwii::wii::CoverWanted(item.id))
				coverQueue.push_back(item.id);
		}
	};
	queueCovers();

	SetHomeMovable(grid, entries);
	int shownPage = -1, shownPages = -1;
	// While a game is being moved, the status line says how to put it down.
	bool carryNoteShown = false;
	// The status line says covers are coming while they are, and why they
	// stopped when a download failed.
	bool coverNoteShown = false;
	while(menu == MENU_NONE)
	{
		usleep(10000);
		std::string coverNote;
		// The covers download on the network's own thread, one at a time:
		// done on the menu's, each held every press for seconds.
		std::string arrived, coverError;
		riftwii::wii::CoverFetch got = riftwii::wii::CoverFetch::NotFound;
		if (riftwii::wii::TakeCoverFetch(arrived, got, coverError)) {
			if (got == riftwii::wii::CoverFetch::Failed) {
				logf("Covers: stopped: %s\n", coverError.c_str());
				g_coversOff = true;
				coverQueue.clear();
				coverNote = tr("Covers could not be downloaded ({1}). To try again, use Settings > Look for games again.", {FlatCapped(coverError, 60)});
			}
			if (got != riftwii::wii::CoverFetch::Stored) arrived.clear();
		}
		std::string boxArrived, boxError;
		riftwii::wii::CoverFetch boxGot = riftwii::wii::CoverFetch::NotFound;
		if (riftwii::wii::TakeBoxFetch(boxArrived, boxGot, boxError)) {
			if (boxGot == riftwii::wii::CoverFetch::Failed) {
				logf("Shelf: box downloads stopped: %s\n", boxError.c_str());
				g_boxesOff = true;
			}
			if (boxGot != riftwii::wii::CoverFetch::Stored) boxArrived.clear();
		}
		std::string cover3dArrived, cover3dError;
		riftwii::wii::CoverFetch cover3dGot = riftwii::wii::CoverFetch::NotFound;
		if (riftwii::wii::TakeCover3dFetch(cover3dArrived, cover3dGot, cover3dError)) {
			if (cover3dGot == riftwii::wii::CoverFetch::Failed) {
				logf("3D covers: downloads stopped: %s\n", cover3dError.c_str());
				g_covers3dOff = true;
			}
			if (cover3dGot != riftwii::wii::CoverFetch::Stored) cover3dArrived.clear();
		}
		// Channels: a GameCube game's animated banner, downloaded on the
		// network's thread (with Download names and cheats on), one game at
		// a time, the page shown's first; its disc's picture shows
		// meanwhile. Before the covers, which this view does not show.
		std::string gcBannerArrived;
		{
			std::string fetched, fetchError;
			riftwii::wii::BannerFetch fetchGot = riftwii::wii::BannerFetch::NotFound;
			if (riftwii::wii::TakeGameCubeBannerFetch(fetched, fetchGot, fetchError)) {
				if (fetchGot == riftwii::wii::BannerFetch::Failed) {
					logf("Banners: no more GameCube banner downloads this session: %s\n", fetchError.c_str());
					g_gcBannersOff = true;
				} else if (fetchGot == riftwii::wii::BannerFetch::Stored) {
					gcBannerArrived = fetched;
				}
			}
		}
		if (grid.Channels() && !g_gcBannersOff && riftwii::wii::Settings().online && !riftwii::wii::NetBackgroundBusy() &&
		    !riftwii::wii::NetFailed() && riftwii::wii::CoverFetchGame().empty() &&
		    riftwii::wii::GameCubeBannerFetchGame().empty()) {
			// The page shown, and the next page's column peeking in.
			const std::size_t first = static_cast<std::size_t>(grid.PageFirst());
			for (std::size_t k = first; k < first + GuiGameGrid::kPerPage + 4 && k < items.size(); ++k) {
				const HomeEntry& e = entries[k];
				if (e.kind != HomeEntry::Kind::Sd) continue;
				const riftwii::wii::ImageGame& game = state.sd_catalog.games[e.index];
				if (!game.gamecube || !g_gcBannersAsked.insert(game.id).second) continue;
				if (!riftwii::wii::GameCubeBannerWanted(game.id)) continue;
				// A mod is asked for by its own ID only: the three-letter
				// banner would be the game it was made from.
				const bool regional = riftwii::gc_banner_regional_copy(game.id, !game.gc_patch_source.empty(),
					riftwii::wii::LoadedTitles());
				if (riftwii::wii::StartGameCubeBannerFetch(game.id, regional))
					logf("Banners: asking for %s's animated banner (%s)\n", game.id.c_str(),
						regional ? "its ID, then its first three letters: a regional copy of a game"
							: "its own ID only: a mod, or a game the title list does not show in another region");
				break;
			}
		}
		// Not while the update check has the network.
		if (!coverQueue.empty() && !g_coversOff && !riftwii::wii::NetBackgroundBusy() && !riftwii::wii::NetFailed() &&
		    riftwii::wii::CoverFetchGame().empty()) {
			std::size_t pick = 0;
			bool onPage = false;
			const std::size_t first = static_cast<std::size_t>(grid.Page()) * GuiGameGrid::kPerPage;
			for (std::size_t q = 0; q < coverQueue.size() && !onPage; ++q) {
				for (std::size_t k = first; k < first + GuiGameGrid::kPerPage && k < items.size() && !onPage; ++k) {
					onPage = items[k].id == coverQueue[q];
					if (onPage) pick = q;
				}
			}
			if (riftwii::wii::StartCoverFetch(coverQueue[pick])) {
				coverQueue.erase(coverQueue.begin() + static_cast<std::ptrdiff_t>(pick));
				coverNote = tr("Getting covers from GameTDB: {1} left", {std::to_string(coverQueue.size() + 1)});
			}
		}
		// Channels: the banners of the page shown, read from the games' images
		// (a moment each, once: they are kept on the card).
		// Once a page's games have all been looked at, it is not looked at again.
		if (grid.Channels() && grid.PageFirst() != bannerPageDone) {
			const std::size_t first = static_cast<std::size_t>(grid.PageFirst());
			bool read = false;
			for (std::size_t k = first; k < first + GuiGameGrid::kPerPage && k < items.size(); ++k) {
				const HomeEntry& e = entries[k];
				if (e.kind == HomeEntry::Kind::Disc || !g_bannersTried.insert(items[k].id).second) continue;
				if (!riftwii::wii::BannerWanted(items[k].id)) continue;
				std::string why;
				if (StoreEntryBanner(state, e, why)) g_bannerAbsent.erase(items[k].id);
				read = true;
				break;
			}
			if (!read) bannerPageDone = grid.PageFirst();
		}
		// Channels: one icon a turn, the page shown's first (with the next
		// page's column peeking in), then the next page's, then the last's.
		std::string iconId;
		std::unique_ptr<riftwii::wii::BannerPlayer> iconLoaded;
		if (grid.Channels()) {
			const int first = grid.PageFirst(), per = GuiGameGrid::kPerPage;
			const int from[3] = {first, first + per, first - per};
			const int count[3] = {per + 4, per, per};
			for (int r = 0; r < 3 && iconId.empty(); ++r) {
				for (int k = std::max(0, from[r]); k < from[r] + count[r] && k < static_cast<int>(items.size()); ++k) {
					const std::string& id = items[static_cast<std::size_t>(k)].id;
					if (id.empty() || entries[static_cast<std::size_t>(k)].kind == HomeEntry::Kind::Disc ||
					    g_iconIndex.count(id) || g_bannerAbsent.count(id))
						continue;
					iconId = id;
					break;
				}
			}
			if (!iconId.empty()) {
				iconLoaded = LoadIcon(iconId);
				if (!iconLoaded) g_bannerAbsent.insert(iconId);
			}
		}
		// The 3D covers view's pictures once the covers are in (a game
		// without one shows its cover meanwhile): the ones near the focus first.
		if (coverQueue.empty() && grid.Ring() && !g_covers3dOff && !g_coversOff && riftwii::wii::Settings().online &&
		    !riftwii::wii::NetBackgroundBusy() && !riftwii::wii::NetFailed() && riftwii::wii::CoverFetchGame().empty() &&
		    riftwii::wii::Cover3dFetchGame().empty()) {
			for (const int i : grid.ShelfWanted()) {
				const std::string& id = items[static_cast<std::size_t>(i)].id;
				if (!g_covers3dChecked.insert(id).second || !riftwii::wii::Cover3dWanted(id)) continue;
				riftwii::wii::StartCover3dFetch(id);
				break;
			}
		}
		// The shelf's boxes once the covers are in: the ones near the focus first.
		if (coverQueue.empty() && grid.Shelf() && !grid.Ring() && !g_boxesOff && !g_coversOff && riftwii::wii::Settings().online &&
		    !riftwii::wii::NetBackgroundBusy() && !riftwii::wii::NetFailed() && riftwii::wii::CoverFetchGame().empty() &&
		    riftwii::wii::BoxFetchGame().empty()) {
			for (const int i : grid.ShelfWanted()) {
				const std::string& id = items[static_cast<std::size_t>(i)].id;
				if (!g_boxesChecked.insert(id).second || !riftwii::wii::BoxWanted(id)) continue;
				riftwii::wii::StartBoxFetch(id);
				break;
			}
		}
		HaltGui();
		if (iconLoaded) KeepIcon(iconId, std::move(iconLoaded));
		if (!gcBannerArrived.empty()) {
			// The disc's picture gives way to the animated banner at the next turn.
			ForgetIcon(gcBannerArrived);
			g_bannerAbsent.erase(gcBannerArrived);
		}
		// The profiles' pictures on the page shown, read here (the GUI
		// halted) before the grid draws them: a slow SD read then holds
		// this loop, not a frame. One a turn; read ones are kept.
		for (int k = grid.PageFirst(); k < grid.PageFirst() + GuiGameGrid::kPerPage && k < static_cast<int>(items.size()); ++k) {
			const GridItem& item = items[static_cast<std::size_t>(k)];
			if (!item.profile) continue;
			const riftwii::Profile* profile = riftwii::wii::Profiles().find(item.key);
			if (profile && riftwii::wii::ProfilePictureLoad(profile->key(), profile->art)) break;
		}
		if (!arrived.empty()) grid.CoverArrived(arrived);
		if (!boxArrived.empty()) grid.BoxArrived(boxArrived);
		if (!cover3dArrived.empty()) grid.Cover3dArrived(cover3dArrived);
		if (!coverNote.empty()) {
			statusTxt.SetText(coverNote.c_str());
			coverNoteShown = !g_coversOff;  // a failure stays up
		} else if (coverNoteShown && riftwii::wii::CoverFetchGame().empty()) {
			statusTxt.SetText(HomeStatus(state, items).c_str());
			coverNoteShown = false;
		}
		ClearStaleButtons({&filterBtn.button, &settingsBtn.button, &searchBtn.button});
		// A hover name fades in while the pointer rests on its button, and out after.
		const auto fade = [](GuiText& hint, int& a, bool on) {
			a = std::max(0, std::min(255, a + (on ? 40 : -40)));
			hint.SetAlpha(a);
			hint.SetVisible(a > 0);
		};
		fade(filterHint, hintAlpha[0], filterBtn.button.GetState() == STATE::SELECTED);
		fade(settingsHint, hintAlpha[1], settingsBtn.button.GetState() == STATE::SELECTED);
		fade(searchHint, hintAlpha[2], searchBtn.button.GetState() == STATE::SELECTED);
		if (TakeUpdateCheck() && !coverNoteShown) statusTxt.SetText(HomeStatus(state, items).c_str());
		riftwii::wii::TakeUpdatePacks();
		if (grid.Page() != shownPage || grid.Pages() != shownPages) {
			shownPage = grid.Page();
			shownPages = grid.Pages();
			// The shelf has no pages: Minus and Plus move it by 12 games.
			if (shownPages > 1 && !grid.Shelf()) {
				setPageText(shownPage + 1, shownPages);
			} else {
				pageTxt.SetText("");
			}
		}
		std::string nowClock, nowDate;
		ClockText(nowClock, nowDate);
		if (nowClock != clock) {
			clock = nowClock;
			date = nowDate;
			clockTxt.SetText(clock.c_str());
			lcd.SetClock(clock);
			dateTxt.SetText(date.c_str());
		}

		int movedFrom = -1, movedTo = -1;
		if (grid.TakeMove(movedFrom, movedTo)) {
			if (MoveHomeGame(state, items, entries, movedFrom, movedTo))
				toast.Show(tr("Home is in your own order now. Settings > Home order puts it back A to Z."), false);
		}
		if (grid.Carrying() != carryNoteShown) {
			carryNoteShown = grid.Carrying();
			const std::string note = !carryNoteShown ? HomeStatus(state, items)
				: grid.CarryingWithPointer() ? tr("Let go of A: put it down   At a side: turn the page")
				: tr("D-pad: move   A: put it down   B: put it back");
			statusTxt.SetText(note.c_str());
		}
		int clicked = grid.GetClicked();
		bool reopening = false;
		if (!g_reopenBanner.empty()) {
			// Only while Home still shows channels (else it would open the page).
			if (clicked < 0 && grid.Channels()) {
				for (std::size_t i = 0; i < items.size(); ++i)
					if (items[i].key == g_reopenBanner) clicked = static_cast<int>(i);
				reopening = clicked >= 0;
			}
			g_reopenBanner.clear();
		}
		if (clicked >= 0 && static_cast<std::size_t>(clicked) < entries.size()) {
			g_homeFocus = clicked;
			HomeEntry entry = entries[static_cast<std::size_t>(clicked)];
			transition::Rect tile;
			if (!grid.TileRect(clicked, tile.x, tile.y, tile.w, tile.h)) tile = transition::Rect{};
			g_openRect = tile;
			// Channels: the game's banner first, as the Wii Menu opens a
			// channel: out of its tile, and back into it.
			bool fromChannel = false;
			if (grid.Channels() && entry.kind != HomeEntry::Kind::Disc &&
				!g_bannerAbsent.count(items[static_cast<std::size_t>(clicked)].id)) {
				transition::Begin(reopening ? transition::Kind::Fade : transition::Kind::ZoomIn, tile);
				// Another game's banner, from the arrows: read from its image
				// first when it is not on the card yet.
				const BannerLoader loadBanner = [&](int i, std::vector<std::uint8_t>& bytes) {
					if (i < 0 || static_cast<std::size_t>(i) >= entries.size()) return false;
					const HomeEntry& e = entries[static_cast<std::size_t>(i)];
					if (e.kind == HomeEntry::Kind::Disc) return false;
					const std::string& id = items[static_cast<std::size_t>(i)].id;
					if (riftwii::wii::LoadBanner(id, bytes)) return true;
					if (!riftwii::wii::BannerWanted(id)) return false;
					std::string why;
					g_bannersTried.insert(id);
					if (!StoreEntryBanner(state, e, why)) return false;
					g_bannerAbsent.erase(id);
					return riftwii::wii::LoadBanner(id, bytes);
				};
				int shown = clicked;
				const ChannelChoice choice = ShowChannel(shown, static_cast<int>(entries.size()),
					items[static_cast<std::size_t>(clicked)].id, loadBanner,
					[&](int i) { return items[static_cast<std::size_t>(i)].key; });
				const bool go = choice != ChannelChoice::Back;
				g_startOnOpen = choice == ChannelChoice::Start;
				if (choice == ChannelChoice::Page) g_reopenBanner = items[static_cast<std::size_t>(shown)].key;
				if (shown != clicked) {
					// Home follows to the game shown last: its tile lit, its page up.
					clicked = shown;
					g_homeFocus = clicked;
					entry = entries[static_cast<std::size_t>(clicked)];
					grid.Focus(clicked);
					if (!grid.TileRect(clicked, tile.x, tile.y, tile.w, tile.h)) tile = transition::Rect{};
					g_openRect = tile;
				}
				if (!go) {
					if (g_favoritesChanged && g_filter == Filter::Favorites) refresh(true);
					g_favoritesChanged = false;
					transition::Begin(transition::Kind::ZoomOut, tile);
					ResumeGui();
					continue;
				}
				// Settings: the banner's last frame stays up while the game
				// opens. Start: the banner itself plays on (g_lingerBanner).
				if (choice == ChannelChoice::Page) transition::Hold(transition::Kind::Fade);
				fromChannel = true;
			}
			std::string error;
			statusTxt.SetText(entry.kind == HomeEntry::Kind::Disc ? "Reading the disc..." : "Opening the game...");
			ResumeGui();
			bool ok = false;
			if (entry.kind == HomeEntry::Kind::Disc) {
				logf("DISC: probing\n");
				ok = SelectDisc(state, error);
				riftwii::wii::mem::WatchHeap("after the disc probe");
				if (!ok && error.empty()) error = "No disc in the drive";
				if (!ok && error.compare(0, 12, "read disc id") == 0) {
					HaltGui();
					OfferBurnedDisc(error);
					ResumeGui();
				}
			} else if (entry.kind == HomeEntry::Kind::Usb) {
				ok = SelectUsbGame(state, entry.index, error);
			} else if (entry.kind == HomeEntry::Kind::Channel) {
				ok = SelectChannel(state, entry.index, error);
			} else {
				ok = SelectSdGame(state, entry.index, error);
			}
			if (ok && !entry.profile.empty()) {
				const riftwii::Profile* profile = riftwii::wii::Profiles().find(entry.profile);
				if (profile) riftwii::wii::SelectProfile(state, *profile);
			}
			HaltGui();
			if (ok) {
				menu = MENU_HOME;
				g_flightOn = grid.Shelf() && grid.ShelfFlightFrom(clicked, g_flight);
				if (g_flightOn) {
					// Off the shelf and onto the page's cover, the screens
					// crossfading under it.
					SetShelfFlying(g_flight.id);
					transition::SetActor(FlightOut, FlightLanded);
					transition::Begin(transition::Kind::Fade);
				} else if (!fromChannel) {
					transition::Begin(transition::Kind::ZoomIn, tile);
				}
			} else {
				if (fromChannel) transition::Begin(transition::Kind::Fade);
				DropLingerBanner();  // Start on a banner: Home comes back from under it
				g_startOnOpen = false;
				g_reopenBanner.clear();
				logf("Home: %s\n", error.c_str());
				std::string shown = FlatCapped(error, 150);
				if (!shown.empty() && shown[0] >= 'a' && shown[0] <= 'z') shown[0] = static_cast<char>(shown[0] - 'a' + 'A');
				// The error on a notice of its own; the status line goes back to what it says.
				toast.Show(shown, true);
				statusTxt.SetText(HomeStatus(state, items).c_str());
			}
		}
		if (menu != MENU_NONE) {
			// picked
		} else if (exitBtn.GetState() == STATE::CLICKED) {
			exitBtn.ResetState();
			if (ShowHomeMenu() > 0) menu = MENU_EXIT;
		} else if (settingsBtn.Clicked()) {
			g_homeFocus = grid.FocusedIndex();
			LearnHomeHints();
			menu = MENU_OPTIONS;
		} else if (searchBtn.Clicked()) {
			searchBtn.button.ResetState();
			const std::string before = g_search;
			std::string typed = g_search;
			// The games behind the keyboard follow what is typed.
			const auto live = [&](const std::string& words, std::string& matches, std::string& next) -> std::string {
				g_search = TrimSearch(words);
				BuildHome(state, items, entries);
				grid.SetItems(&items);
				grid.Focus(0);
				matches.clear();
				// The keys still worth pressing: a character is live when some
				// game shown now still matches with it typed next (a Space
				// once a word is typed). The list only narrows, so the games
				// shown are the ones to try.
				next.clear();
				static const char kCandidates[] = "abcdefghijklmnopqrstuvwxyz0123456789-'. ";
				for (const char* c = kCandidates; *c; ++c) {
					if (*c == ' ' && (words.empty() || words.back() == ' ')) continue;
					const std::string trial = words + *c;
					for (std::size_t k = 0; k < entries.size(); ++k) {
						const HomeEntry& e = entries[k];
						if (e.searchName.empty()) continue;  // the disc drive's tile
						if (WordsMatch(trial, e.searchName, items[k].id, items[k].badge == "GC")) {
							next += *c;
							break;
						}
					}
				}
				if (g_search.empty()) return "";
				// The keyboard hides the grid: the first few by name
				// (a tester saw only the count until Search).
				constexpr std::size_t kNamed = 5;
				std::size_t games = 0;
				for (const GridItem& item : items) {
					if (item.badge == "DISC") continue;
					if (games++ < kNamed) matches += (matches.empty() ? "" : ", ") + item.title;
				}
				if (games > kNamed) matches += " " + tr("and {1} more", {std::to_string(games - kNamed)});
				return games == 1 ? std::string(tr("1 game")) : tr("{1} games", {std::to_string(games)});
			};
			if (AskSearch(typed, live)) {
				g_search = TrimSearch(typed);
				logf("Home: search \"%s\"\n", g_search.c_str());
			} else {
				g_search = before;
			}
			refresh(false);
		} else if (filterBtn.Clicked()) {
			filterBtn.button.ResetState();
			if (!g_search.empty()) {
				// A search picks from every game: the first press ends it.
				g_search.clear();
				refresh(false);
				ResumeGui();
				continue;
			}
			// Recently played joins the cycle once a game was played, and
			// Favourites once one is marked.
			const bool anyPlayed = riftwii::wii::History().size() != 0;
			const bool anyFavorite = !riftwii::wii::Settings().favorites.empty();
			if (g_filter == Filter::Mods) g_filter = Filter::All;
			else if (g_filter == Filter::All && anyPlayed) g_filter = Filter::Recent;
			else if (g_filter != Filter::Favorites && anyFavorite) g_filter = Filter::Favorites;
			else g_filter = Filter::Mods;
			logf("Home: filter %s\n", FilterLabel(g_filter));
			riftwii::wii::Settings().other["view"] = FilterKey(g_filter);
			riftwii::wii::Settings().other["home_hints"] = "learned";
			riftwii::wii::SaveSettings();
			refresh(false);
		} else if (rescanBtn.GetState() == STATE::CLICKED) {
			rescanBtn.ResetState();
			riftwii::wii::ForgetProfilePictures();
			ScanDrives(state, statusTxt);
			refresh(true);
			// Covers that failed (or were never tried) are asked for again.
			g_coversOff = false;
			g_coversChecked.clear();
			queueCovers();
		}
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

// ---------------------------------------------------------------------------
// Game page

// The game's banner across the top: its own box art, blurred and dimmed
// so the white text on it reads, as TV app stores head a game's page
// (Nintendo eShop, Apple TV). Until the art is on the card (or for a game
// with none), the game's colour with light stripes and the accent line.
class GameBanner : public GuiElement {
public:
	static constexpr int kHeight = 120;
	GameBanner(GXColor hue, std::string id = "", bool disc = false) : hue(hue), id(std::move(id)), disc(disc) {}
	void Draw() override {
		riftwii::wii::DrawTrace trace("the page's band");
		UpdateArt();
		Menu_FillScreen(-1000, 1000 + kHeight, hue);
		const int plainAlpha = static_cast<int>(255 * (1.0f - artShown));
		if (plainAlpha > 0) {
			Menu_Scissor(-1000, -1000, 3000, 1000 + kHeight);
			// Across the whole screen: a widescreen menu is wider than the
			// stripes' 640 (drawn once, the band's sides were bare). Copies
			// side by side, a whole period apart, so they join.
			f32 safeX, safeW;
			Menu_SafeArea(&safeX, &safeW);
			const int w = skin::bannerStripes.w > 0 ? skin::bannerStripes.w : 640;
			for (int x = 0; x > safeX - w; x -= w) Stripes(x, plainAlpha);
			for (int x = w; x < safeX + safeW; x += w) Stripes(x, plainAlpha);
			GX_SetScissor(0, 0, Menu_XfbWidth(), Menu_EfbHeight());
		}
		if (artShown > 0.0f) DrawArt();
		for (int i = 0; i < 4; ++i) Menu_FillScreen(kHeight - 19 + 4 * i, 4, (GXColor){0, 0, 0, static_cast<u8>(10 + 10 * i)});
		Menu_FillScreen(kHeight - 3, 3, skin::WithAlpha(skin::kAccent, plainAlpha));
		// The art's lower edge: a faint light line, as on a glass panel.
		Menu_FillScreen(kHeight - 1, 1, (GXColor){255, 255, 255, static_cast<u8>(40 * artShown)});
		Menu_FillScreen(kHeight, 2, (GXColor){0, 0, 0, 30});
		Menu_FillScreen(kHeight + 2, 3, (GXColor){0, 0, 0, 12});
	}
private:
	// The best art on the card so far (the box's front, its cover, the disc
	// drive's box), made small and soft once (riftwii::make_backdrop), and
	// again when better art arrives; faded in over a third of a second.
	void UpdateArt() {
		const u8* box = id.empty() ? nullptr : riftwii::wii::BoxTexture(id);
		const u8* cover = box || id.empty() ? nullptr : riftwii::wii::CoverTexture(id);
		const int rank = box ? 3 : cover ? 2 : disc && skin::discCase.data ? 1 : 0;
		if (rank > artRank) {
			if (box)
				riftwii::make_backdrop(box, riftwii::kBoxWidth, riftwii::kBoxHeight, riftwii::kBoxSpineWidth,
					riftwii::kBoxSpineWidth + riftwii::kBoxFrontWidth, art);
			else if (cover)
				riftwii::make_backdrop(cover, riftwii::kCoverWidth, riftwii::kCoverHeight, 0, riftwii::kCoverWidth, art);
			else
				riftwii::make_backdrop(skin::discCase.data, skin::discCase.w, skin::discCase.h, 0, skin::discCase.w, art,
					true);
			DCFlushRange(art, sizeof(art));
			GX_InvalidateTexAll();
			artRank = rank;
		}
		if (artRank > 0 && artShown < 1.0f) artShown = std::min(1.0f, artShown + 0.06f);
	}
	// The art across the band, the whole screen wide: the upper part of the
	// picture (a box's title and colours), dimmed to about half, darker
	// towards the bottom where the facts are; then the theme's tint.
	void DrawArt() {
		f32 vx, vy, vw, vh;
		Menu_VisibleArea(&vx, &vy, &vw, &vh);
		const u8 a = static_cast<u8>(255 * artShown);
		skin::DrawRgb5a3Region(art, riftwii::kBackdropWidth, riftwii::kBackdropHeight, 0.0f, 0.06f, 1.0f, 0.46f, vx - 8,
			vy - 8, vw + 16, kHeight - vy + 8, (GXColor){128, 128, 134, a}, (GXColor){92, 92, 98, a});
		const GXColor tint = skin::BannerTint();
		if (tint.a > 0) Menu_FillScreen(-1000, 1000 + kHeight, skin::WithAlpha(tint, a));
	}
	// One copy of the stripes at x, cut to the screen and the band first:
	// the copies at the sides of a widescreen menu ran some 530 units past
	// the screen, and a console draws a polygon that runs far past it
	// wrongly at times (7f1ad1d); a tester's 16:9 TV showed a bright bar
	// where two copies meet. Dolphin draws either way the same.
	void Stripes(int x, int alpha) {
		const skin::Tex& t = skin::bannerStripes;
		if (!t.data) return;
		f32 vx, vy, vw, vh;
		Menu_VisibleArea(&vx, &vy, &vw, &vh);
		const float top = -8, x0 = std::max<float>(x, vx), x1 = std::min<float>(x + t.w, vx + vw);
		const float y0 = std::max(top, vy), y1 = std::min<float>(top + t.h, kHeight);
		if (x1 <= x0 || y1 <= y0) return;
		Menu_DrawImgPart(x0, y0, x1 - x0, y1 - y0, static_cast<u16>(t.w), static_cast<u16>(t.h), t.data,
			(x0 - x) / t.w, (y0 - top) / t.h, (x1 - x) / t.w, (y1 - top) / t.h, alpha);
	}
	GXColor hue;
	std::string id;
	bool disc;
	alignas(32) u8 art[riftwii::kBackdropBytes];
	int artRank = 0;  // 0 none yet, 1 the disc drive's box, 2 the cover, 3 the box's front
	float artShown = 0.0f;
};

// Text over a picture (the game page's band): a drop shadow, a dark
// outline all round, then the text itself, so it reads on any art. The
// text goes at (x, y), from its top left, in `color`.
class OutlinedText : public GuiElement {
public:
	OutlinedText(GuiText& text, int x, int y, GXColor color, int outline, int drop)
		: text(text), x(x), y(y), color(color), outline(outline), drop(drop) {
		text.SetParent(this);
		Place(text, x, y);
	}
	void Draw() override {
		riftwii::wii::DrawTrace trace("the page's text");
		text.SetColor((GXColor){0, 0, 0, 255});
		// The shadow, softer and lower; then the outline: every offset
		// within its radius (8 at 1, 20 at 2).
		text.SetAlpha(110);
		text.SetPosition(x + 1, y + outline + drop);
		text.Draw();
		text.SetAlpha(225);
		for (int dy = -outline; dy <= outline; ++dy)
			for (int dx = -outline; dx <= outline; ++dx) {
				if ((dx == 0 && dy == 0) || dx * dx + dy * dy > outline * outline + 1) continue;
				text.SetPosition(x + dx, y + dy);
				text.Draw();
			}
		text.SetColor(color);
		text.SetAlpha(255);
		text.SetPosition(x, y);
		text.Draw();
	}
private:
	GuiText& text;
	int x, y;
	GXColor color;
	int outline, drop;
};

// A white card at (x, y) drawn from a panel texture.
static std::string SaveLabel(const std::string& mode)
{
	if (mode == "separate") return "SD, from Wii save";
	if (mode == "fresh") return "SD, fresh start";
	if (mode == "emunand") return "SD, emulated NAND";
	return "On the Wii";
}
// The shared emulated NAND's folder for a game, as its settings say. Read
// again for another game or when asked (the choice was just picked); the
// game's own settings file can be long, so not at every focus.
static const riftwii::wii::SharedNandLookup& SharedNandFor(const std::string& game_id, bool read_again)
{
	static std::string lookedUpFor;
	static riftwii::wii::SharedNandLookup found;
	static bool known = false;
	if (!known || read_again || lookedUpFor != game_id) {
		found = riftwii::wii::look_up_shared_nand(game_id);
		lookedUpFor = game_id;
		known = true;
	}
	return found;
}
// The Saves tile's note. "SD, emulated NAND" names the folder, since
// which one (the game's own, the global one or the default) decides which
// save the game finds; a folder the launch would refuse shows here first.
static std::string SaveNote(const riftwii::LaunchModel& model, const std::string& game_id, bool read_again = false)
{
	const std::string owner = model.pack_save_owner();
	if (!owner.empty())
		return "This pack keeps its own saves. Turn it off to choose here.";
	const std::string& mode = model.save_mode;
	if (mode == "separate") return "Saves go to the SD card, starting from the Wii's save.";
	if (mode == "fresh") return "Saves go to the SD card, starting fresh.";
	if (mode == "emunand") {
		const riftwii::wii::SharedNandLookup& shared = SharedNandFor(game_id, read_again);
		if (shared.usable)
			return tr("Saves go to {1} on the SD card, shared with other loaders.", {shared.folder});
		return tr("{1} can't hold saves: use a short folder on the SD card.", {shared.configured});
	}
	return "Saves stay on the Wii, as usual.";
}

struct RowRef {
	enum class What { Mods, Saves, Cheats, Width, Deflicker, Borders, VideoMode, RegionVideo, Aspect, Rumble, Speaker,
		RegionStrings, Language, Cios, Server, Favorite, Pack, Option, Note,
		AddCodes, ForgetCodes, MakeImage, Cover, Memory, About, GcDiscReads, GcCard, GcProgressive, GcWidescreen,
		GcSharp, GcWidth, GcSpeed, GcLanguage, GcControls, SaveTile, RenameTile, RemoveTile } what = What::Note;
	std::size_t pkg = 0, opt = 0;
};

// The game's video settings: "global" follows Settings, the rest are
// riftwii/videopatch.hpp's names.
static const char* const kWidths[] = {"global", "game", "framebuffer", "704", "720"};
static const char* const kDeflickers[] = {"global", "game", "off", "low", "medium", "high"};
static const char* const kBorderModes[] = {"global", "keep", "remove", "remove_all"};
static const char* const kVideoModes[] = {"global", "game", "system", "ntsc", "pal60", "pal50", "480p"};
static const char* const kAspects[] = {"global", "game", "4:3", "16:9"};
static std::string AspectName(const std::string& v)
{
	return v == "4:3" || v == "16:9" ? v : std::string(tr("Game's own"));
}
static const char* const kGameLanguages[] = {"global", "console", "ja", "en", "de", "fr", "es", "it", "nl", "zh-hans",
	"zh-hant", "ko"};
// A GameCube game's picture and language: Settings' and the Wii's own
// first (riftwii::GcConsoleSettings), then the game's choices; the
// languages are the console's GameCube setting, then riftwii::kGcLanguages
// in the SRAM's order.
static const char* const kGcProgressiveChoices[] = {"global", "480p", "game"};
static const char* const kGcWidescreenChoices[] = {"global", "16:9", "game"};
static const char* const kGcLanguageChoices[] = {"global", "game", "english", "german", "french", "spanish", "italian",
	"dutch"};
static std::string GcLanguageName(const std::string& v)
{
	if (v == "english") return tr("English");
	if (v == "german") return tr("German");
	if (v == "french") return tr("French");
	if (v == "spanish") return tr("Spanish");
	if (v == "italian") return tr("Italian");
	if (v == "dutch") return tr("Dutch");
	return tr("The console's");
}
static const char* const kCiosChoices[] = {"global", "auto", "248", "249", "250", "251", "252"};
static const char* const kServers[] = {"global", "off", "wiimmfi", "wiilink", "altwfc", "custom"};

template <std::size_t N>
static std::string StepValue(const char* const (&list)[N], const std::string& value, int direction, bool withGlobal = true)
{
	const int first = withGlobal ? 0 : 1;
	const int n = static_cast<int>(N) - first;
	int at = 0;
	while (at < n && value != list[first + at]) ++at;
	if (at == n) at = 0;
	return list[first + ((at + direction) % n + n) % n];
}

static const char* const kChannelCornerChoices[] = {"theme", "square", "slight", "round", "rounder"};

static std::string ChannelCornersName(const std::string& v)
{
	// Each named with its corners, not a bare "Round" or "Square" another
	// screen might want for something else (a race's round, a button).
	if (v == "square") return tr("Square corners");
	if (v == "slight") return tr("Slightly round");
	if (v == "round") return tr("Round corners");
	if (v == "rounder") return tr("Extra round");
	return tr("Theme");
}

static std::string WidthName(const std::string& v)
{
	if (v == "framebuffer") return tr("Framebuffer");
	if (v == "704") return tr("704 pixels");
	if (v == "720") return tr("720 pixels (full)");
	return tr("Game's own");
}
static std::string DeflickerName(const std::string& v)
{
	if (v == "off") return tr("Off (sharp)");
	if (v == "low") return tr("Low");
	if (v == "medium") return tr("Medium");
	if (v == "high") return tr("High");
	return tr("Game's own");
}
static const char* const kBordersNote = "Remove stretches the picture over the bars at the sides. Remove all also stretches it over the bars at the top and bottom: experimental, some games show a broken picture or crash with it.";
static std::string BordersName(const std::string& v)
{
	if (v == "remove_all") return tr("Remove all (experimental)");
	return v == "remove" ? tr("Remove") : tr("Keep");
}
static std::string BordersShortName(const std::string& v)
{
	if (v == "remove_all") return tr("Remove all");
	return BordersName(v);
}
static std::string VideoModeName(const std::string& v)
{
	if (v == "system") return tr("The console's");
	if (v == "ntsc") return "NTSC (480i)";
	if (v == "pal60") return "PAL 60 Hz";
	if (v == "pal50") return "PAL 50 Hz";
	if (v == "480p") return "480p";
	return tr("Game's own");
}
static std::string GameLanguageName(const std::string& v)
{
	if (v == "ja") return tr("Japanese");
	if (v == "en") return tr("English");
	if (v == "de") return tr("German");
	if (v == "fr") return tr("French");
	if (v == "es") return tr("Spanish");
	if (v == "it") return tr("Italian");
	if (v == "nl") return tr("Dutch");
	if (v == "zh-hans") return tr("Chinese (simplified)");
	if (v == "zh-hant") return tr("Chinese (traditional)");
	if (v == "ko") return tr("Korean");
	return tr("The console's");
}
static std::string CiosName(const std::string& v)
{
	if (v == "auto") return tr("Automatic");
	return "cIOS " + v;
}
static std::string ServerName(const std::string& v)
{
	if (v == "wiimmfi") return "Wiimmfi";
	if (v == "wiilink") return "WiiLink WFC";
	if (v == "altwfc") return "AltWFC";
	if (v == "custom") {
		const std::string& domain = riftwii::wii::Settings().wfc_domain;
		if (!riftwii::valid_wfc_domain(domain)) return tr("Custom (no wfc_domain set)");
		return domain;
	}
	return tr("Off");
}
// A game's value, or "Default (...)" naming what the global setting is.
static std::string GameValue(const std::string& v, const std::string& global, std::string (*name)(const std::string&))
{
	if (v == "global") return tr("Default ({1})", {name(global)});
	return name(v);
}
static std::string CheatsValue(const riftwii::GameSettings& g)
{
	if (!g.cheats) return tr("Off");
	if (g.cheat_names.empty()) return tr("On, none picked");
	return tr("On, {1} picked", {std::to_string(g.cheat_names.size())});
}
static std::string CheatFileShown(const std::string& id)
{
	// Named as the player sees the card on a PC.
	return "SD:/riftwii/cheats/" + id + ".txt";
}

// The packs made for the game, as the Mods row counts them.
static std::size_t ShownPacks(const FrontendState& state, std::size_t* enabled = nullptr)
{
	std::size_t shown = 0, on = 0;
	for (const riftwii::LaunchPackage& p : state.model.packages) {
		if (!riftwii::show_package(p)) continue;
		++shown;
		if (p.valid && p.enabled) ++on;
	}
	if (enabled) *enabled = on;
	return shown;
}

// `settingsOnly`: the Game settings page's rows (the picture, the language
// and the rest), without the ones the game page has as tiles.
static void BuildGameRows(const FrontendState& state, std::vector<FlowRow>& rows, std::vector<RowRef>& refs,
	bool settingsOnly = false)
{
	rows.clear();
	refs.clear();
	// Made in any order, shown in groups (below).
	std::vector<std::pair<FlowRow, RowRef>> made;
	const auto add = [&](FlowRow row, RowRef ref) { made.emplace_back(std::move(row), ref); };
	// Mods first: the reason most games are opened here.
	std::size_t enabled = 0;
	const std::size_t shown = ShownPacks(state, &enabled);
	FlowRow mods;
	mods.kind = FlowRow::Kind::Action;
	mods.label = tr("Mods");
	mods.value = shown == 0 ? tr("None") : enabled == 0 ? tr("Off") : tr("{1} switched on", {std::to_string(enabled)});
	mods.on = enabled != 0;
	add(mods, {RowRef::What::Mods});
	if (!state.game_id.empty()) {
		FlowRow about;
		about.kind = FlowRow::Kind::Action;
		about.label = tr("About this game");
		add(about, {RowRef::What::About});
	}

	// A pack with its own <savegame> decides where the saves go; the
	// setting is shown as the pack's and left alone until it lets go.
	FlowRow saves;
	saves.kind = FlowRow::Kind::Option;
	saves.label = "Saves";
	if (!state.model.pack_save_owner().empty()) {
		saves.value = "Kept by the pack";
		saves.dim = true;
	} else {
		saves.value = SaveLabel(state.model.save_mode);
		saves.on = state.model.save_mode != "nand";
	}
	add(saves, {RowRef::What::Saves});

	// Cheats and the picture: the same for every game, whatever its packs.
	const riftwii::GameSettings& game = state.model.game;
	const riftwii::LoaderSettings& global = riftwii::wii::Settings();
	FlowRow cheats;
	cheats.kind = FlowRow::Kind::Action;
	cheats.label = tr("Cheats");
	cheats.value = CheatsValue(game);
	cheats.on = game.cheats && !game.cheat_names.empty();
	add(cheats, {RowRef::What::Cheats});
	FlowRow width;
	width.kind = FlowRow::Kind::Option;
	width.label = tr("Picture width");
	width.value = GameValue(game.video_width, global.video_width, WidthName);
	width.on = game.video_width != "global";
	add(width, {RowRef::What::Width});
	FlowRow deflicker;
	deflicker.kind = FlowRow::Kind::Option;
	deflicker.label = tr("Deflicker");
	deflicker.value = GameValue(game.deflicker, global.deflicker, DeflickerName);
	deflicker.on = game.deflicker != "global";
	add(deflicker, {RowRef::What::Deflicker});
	FlowRow borders;
	borders.kind = FlowRow::Kind::Option;
	borders.label = tr("Black borders");
	borders.value = game.borders == "global" ? tr("Default ({1})", {BordersShortName(global.borders)})
	                                         : BordersName(game.borders);
	borders.on = game.borders != "global";
	add(borders, {RowRef::What::Borders});
	FlowRow videoMode;
	videoMode.kind = FlowRow::Kind::Option;
	videoMode.label = tr("Video mode");
	videoMode.value = GameValue(game.video_mode, global.video_mode, VideoModeName);
	videoMode.on = game.video_mode != "global";
	add(videoMode, {RowRef::What::VideoMode});
	FlowRow regionVideo;
	regionVideo.kind = FlowRow::Kind::Toggle;
	regionVideo.label = tr("Region video fix");
	regionVideo.on = game.region_video == "on";
	regionVideo.value = regionVideo.on ? tr("On") : tr("Off");
	add(regionVideo, {RowRef::What::RegionVideo});
	FlowRow aspect;
	aspect.kind = FlowRow::Kind::Option;
	aspect.label = tr("Aspect ratio");
	aspect.value = GameValue(game.aspect, global.aspect, AspectName);
	aspect.on = game.aspect != "global";
	add(aspect, {RowRef::What::Aspect});
	FlowRow rumble;
	rumble.kind = FlowRow::Kind::Toggle;
	rumble.label = tr("Rumble");
	rumble.on = game.rumble != "off";
	rumble.value = rumble.on ? tr("On") : tr("Off");
	add(rumble, {RowRef::What::Rumble});
	FlowRow speaker;
	speaker.kind = FlowRow::Kind::Toggle;
	speaker.label = tr("Wii Remote speaker");
	speaker.on = game.speaker != "off";
	speaker.value = speaker.on ? tr("On") : tr("Off");
	add(speaker, {RowRef::What::Speaker});
	FlowRow regionStrings;
	regionStrings.kind = FlowRow::Kind::Toggle;
	regionStrings.label = tr("Region strings fix");
	regionStrings.on = game.region_strings == "on";
	regionStrings.value = regionStrings.on ? tr("On") : tr("Off");
	add(regionStrings, {RowRef::What::RegionStrings});
	FlowRow language;
	language.kind = FlowRow::Kind::Option;
	language.label = tr("Game language");
	language.value = GameValue(game.language, global.game_language, GameLanguageName);
	language.on = game.language != "global";
	add(language, {RowRef::What::Language});
	// Discs run under the menu's IOS reload; the cIOS is for images.
	if (state.use_usb || state.use_sd) {
		FlowRow cios;
		cios.kind = FlowRow::Kind::Option;
		cios.label = "cIOS";
		cios.value = GameValue(game.cios, global.game_cios, CiosName);
		cios.on = game.cios != "global";
		add(cios, {RowRef::What::Cios});
	}
	FlowRow server;
	server.kind = FlowRow::Kind::Option;
	server.label = tr("Online server");
	if (riftwii::game_has_no_online(state.game_id)) {
		// GameTDB says the game never went online (a tester: Wii Sports
		// Resort showed a server to pick): shown, not offered.
		server.value = tr("No online play");
		server.dim = true;
	} else if (state.model.packs_patch_game()) {
		// The launch patches no server while a pack is on (a tester:
		// Retro Rewind and MKW-DX bring their own): shown as the pack's.
		server.value = tr("Left to the pack");
		server.dim = true;
	} else {
		server.value = GameValue(game.server, global.wfc_server, ServerName);
		server.on = game.server != "global";
	}
	add(server, {RowRef::What::Server});
	FlowRow favorite;
	favorite.kind = FlowRow::Kind::Toggle;
	favorite.label = tr("Favourite");
	favorite.on = global.favorites.count(riftwii::wii::TileKey(state)) != 0;
	favorite.value = favorite.on ? tr("On") : tr("Off");
	add(favorite, {RowRef::What::Favorite});
	if (global.online && !state.game_id.empty()) {
		FlowRow cover;
		cover.kind = FlowRow::Kind::Action;
		cover.label = tr("Cover");
		cover.value = riftwii::wii::CoverStored(state.game_id) ? tr("Download again") : tr("Download");
		add(cover, {RowRef::What::Cover});
	}

	// The game's own rows first, then the picture's and the rest under
	// their names (rows the focus passes over).
	using W = RowRef::What;
	const auto show = [&](std::initializer_list<W> whats) {
		for (W what : whats)
			for (const auto& m : made)
				if (m.second.what == what) {
					rows.push_back(m.first);
					refs.push_back(m.second);
				}
	};
	const auto heading = [&](const char* name) {
		FlowRow row;
		row.kind = FlowRow::Kind::Info;
		row.heading = true;
		row.label = name;
		rows.push_back(row);
		refs.push_back({W::Note});
	};
	if (!settingsOnly && state.use_channel) show({W::About, W::Cheats, W::Favorite, W::Cover});
	else if (!settingsOnly) show({W::Mods, W::About, W::Saves, W::Cheats, W::Favorite, W::Cover});
	if (state.use_sd && state.sd_index < state.sd_catalog.games.size() && state.sd_catalog.games[state.sd_index].gamecube) {
		// A GameCube game: none of the Wii's picture or online settings
		// reach it; its own instead.
		if (riftwii::kGcDriveTimingReady) {
			FlowRow discReads;
			discReads.kind = FlowRow::Kind::Option;
			discReads.label = tr("Loading speed");
			discReads.value = game.gc_disc_reads == "fast" ? tr("Fast") : tr("As on a GameCube (slower)");
			discReads.on = game.gc_disc_reads == "drive";
			rows.push_back(discReads);
			refs.push_back({W::GcDiscReads});
		}
		FlowRow card;
		card.kind = FlowRow::Kind::Option;
		card.label = tr("Memory card");
		card.value = game.gc_card == "shared" ? tr("Shared") : tr("This game's own");
		card.on = game.gc_card == "shared";
		rows.push_back(card);
		refs.push_back({W::GcCard});
		FlowRow progressive;
		progressive.kind = FlowRow::Kind::Option;
		progressive.label = tr("480p picture");
		// "global" is shown as what it comes to now, as a Wii game's rows
		// show Settings' choice.
		const riftwii::GcConsoleSettings console = riftwii::wii::GcConsoleNow();
		// 480p is NTSC's: a PAL game keeps its own picture whatever is picked.
		progressive.dim = riftwii::gc_pal_game(state.game_id);
		progressive.on = game.gc_progressive != "global" && !progressive.dim;
		const bool progressiveNow = riftwii::gc_progressive_on(game, state.game_id, console);
		if (progressive.dim) progressive.value = tr("Not for European games");
		else if (game.gc_progressive == "global")
			progressive.value = tr("Default ({1})", {progressiveNow ? std::string(tr("On")) : std::string(tr("Off"))});
		else progressive.value = progressiveNow ? tr("On (480p)") : tr("Off");
		rows.push_back(progressive);
		refs.push_back({W::GcProgressive});
		if (riftwii::kGcWidescreenReady) {
			FlowRow widescreen;
			widescreen.kind = FlowRow::Kind::Option;
			widescreen.label = tr("Widescreen");
			widescreen.on = game.gc_widescreen != "global";
			const std::string widescreenNow = riftwii::gc_widescreen_on(game, console)
				? std::string("16:9") : std::string(tr("Game's own"));
			widescreen.value = game.gc_widescreen == "global" ? tr("Default ({1})", {widescreenNow}) : widescreenNow;
			rows.push_back(widescreen);
			refs.push_back({W::GcWidescreen});
		}
		FlowRow sharp;
		sharp.kind = FlowRow::Kind::Option;
		sharp.label = tr("Sharper picture");
		sharp.on = game.gc_sharp == "on";
		sharp.value = sharp.on ? tr("On") : tr("Off (as made)");
		rows.push_back(sharp);
		refs.push_back({W::GcSharp});
		FlowRow width;
		width.kind = FlowRow::Kind::Option;
		width.label = tr("Picture width");
		width.on = game.gc_width != "global";
		width.value = GameValue(game.gc_width, global.video_width, WidthName);
		rows.push_back(width);
		refs.push_back({W::GcWidth});
		// 50 or 60 Hz is a European game's question only.
		if (riftwii::gc_pal_game(state.game_id)) {
			FlowRow speed;
			speed.kind = FlowRow::Kind::Option;
			speed.label = tr("Game speed");
			speed.on = game.gc_speed == "60";
			speed.value = speed.on ? tr("Smoother (60 Hz)") : tr("As made (50 Hz)");
			rows.push_back(speed);
			refs.push_back({W::GcSpeed});
		}
		FlowRow language;
		language.kind = FlowRow::Kind::Option;
		language.label = tr("Game language");
		language.on = game.gc_language != "global";
		const int languageNumber = riftwii::gc_language_number(game, console);
		const std::string languageNow = GcLanguageName(languageNumber >= 0 ? riftwii::kGcLanguages[languageNumber] : "game");
		language.value = game.gc_language == "global" ? tr("Default ({1})", {languageNow}) : languageNow;
		rows.push_back(language);
		refs.push_back({W::GcLanguage});
		FlowRow controls;
		controls.kind = FlowRow::Kind::Action;
		controls.label = tr("Controls");
		rows.push_back(controls);
		refs.push_back({W::GcControls});
		if (settingsOnly) show({W::Cover});
		return;
	}
	heading(tr("Picture"));
	show({W::Width, W::Deflicker, W::Borders, W::VideoMode, W::RegionVideo, W::Aspect});
	heading(tr("Other"));
	show({W::Language, W::RegionStrings, W::Cios, W::Server, W::Rumble, W::Speaker});
	if (settingsOnly) show({W::Cover});
	// What RiftWii's extras take of the game's memory this time.
	const riftwii::ExtrasBudget budget = riftwii::wii::EstimateExtras(state);
	if (!budget.pieces.empty() && !state.use_channel) {
		FlowRow memory;
		// An action row only to show its value (A does nothing); its
		// chip lights up when it is a lot.
		memory.kind = FlowRow::Kind::Action;
		memory.label = tr("Memory for RiftWii's extras");
		memory.value = tr("about {1} KB", {std::to_string(riftwii::whole_kb(budget.total()))});
		if (budget.heavy()) memory.value += std::string(" ") + tr("(a lot)");
		memory.on = budget.heavy();
		rows.push_back(memory);
		refs.push_back({W::Memory});
	}
}

// "1.2 GB", "640 MB".
static std::string SizeText(std::uint64_t bytes)
{
	const std::uint64_t mb = (bytes + (1 << 20) - 1) >> 20;
	if (mb < 1024) return std::to_string(mb) + " MB";
	const std::uint64_t tenths = mb * 10 / 1024;
	return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " GB";
}

// Stop pressed in `box`: asked once more, with `question` and `body`, as
// the work so far is thrown away. Stop is the first button (A, or the
// pointer): the second takes B and HOME, so a B pressed twice in a hurry
// keeps the work going. True: stop.
static bool ConfirmStop(PopupBox& box, const std::string& question, const std::string& body)
{
	const bool stop = ShowPopup(question, body, tr("Stop"), tr("Keep going")) == 0;
	box.Reactivate();
	return stop;
}

// Makes an image of the code build `p` on the SD card (wii/menu/vsdmake.hpp),
// asking first. True when it is made, with `key` the build's key in it.
static bool MakeSdImage(const FrontendState& state, const riftwii::LaunchPackage& p, std::string& key)
{
	riftwii::wii::VsdMakePlan plan;
	std::string error;
	bool ok;
	{
		PopupBox box(tr("SD image"), tr("Looking at the build's files..."));
		ResumeGui();
		ok = riftwii::wii::PlanVsdMake(p.file, state.game_id, plan, error);
		HaltGui();
	}
	if (!ok) {
		ShowPopup(tr("No SD image made"), error, tr("OK"));
		return false;
	}
	std::string what;
	for (std::size_t i = 0; i < plan.tops.size(); ++i)
		what += (i == 0 ? std::string() : i + 1 == plan.tops.size() ? std::string(tr(" and ")) : std::string(", ")) + plan.tops[i];
	// About 3 MB a second, reading and writing the same card.
	const unsigned minutes = static_cast<unsigned>(plan.plan.image_bytes / (3ull << 20) / 60) + 1;
	std::string body = tr("RiftWii copies {1} into sd:/riftwii/{2} ({3}). The game then gets the image as its SD card, so it can be on the SD card too. This takes about {4} minutes.",
		{what, plan.image, SizeText(plan.plan.image_bytes), std::to_string(minutes)});
	if (plan.parts) body += std::string(" ") + tr("It is saved in {1} parts, as a FAT32 card holds no file of 4 GB.", {std::to_string(plan.parts)});
	if (plan.replaces) body += std::string(" ") + tr("It replaces the {1} there now.", {plan.image});
	if (ShowPopup(tr("Make an SD image?"), body, tr("Make it"), tr("Cancel")) != 0) return false;

	logf("Image: making %s from %s\n", plan.image.c_str(), p.file.c_str());
	{
		PopupBox box(tr("Making {1}", {plan.image}), "", "", tr("Stop"));
		box.CenterCancel();  // its only button, in the middle
		ProgressPanel progress(box, state.game_id);
		std::string shown;
		ResumeGui();
		ok = riftwii::wii::MakeVsdImage(plan, [&](std::uint64_t written, const std::string& path) {
			if (!path.empty()) shown = path;
			HaltGui();
			progress.Set(written, plan.plan.image_bytes);
			progress.SetStage(SizeText(written) + " / " + SizeText(plan.plan.image_bytes) + "\n" +
				FlatCapped(shown.empty() ? std::string(tr("Free space")) : shown, 44));
			const bool stop = box.Pressed() &&
				ConfirmStop(box, tr("Stop making {1}?", {plan.image}),
					tr("What is made so far is deleted. It can be made again later, from the start."));
			ResumeGui();
			return !stop;
		}, error);
		HaltGui();
	}
	if (!ok) {
		ShowPopup(tr("No SD image made"),
			error == "stopped" ? std::string(tr("Stopped. Nothing was kept."))
				: tr("{1} could not be made: {2}. Nothing was kept.", {plan.image, FlatCapped(error, 140)}),
			tr("OK"));
		return false;
	}
	key = plan.image_key();
	ShowPopup(tr("SD image made"),
		tr("{1} is in sd:/riftwii, and the build in it is turned on: the game gets the image as its SD card.",
			{plan.image}),
		tr("OK"));
	return true;
}

// The game picked, when it is a GameCube one (on the SD card).
static const riftwii::wii::ImageGame* SelectedGcGame(const FrontendState& state)
{
	if (!state.use_sd || state.sd_index >= state.sd_catalog.games.size()) return nullptr;
	const riftwii::wii::ImageGame& game = state.sd_catalog.games[state.sd_index];
	return game.gamecube ? &game : nullptr;
}

// The game picked, when it is a GameCube game still to be made from its
// patch (wii/launch/gcpatch.hpp).
static const riftwii::wii::ImageGame* SelectedGcPatch(const FrontendState& state)
{
	const riftwii::wii::ImageGame* game = SelectedGcGame(state);
	return game && !game->gc_patch_source.empty() ? game : nullptr;
}

static std::string ImageGameName(const riftwii::wii::ImageGame& game)
{
	if (!game.display.empty()) return game.display;
	return game.title.empty() ? game.id : game.title;
}

// The game a patch makes its game from, when it is listed.
static const riftwii::wii::ImageGame* GcOriginal(const FrontendState& state, const riftwii::wii::ImageGame& patch)
{
	for (const riftwii::wii::ImageGame& game : state.sd_catalog.games)
		if (game.path == patch.gc_patch_source) return &game;
	return nullptr;
}

// Building a game from its patch reads the original and writes the new
// game on the same card: about 4 MB a second here (Super Mario Eclipse,
// 1.4 GB, in 5 to 6 minutes), but SD cards differ a lot (an old class 4
// card, a nearly full one), so a range: 5 down to 2.5 MB a second. The
// progress window says the measured time once it runs.
static std::string GcBuildTime(std::uint64_t bytes)
{
	const unsigned fast = static_cast<unsigned>((bytes / (5ull << 20) + 59) / 60);
	const unsigned slow = static_cast<unsigned>((bytes * 2 / (5ull << 20) + 59) / 60);
	if (slow <= 1) return tr("About a minute");
	return tr("About {1}-{2} minutes", {std::to_string(std::max(1u, fast)), std::to_string(slow)});
}

// Room and time for the Play tile and the box that asks: "1.4 GB · about
// 6 minutes", from the original's size (0 when it cannot be opened).
static std::uint64_t GcBuildBytes(const riftwii::wii::ImageGame& patch)
{
	riftwii::wii::GcImage image{patch.path, patch.id, patch.gc_patch_source};
	std::uint64_t need = 0, have = 0;
	if (!riftwii::wii::GcPatchSpace(image, need, have)) return 0;
	return need;
}

// The box that asks before a game is made from its patch: the original's
// cover, an arrow, the new game's; what it does, what it needs; and, in
// place of the question, what is missing when it cannot be made. True:
// make it.
static bool AskGcBuild(const std::string& name, const std::string& originalName, const std::string& originalId,
		       const std::string& id, std::uint64_t need, std::uint64_t have, bool originalThere)
{
	const bool roomy = have == 0 || have >= need;
	std::string title, body;
	if (!originalThere) {
		title = tr("{1} can't be built", {name});
		body = tr("It is built from {1}, and RiftWii cannot open it on the SD card any more. Put it back in sd:/games and try again.",
			{originalName});
	} else if (!roomy) {
		title = tr("{1} can't be built", {name});
		body = tr("Building it needs {1} free on the SD card, and only {2} is free. Free some space and press Start again.",
			{SizeText(need), SizeText(have)});
	} else {
		title = tr("Build this game first?");
		body = tr("Build {1} from your {2}? This happens once.", {name, originalName});
	}
	// The facts only when they are known: without the original there is no size.
	if (originalThere)
		body += "\n\n" + tr("Needs {1}", {SizeText(need)}) + " \xC2\xB7 " + GcBuildTime(need) + " \xC2\xB7 " + tr("Once only");
	const bool canBuild = originalThere && roomy;
	PopupBox box(title, body, canBuild ? tr("Build it") : tr("OK"), canBuild ? tr("Not now") : "");
	// The covers on the right, the text beside them.
	const int coversX = 584 + box.Extra() - 2 * 64 - 28;
	const int coversY = PopupBox::kBodyY + 6;
	SmallCover from(originalId, coversX, coversY, 64, 90);
	SmallCover to(id, coversX + 64 + 28, coversY, 64, 90);
	// The theme's own right arrow (the banner view's), at about the gap's
	// width; skin::Draw scales about the picture's middle, so its 44x44 box
	// is placed with its middle on the gap's and the covers' middles.
	SkinMark arrow(skin::arrowRight, coversX + 64 + 14 - 22, coversY + 45 - 22, 0.6f);
	// The text runs from 56 - Extra() to 16 short of the covers; Add counts
	// Extra() in itself.
	box.Add(&from, coversX - 16 - 56);
	box.Append(&to);
	box.Append(&arrow);
	return box.Wait() == 0 && canBuild;
}

// A GameCube game that is still to be made from its patch (gcpatch.hpp)
// is made here, with the menu's own box, before the launch screen (which
// cannot show translations) takes over. True: the game is ready (or was
// never a patch); false: the player stopped it or it failed, and the
// popup said why.
static bool PrepareGcGameInMenu(FrontendState& state)
{
	if (!SelectedGcPatch(state)) return true;
	riftwii::wii::ImageGame& game = state.sd_catalog.games[state.sd_index];
	const std::string name = ImageGameName(game);
	const riftwii::wii::ImageGame* original = GcOriginal(state, game);
	const std::string originalName = original ? ImageGameName(*original) : FlatCapped(game.gc_patch_source, 40);
	const std::string originalId = original ? original->id : std::string();

	riftwii::wii::GcImage image{game.path, game.id, game.gc_patch_source};
	std::uint64_t need = 0, have = 0;
	const bool originalThere = riftwii::wii::GcPatchSpace(image, need, have);
	if (!originalThere) logf("GameCube: %s cannot be made: the original %s cannot be opened\n", game.id.c_str(), image.patch_source.c_str());
	else if (have != 0 && have < need)
		logf("GameCube: %s needs %llu MB free on the SD card, %llu MB are free\n", game.id.c_str(),
			static_cast<unsigned long long>(need >> 20), static_cast<unsigned long long>(have >> 20));
	if (!AskGcBuild(name, originalName, originalId, game.id, need, have, originalThere)) {
		if (originalThere && (have == 0 || have >= need)) logf("GameCube: the player did not want %s made now\n", game.id.c_str());
		return false;
	}

	for (;;) {
		std::string error;
		bool ok;
		{
			PopupBox box(tr("Building {1}", {name}), "", "", tr("Stop"));
			box.CenterCancel();  // its only button, in the middle
			ProgressPanel progress(box, game.id);
			progress.SetStage(tr("Reading {1} and writing {2}", {originalName, name}));
			u64 last_redraw = 0;
			ResumeGui();
			ok = riftwii::wii::PrepareGcPatch(image, error, [&](std::uint64_t done, std::uint64_t total) {
				// A few redraws a second: each one halts the GUI, which costs copy time.
				const u64 now = gettime();
				if (last_redraw != 0 && diff_msec(last_redraw, now) < 400) return true;
				last_redraw = now;
				HaltGui();
				// Minutes of work with no button pressed: not idle, so the
				// screen does not dim over the bar.
				lastActive = now;
				progress.Set(done, total);
				const bool stop = box.Pressed() &&
					ConfirmStop(box, tr("Stop building {1}?", {name}),
						tr("What is built so far is deleted. It can be built again later, from the start."));
				ResumeGui();
				return !stop;
			});
			HaltGui();
		}
		if (ok) break;
		logf("GameCube: %s was not made: %s\n", game.id.c_str(), error.c_str());
		if (error == "stopped") {
			ShowPopup(tr("{1} not built", {name}), tr("Stopped. Nothing was kept."), tr("OK"));
			return false;
		}
		// Most failures are the card's (a write, a read): worth another go.
		if (ShowPopup(tr("{1} not built", {name}),
			    tr("{1} could not be built: {2}. Nothing was kept.", {name, FlatCapped(error, 140)}),
			    tr("Try again"), tr("OK")) != 0)
			return false;
		image = riftwii::wii::GcImage{game.path, game.id, game.gc_patch_source};
	}
	// The entry now is the prepared image, so the launch starts it as any other.
	game.path = image.path;
	game.gc_patch_source.clear();
	return true;
}

// The controllers a game can see, connected now (PadsConnectedNow, with
// About this game).
struct ConnectedPads {
	bool remote = false, nunchuk = false, classic = false, pro = false, gamecube = false, board = false;
};
static ConnectedPads PadsConnectedNow();

// One controller's buttons as the GameCube controller's, in two columns.
// Each row reads the same way: the badges of the buttons in the player's
// hand, an arrow, then the GameCube button they are. A badge is Kenney's
// picture where there is one (as the prompts draw it), RiftWii's own for
// the Control Stick and the D-pad, the stick's picture with L or R on it
// for a Classic Controller's sticks, or the theme's round badge with the
// button's name. A column's arrows line up, so the controller's side and
// the GameCube's read as columns of their own.
class GcControlsTable : public GuiElement {
public:
	// `buttons`: one button name or more, apart by spaces ("ZL ZR": both
	// are the one GameCube button). "LS" and "RS": a Classic Controller's
	// left and right sticks.
	struct Pair { std::string buttons, gamecube; };
	GcControlsTable(const std::vector<Pair>& pairs, int left, int right) : left(left), right(right),
		arrow("\xE2\x86\x92", 18, skin::kInkSoft)  // U+2192, in the leave line's quieter colour
	{
		arrow.SetAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
		for (const Pair& p : pairs) {
			Row row;
			std::size_t start = 0;
			while (start < p.buttons.size()) {
				std::size_t end = p.buttons.find(' ', start);
				if (end == std::string::npos) end = p.buttons.size();
				row.badges.push_back(MakeBadge(p.buttons.substr(start, end - start)));
				start = end + 1;
			}
			row.name.reset(new GuiText(p.gamecube.c_str(), 18, skin::kInk));
			row.name->SetAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
			rows.push_back(std::move(row));
		}
	}
	void Draw() override
	{
		// Hidden while another controller's card is shown.
		if (!IsVisible()) return;
		const int column = (right - left) / 2;
		const int perColumn = static_cast<int>((rows.size() + 1) / 2);
		// Each column's arrows sit just past its widest group of badges.
		int arrowOffset[2] = {0, 0};
		for (std::size_t i = 0; i < rows.size(); ++i) {
			const int col = static_cast<int>(i) / perColumn;
			arrowOffset[col] = std::max(arrowOffset[col], BadgesWidth(rows[i]) + kGap);
		}
		const int arrowWidth = arrow.GetTextWidth();
		for (std::size_t i = 0; i < rows.size(); ++i) {
			// Down the first column, then the second.
			const int col = static_cast<int>(i) / perColumn, line = static_cast<int>(i) % perColumn;
			const int x = left + col * column, y = kTop + line * kRow;
			int badgeX = x;
			for (Badge& badge : rows[i].badges) {
				DrawBadge(badge, badgeX, y);
				badgeX += kBadge + kBadgeGap;
			}
			const int arrowX = x + arrowOffset[col];
			arrow.SetPosition(arrowX, y + kBadge / 2 - 1);
			arrow.Draw();
			rows[i].name->SetPosition(arrowX + arrowWidth + kGap, y + kBadge / 2);
			rows[i].name->Draw();
		}
	}
	// Five rows (a Classic Controller's ten buttons) and the leave line
	// under them fit the popup's body, from its top (PopupBox::kBodyY) to
	// kLeaveY: rows a little closer than the pictures' 24 (they have a
	// clear margin of their own). A picture smaller than 24 would be too
	// small to read, and GX takes no size that is not a multiple of 4.
	static constexpr int kBadge = 24, kTop = PopupBox::kBodyY, kRow = 23;
	static constexpr int kLeaveY = PopupBox::kButtonY - 10 - 18;  // its 15-point line ends above the buttons
private:
	static constexpr int kBadgeGap = 4, kGap = 8;
	struct Badge {
		std::string button;
		std::unique_ptr<GuiText> letters;  // on the theme's badge or a stick's picture; none on Kenney's
	};
	struct Row {
		std::vector<Badge> badges;
		std::unique_ptr<GuiText> name;
	};
	static bool IsClassicStick(const std::string& button) { return button == "LS" || button == "RS"; }
	static Badge MakeBadge(const std::string& button)
	{
		Badge badge;
		badge.button = button;
		if (IsClassicStick(button)) {
			// The stick's L or R, small enough for the middle of its picture.
			badge.letters.reset(new GuiText(button.substr(0, 1).c_str(), 12, skin::kCard));
		} else if (!skin::ButtonIcon(button, kBadge).data) {
			badge.letters.reset(new GuiText(button.c_str(), 14, skin::kCard));
		}
		if (badge.letters) badge.letters->SetAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
		return badge;
	}
	static int BadgesWidth(const Row& row)
	{
		const int count = static_cast<int>(row.badges.size());
		return count * kBadge + (count - 1) * kBadgeGap;
	}
	static void DrawBadge(Badge& badge, int x, int y)
	{
		if (IsClassicStick(badge.button)) {
			// Beside the Classic Controller's other buttons, which are the
			// theme's badges and fill all kBadge: the stick's picture a
			// size up (its disc fills 3/4 of it) to be as big, which also
			// leaves room for the letter.
			const int size = kBadge * 4 / 3, margin = (size - kBadge) / 2;
			skin::Draw(skin::ButtonIcon("Stick", size), x - margin, y - margin);
		} else {
			const skin::Tex& icon = skin::ButtonIcon(badge.button, kBadge);
			skin::Draw(icon.data ? icon : skin::promptButton, x, y);
		}
		if (badge.letters) {
			badge.letters->SetPosition(x + kBadge / 2, y + kBadge / 2 - 1);
			badge.letters->Draw();
		}
	}
	std::vector<Row> rows;
	int left, right;
	GuiText arrow;
};

// One kind of controller's GameCube buttons, and how it leaves the game.
struct GcControlsCard {
	std::string title;
	std::vector<GcControlsTable::Pair> pairs;  // none: a GameCube controller's own
	std::string leave;
};

// Which buttons are the GameCube controller's (runtime/gc/gc_bt.c's
// bt_fill_one), one card per kind of controller connected now (a Wii
// Remote alone when none is). A Wii Remote alone leaves with HOME and -
// together, as its HOME alone is the game's Y.
static std::vector<GcControlsCard> GcControlsCards()
{
	std::vector<GcControlsCard> cards;
	const ConnectedPads pads = PadsConnectedNow();
	const std::string leaveHome = tr("Hold HOME for two seconds to leave the game.");
	if (pads.classic)
		cards.push_back({tr("Classic Controller"),
			{{"a", "A"}, {"b", "B"}, {"x", "X"}, {"y", "Y"}, {"L", "L"}, {"R", "R"}, {"ZL ZR", "Z"}, {"+", "Start"},
				{"LS", tr("Stick")}, {"RS", tr("C-stick")}}, leaveHome});
	if (pads.pro)
		cards.push_back({tr("Wii U Pro Controller"),
			{{"a", "A"}, {"b", "B"}, {"x", "X"}, {"y", "Y"}, {"L", "L"}, {"R", "R"}, {"ZL ZR", "Z"}, {"+", "Start"},
				{"LS", tr("Stick")}, {"RS", tr("C-stick")}}, leaveHome});
	if (pads.nunchuk)
		cards.push_back({tr("Wii Remote and Nunchuk"),
			{{"A", "A"}, {"B", "B"}, {"1", "X"}, {"2", "Y"}, {"C", "L"}, {"Z", "R"}, {"-", "Z"}, {"+", "Start"},
				{"Stick", tr("Stick")}, {"D-pad", tr("C-stick")}}, leaveHome});
	if ((pads.remote && !pads.nunchuk && !pads.classic) || (!pads.remote && !pads.gamecube))
		cards.push_back({tr("Wii Remote, held sideways"),
			{{"2", "A"}, {"1", "B"}, {"A", "X"}, {"HOME", "Y"}, {"B", "R"}, {"-", "Z"}, {"+", "Start"},
				{"D-pad", tr("Stick")}}, tr("Hold HOME and - for two seconds to leave the game.")});
	if (pads.gamecube)
		cards.push_back({tr("GameCube controller"), {}, tr("Hold L, R, Z and Start for two seconds to leave the game.")});
	return cards;
}

// Dots under a popup's title, one per card, the one shown filled.
static std::string CardDots(std::size_t count, std::size_t shown)
{
	std::string dots;
	for (std::size_t i = 0; i < count; ++i) {
		if (i) dots += " ";
		dots += i == shown ? "\xE2\x97\x8F" : "\xE2\x97\x8B";  // U+25CF, U+25CB
	}
	return dots;
}

// The controls cards in one popup: Left and Right (or the arrows) turn to
// the other controllers' cards, the dots say which is shown. `starting`:
// before a game starts, with Play and Back; true when Play was pressed.
// Otherwise OK alone.
static bool ShowGcControls(bool starting)
{
	const std::vector<GcControlsCard> cards = GcControlsCards();
	const std::string ownButtons = tr("Its buttons are the game's own, as on a GameCube.");
	const auto title = [&](std::size_t i) { return std::string(tr("GameCube controls: {1}", {cards[i].title})); };
	const auto body = [&](std::size_t i) { return cards[i].pairs.empty() ? ownButtons : std::string(); };
	PopupBox box(title(0), body(0), starting ? tr("Play") : tr("OK"), starting ? tr("Back") : "");
	const int left = 56 - box.Extra(), right = 584 + box.Extra();
	// Every card's table and leave line made at once; only the shown
	// card's are visible.
	std::vector<std::unique_ptr<GcControlsTable>> tables;
	std::vector<std::unique_ptr<GuiText>> leaves;
	for (const GcControlsCard& card : cards) {
		tables.emplace_back(new GcControlsTable(card.pairs, left, right));
		leaves.emplace_back(new GuiText(card.leave.c_str(), 15, skin::kInkSoft));
		Place(*leaves.back(), left, GcControlsTable::kLeaveY);
		box.Append(tables.back().get());
		box.Append(leaves.back().get());
	}
	// The dots in the body's top right corner, beside the table's short
	// first row: in the title band they ran into a long title.
	GuiText dots(CardDots(cards.size(), 0).c_str(), 14, skin::kInkSoft);
	dots.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	dots.SetPosition(-left, PopupBox::kBodyY);
	// The arrows on the box's sides, level with the middle of its body.
	const int arrowY = (PopupBox::kBodyY + PopupBox::kButtonY - 10) / 2 - skin::arrowLeft.h / 2;
	SkinButton prevBtn(skin::arrowLeft, skin::arrowLeftOver, 2, 2 - box.Extra(), arrowY, nullptr,
		WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT, PAD_BUTTON_LEFT, WIIDRC_BUTTON_LEFT);
	SkinButton nextBtn(skin::arrowRight, skin::arrowRightOver, 2, 594 + box.Extra(), arrowY, nullptr,
		WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT, PAD_BUTTON_RIGHT, WIIDRC_BUTTON_RIGHT);
	if (cards.size() > 1) {
		box.Append(&dots);
		box.Append(&prevBtn.button);
		box.Append(&nextBtn.button);
	}
	std::size_t shown = 0;
	const auto show = [&](std::size_t i) {
		shown = i;
		for (std::size_t c = 0; c < cards.size(); ++c) {
			tables[c]->SetVisible(c == i);
			leaves[c]->SetVisible(c == i);
		}
		box.SetTitleFitted(title(i));
		box.SetBody(body(i));
		dots.SetText(CardDots(cards.size(), i).c_str());
	};
	show(0);
	ResumeGui();
	int choice = -1;
	while (choice < 0) {
		usleep(20000);
		HaltGui();
		ClearStaleButtons({&prevBtn.button, &nextBtn.button});
		if (prevBtn.Clicked() || nextBtn.Clicked()) {
			const std::size_t n = cards.size();
			show(prevBtn.Clicked() ? (shown + n - 1) % n : (shown + 1) % n);
			prevBtn.button.ResetState();
			nextBtn.button.ResetState();
		}
		choice = box.Poll();
		if (choice < 0) ResumeGui();
	}
	return choice == 0;
}

// The leave line for the controller a player would hold first (the
// controls cards' first one), for a note shown before the game starts.
static std::string GcLeaveLine()
{
	const std::vector<GcControlsCard> cards = GcControlsCards();
	return cards.empty() ? std::string(tr("Hold HOME for two seconds to leave the game.")) : cards.front().leave;
}

// Before a game's memory card changes between its own and the shared one.
static bool AskGcCardSwitch()
{
	return ShowPopup(tr("Memory card"), tr("Switching copies no saves: they stay on the other card. Move them with Copy to the SD card and Add a save from the SD card on the Saves page. Japanese games have a shared card of their own."),
		tr("Switch"), tr("Cancel")) == 0;
}

// 480p picked with no component cable seen: said once, as the TV may not
// take it. True: Turn on.
static bool AskGc480p()
{
	const std::string body = std::string(tr("Only with a component or HDMI cable: without one the picture stays black.")) +
		" " + GcLeaveLine();
	return ShowPopup(tr("480p picture"), body, tr("Turn on"), tr("Cancel")) == 0;
}

// The controls before the first GameCube game starts from this card
// (sd:/riftwii/gc/controls_seen.txt remembers). False: Back.
static bool ShowGcControlsOnce(const FrontendState& state)
{
	static const char* const kMarker = "sd:/riftwii/gc/controls_seen.txt";
	if (!SelectedGcGame(state)) return true;
	struct stat st;
	// Without a card nothing could remember it: it would show every start.
	if (stat("sd:/", &st) != 0 || stat(kMarker, &st) == 0) return true;
	if (!ShowGcControls(true)) return false;
	mkdir("sd:/riftwii", 0777);
	mkdir("sd:/riftwii/gc", 0777);
	if (FILE* f = std::fopen(kMarker, "w")) {
		std::fprintf(f, "shown\n");
		std::fclose(f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// A GameCube game's memory card (riftwii/gccard.hpp): its saves, copied to
// the SD card as .gci files (Dolphin's and Nintendont's), deleted, or
// added from the SD card.

static const char* const kGcSavesDir = "sd:/riftwii/gc/saves";
// Where the player copies Dolphin's GC folder (riftwii/gcimport.hpp).
static const char* const kGcDolphinDir = "sd:/riftwii/gc/dolphin";

static std::string GameTitle(const FrontendState& state);

// The game's card, named as the loader names it (gcnative.cpp): its own
// file, or the shared one when the game's Memory card option says so.
static bool GcCardShared(const FrontendState& state)
{
	return state.model.game.gc_card == "shared";
}

static std::string GcCardPath(const FrontendState& state)
{
	return riftwii::gc_card_path(state.game_id, GcCardShared(state));
}

// Whether the card the game will use has no block or no directory slot
// left (riftwii::GcCardSpace::full), read from its first five blocks so
// the game page opens as fast as before. False without a card yet, or
// with one that cannot be read (its own page says why).
static bool ReadGcCardSpace(const std::string& path, riftwii::GcCardSpace& space)
{
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) return false;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<std::uint8_t> head(riftwii::kGcCardSystemBlocks * riftwii::kGcCardBlockBytes);
	const bool read = size >= static_cast<long>(head.size()) && std::fread(head.data(), 1, head.size(), f) == head.size();
	std::fclose(f);
	std::string why;
	return read && riftwii::gc_card_space(head, static_cast<std::uint64_t>(size), space, why);
}

static bool GcCardFull(const FrontendState& state)
{
	riftwii::GcCardSpace space;
	return ReadGcCardSpace(GcCardPath(state), space) && space.full();
}

// What the game page says about a full card: that the game can't save, and
// how to make room (on a shared card, also the way out to its own card).
// Both fit the page's two note lines at 16:9, beside the button hints.
static std::string GcCardFullNote(const FrontendState& state)
{
	return GcCardShared(state)
		? tr("The card is full: this game can't save. Make room on the Memory Card page or use its own card.")
		: tr("The memory card is full: this game can't save. Delete or copy saves on the Memory Card page to make room.");
}

// The icons of a card's saves as textures (32x32 RGB5A3, 2 KB each) for
// the rows of the Memory Card page; freed with the page.
class GcIconTextures {
public:
	GcIconTextures() = default;
	GcIconTextures(const GcIconTextures&) = delete;
	GcIconTextures& operator=(const GcIconTextures&) = delete;
	~GcIconTextures() { Clear(); }
	void Clear()
	{
		for (u8* buffer : buffers) free(buffer);
		buffers.clear();
	}
	// The save's first icon frame, or null: no icon, or one that does not
	// fit in its save (a damaged card), or no memory left for it.
	const u8* Make(const std::vector<std::uint8_t>& card, unsigned slot)
	{
		std::vector<std::uint8_t> rgba;
		std::string why;
		if (!riftwii::gc_card_icon(card, slot, rgba, why)) return nullptr;
		const std::vector<std::uint8_t> tiles = riftwii::gc_icon_rgb5a3(rgba);
		if (tiles.empty()) return nullptr;
		u8* buffer = static_cast<u8*>(memalign(32, tiles.size()));
		if (!buffer) return nullptr;
		memcpy(buffer, tiles.data(), tiles.size());
		DCFlushRange(buffer, tiles.size());
		buffers.push_back(buffer);
		return buffer;
	}
private:
	std::vector<u8*> buffers;
};

// A whole file of at most `limit` bytes.
static bool ReadSmallFile(const std::string& path, std::size_t limit, std::vector<std::uint8_t>& out)
{
	FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) return false;
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	bool ok = size >= 0 && static_cast<std::size_t>(size) <= limit;
	if (ok) {
		out.resize(static_cast<std::size_t>(size));
		ok = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
	}
	std::fclose(f);
	return ok;
}

// What a card change is, which sets the order its blocks reach the SD card.
enum class CardChange { Import, Delete };

// Writes back only the card's blocks that changed: the library writes an
// update into the copies not in use, so a write cut off halfway leaves the
// old directory and block table in charge, which rewriting the whole file
// would not. The groups go in the library's own order (gccard.cpp), each
// flushed to the card before the next: for an import the save's blocks,
// then the BAT, then the directory, so a directory entry never points at
// blocks not written yet; for a delete the directory, then the BAT, so a
// cut-off delete leaves blocks nobody uses rather than a save on free ones.
static bool WriteChangedBlocks(const std::string& path, const std::vector<std::uint8_t>& before,
			       const std::vector<std::uint8_t>& after, CardChange change)
{
	if (before.size() != after.size()) return false;
	FILE* f = std::fopen(path.c_str(), "r+b");
	if (!f) return false;
	constexpr std::size_t kBlock = 0x2000;
	// Block 0 is the header (changed by neither), 1-2 the directory, 3-4
	// the BAT, 5 on the saves.
	enum Group { kData, kBat, kDirectory };
	const auto groupOf = [](std::size_t block) {
		if (block == 1 || block == 2) return kDirectory;
		if (block == 3 || block == 4) return kBat;
		return kData;
	};
	const std::vector<Group> order = change == CardChange::Import
		? std::vector<Group>{kData, kBat, kDirectory}
		: std::vector<Group>{kDirectory, kBat, kData};
	bool ok = true;
	for (Group group : order) {
		bool wroteAny = false;
		for (std::size_t at = 0; ok && at < after.size(); at += kBlock) {
			if (groupOf(at / kBlock) != group) continue;
			const std::size_t n = std::min(kBlock, after.size() - at);
			if (memcmp(before.data() + at, after.data() + at, n) == 0) continue;
			ok = std::fseek(f, static_cast<long>(at), SEEK_SET) == 0 && std::fwrite(after.data() + at, 1, n, f) == n;
			wroteAny = true;
		}
		if (!ok) break;
		if (wroteAny) {
			ok = std::fflush(f) == 0;
			fsync(fileno(f));
		}
		if (!ok) break;
	}
	ok = std::fclose(f) == 0 && ok;
	return ok;
}

// A save's name as the menu shows it, from the card's own encoding.
static std::string GcSaveName(const riftwii::GcSave& save, bool shiftJis)
{
	if (!save.comment_title.empty()) return riftwii::gc_card_text_utf8(save.comment_title, shiftJis);
	if (save.file_name.empty()) return save.game_code;
	return riftwii::gc_card_text_utf8(save.file_name, shiftJis);
}

// "10/16/2026": a save's time, seconds since 2000-01-01.
static std::string GcSaveDate(std::uint32_t seconds)
{
	// Whole years, then whole months, from 2000 (a leap year every fourth,
	// enough until 2100).
	std::uint32_t days = seconds / 86400;
	unsigned year = 2000;
	for (;;) {
		const std::uint32_t inYear = year % 4 == 0 ? 366 : 365;
		if (days < inYear) break;
		days -= inYear;
		++year;
	}
	static const unsigned kMonthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	unsigned month = 0;
	for (; month < 11; ++month) {
		const std::uint32_t inMonth = kMonthDays[month] + (month == 1 && year % 4 == 0 ? 1 : 0);
		if (days < inMonth) break;
		days -= inMonth;
	}
	// A whole date in the language's own order: month, day and year are
	// {1}, {2} and {3} ("10/16/2026"; "16/10/2026", "2026/10/16").
	return tr("{1}/{2}/{3}", {std::to_string(month + 1), std::to_string(static_cast<unsigned>(days) + 1),
		std::to_string(year)});
}

// A save's own file name on the SD card: "GMSE01-Sunshine save.gci", with
// only letters, digits and a few marks.
static std::string GciFileName(const riftwii::GcSave& save)
{
	std::string name = save.game_code + save.maker + "-" + save.file_name;
	std::string clean;
	for (char c : name) {
		const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
				   c == '_' || c == '.' || c == ' ';
		clean += plain ? c : '_';
	}
	if (clean.size() > 60) clean.resize(60);
	return clean + ".gci";
}

// Where in `dir` a copy of the save goes: GciFileName, or with -2, -3...
// before ".gci" when a file of that name is there, so a copy never
// overwrites an older one. Empty when even -999 is taken.
static std::string FreeGciPath(const std::string& dir, const riftwii::GcSave& save)
{
	const std::string name = GciFileName(save);
	const std::string stem = name.substr(0, name.size() - 4);
	for (int n = 1; n <= 999; ++n) {
		const std::string candidate = dir + "/" + (n == 1 ? name : stem + "-" + std::to_string(n) + ".gci");
		struct stat st;
		if (stat(candidate.c_str(), &st) != 0) return candidate;
	}
	return std::string();
}

// Two choices as tiles over a popup, with Back: 0 or 1, -1 for Back.
static int ChooseTwo(const std::string& title, const std::string& body, const std::string& label0,
		     const std::string& value0, const std::string& label1, const std::string& value1)
{
	PopupBox box(title, body, "", tr("Back"));
	box.CenterCancel();
	std::vector<TileSpec> tiles(2);
	tiles[0].x = 56;
	tiles[1].x = 332;
	for (TileSpec& t : tiles) {
		t.w = 252;
		t.h = 64;
		// Just above the buttons, as the code mod's start choice: at 238
		// they touched Back.
		t.y = PopupBox::kButtonY - 14 - t.h;
	}
	tiles[0].label = label0;
	tiles[0].value = value0;
	tiles[1].label = label1;
	tiles[1].value = value1;
	GuiTiles choices;
	choices.SetTiles(&tiles);
	choices.Select(0);
	box.Append(&choices);
	ResumeGui();
	int picked = -1;
	bool back = false;
	while (picked < 0 && !back) {
		usleep(20000);
		HaltGui();
		picked = choices.GetClicked();
		back = picked < 0 && box.Pressed();  // Back, B or HOME
		if (picked < 0 && !back) ResumeGui();
	}
	return back ? -1 : picked;
}

// What a row page's `act` calls to open another page over it:
// `aside(nested)` takes the page away (no input reaches it, nothing of it
// is drawn) while `nested` runs, then puts it back. Popups need none of
// this, as they switch the page off themselves.
using PageAside = std::function<void(const std::function<void()>&)>;

// A list of rows on its own page, with a note under it, until B: what
// MenuGcCard and the .gci picker show. `act(row, aside)` is called on A
// (with the GUI halted); it returns true to leave.
// `rowHeight`, when given, makes the rows that tall, for rows with a
// second line (FlowRow::detail).
static void RunRowPage(const std::string& title, const std::string& subtitle, std::vector<FlowRow>& rows,
		       const std::function<std::string(int)>& note, const std::function<bool(int, const PageAside&)>& act,
		       const std::function<std::string()>& status = nullptr, int rowHeight = 0)
{
	GuiText titleTxt(title.c_str(), 30, skin::kInk);
	Place(titleTxt, 78, 28);
	TitleBand titleBand;
	PageIcon pageIcon(&skin::iconSaves);
	const std::string sub = FlatCapped(subtitle, 40);
	GuiText subTxt(sub.c_str(), 16, skin::kInkDim);
	subTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	// With a status line (the card's space) the two share the band's height.
	subTxt.SetPosition(-40, status ? 28 : 40);
	GuiText statusTxt(status ? status().c_str() : "", 14, skin::kInkDim);
	statusTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	statusTxt.SetPosition(-40, 48);
	NoteCard noteCard;
	// Taller rows fit fewer in the same space: four of 56 where five of 44 go.
	GuiFlowList list(44, 84, 540, rowHeight > 0 ? 4 : 5);
	if (rowHeight > 0) list.SetRowHeight(rowHeight);
	list.SetCards(true);
	list.SetRows(&rows);
	list.Select(0);
	GuiText noteTxt("", 15, skin::kInkSoft);
	Place(noteTxt, NoteCard::kTextX, NoteCard::kTextY);
	noteTxt.SetWrap(true, NoteCard::kTextW, NoteCard::kLines);
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	HideButton(backBtn);
	GuiPrompts prompts;
	const int promptBack = prompts.Add("B", tr("Back"));
	prompts.Add("A", tr("Select"));

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&pageIcon);
	w.Append(&titleTxt);
	w.Append(&subTxt);
	if (status) w.Append(&statusTxt);
	w.Append(&noteCard);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
	w.Append(&prompts);
	mainWindow->Append(&w);
	ResumeGui();

	// A nested page ends with the GUI halted, as `act` was called.
	const PageAside aside = [&](const std::function<void()>& nested) {
		transition::Hold(transition::Kind::SlideForward);
		mainWindow->Remove(&w);
		nested();
		HaltGui();
		transition::Hold(transition::Kind::SlideBack);
		mainWindow->Append(&w);
	};

	int shownRow = -1;
	bool done = false;
	while (!done) {
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button});
		const int row = list.Selected();
		if (row != shownRow && row >= 0 && static_cast<std::size_t>(row) < rows.size()) {
			shownRow = row;
			noteTxt.SetText(note(row).c_str());
		}
		const int acted = list.GetClicked();
		if (acted >= 0 && static_cast<std::size_t>(acted) < rows.size()) {
			done = act(acted, aside);
			// What was pressed in a page or popup opened from here is not
			// this page's: B there must not also leave this one.
			list.GetClicked();
			list.GetClickedBack();
			prompts.GetClicked();
			backBtn.button.ResetState();
			if (status) statusTxt.SetText(status().c_str());
			list.Refresh();
			if (!rows.empty()) list.Select(std::min<int>(acted, static_cast<int>(rows.size()) - 1));
			shownRow = -1;
		}
		if (prompts.GetClicked() == promptBack) backBtn.button.SetState(STATE::CLICKED);
		if (backBtn.Clicked()) done = true;
		ResumeGui();
	}
	HaltGui();
	mainWindow->Remove(&w);
}

// The .gci files in sd:/riftwii/gc/saves, sorted, at most 200.
static std::vector<std::string> ListGciFiles()
{
	std::vector<std::string> names;
	if (DIR* dir = opendir(kGcSavesDir)) {
		while (dirent* e = readdir(dir)) {
			const std::string name = e->d_name;
			// macOS's "._" files beside every copied one are not saves.
			if (name.size() > 4 && name[0] != '.' && strcasecmp(name.c_str() + name.size() - 4, ".gci") == 0)
				names.push_back(name);
		}
		closedir(dir);
	}
	std::sort(names.begin(), names.end());
	if (names.size() > 200) names.resize(200);
	return names;
}

// One of `names` (ListGciFiles), picked on a page of its own. False: B.
static bool PickGciFile(const std::vector<std::string>& names, std::string& path)
{
	std::vector<FlowRow> rows;
	for (const std::string& name : names) {
		FlowRow row;
		row.kind = FlowRow::Kind::Action;
		row.label = FlatCapped(name, 50);
		rows.push_back(row);
	}
	int picked = -1;
	RunRowPage(tr("Add a save"), kGcSavesDir, rows, [](int) { return std::string(tr("Press A to add this save to the memory card.")); },
		[&](int row, const PageAside&) {
			picked = row;
			return true;
		});
	if (picked < 0) return false;
	path = std::string(kGcSavesDir) + "/" + names[static_cast<std::size_t>(picked)];
	return true;
}

// ---------------------------------------------------------------------------
// Restore an older save: the backups the loader makes of a GameCube game's
// memory card before each start (riftwii/gcbackup.hpp).

// A backup's date, as GcSaveDate writes one.
static std::string GcBackupDate(const riftwii::GcBackupStamp& stamp)
{
	return tr("{1}/{2}/{3}", {std::to_string(stamp.month), std::to_string(stamp.day), std::to_string(stamp.year)});
}

// A backup's date and time: the time as Home's clock writes it (the Clock
// setting); a second backup in the same minute gets its number.
static std::string GcBackupWhen(const riftwii::GcBackupStamp& stamp)
{
	const std::string date = GcBackupDate(stamp);
	char minutes[4];
	snprintf(minutes, sizeof(minutes), "%02u", stamp.minute % 60);
	const unsigned hour12 = stamp.hour % 12 == 0 ? 12 : stamp.hour % 12;
	const bool morning = stamp.hour < 12;
	const char* const ampm = morning ? "{1}:{2} AM" : "{1}:{2} PM";
	const std::string& mode = riftwii::wii::Settings().clock;
	std::string timeText;
	if (mode == "24") {
		timeText = std::to_string(stamp.hour) + ":" + minutes;
	} else if (mode == "12") {
		// The language's own 12-hour way, unless it writes the 24-hour
		// hour: then the English one, as ClockText does.
		const std::string own = tr(ampm);
		timeText = own.find("{1}") != std::string::npos ? tr(ampm, {std::to_string(hour12), minutes, std::to_string(hour12)})
			: std::to_string(hour12) + ":" + minutes + (morning ? " AM" : " PM");
	} else {
		timeText = tr(ampm, {std::to_string(hour12), minutes, std::to_string(stamp.hour)});
	}
	std::string when = date + " \xC2\xB7 " + timeText;
	if (stamp.serial >= 2) when += " (" + std::to_string(stamp.serial) + ")";
	return when;
}

// Puts the backup `from` over the card at `cardPath`, after a backup of the
// card as it is now (made even with Back up saves off), so the restore can
// be undone from the same list. Says how it went. True: the card changed.
static bool RestoreGcBackup(const std::string& cardPath, const std::string& folder, const std::string& from,
	const std::string& when)
{
	std::vector<std::uint8_t> bytes;
	if (!ReadSmallFile(from, riftwii::kGcBackupMaxBytes, bytes)) {
		logf("GameCube card %s: the backup %s could not be read\n", cardPath.c_str(), from.c_str());
		ShowPopup(tr("Save not restored"), tr("RiftWii could not read that copy."), tr("OK"));
		return false;
	}
	riftwii::GcCardInfo info;
	std::string why;
	if (!riftwii::gc_card_read(bytes, info, why)) {
		logf("GameCube card %s: the backup %s is damaged: %s\n", cardPath.c_str(), from.c_str(), why.c_str());
		ShowPopup(tr("Save not restored"), tr("That copy is damaged, so your save was not changed."), tr("OK"));
		return false;
	}
	const time_t now = time(nullptr);
	struct tm local;
	localtime_r(&now, &local);
	// Marked as the undo copy, which the list names "before you restored".
	riftwii::GcBackupStamp stamp = riftwii::gc_backup_stamp(local);
	stamp.undo = true;
	const riftwii::GcBackupResult before = riftwii::gc_backup_card(cardPath, folder, stamp);
	if (before.what == riftwii::GcBackupResult::What::Failed) {
		logf("GameCube card %s: not restored, as the card as it is now could not be backed up: %s\n", cardPath.c_str(),
			before.error.c_str());
		ShowPopup(tr("Save not restored"), tr("RiftWii could not keep a copy of the save as it is now, so it was not changed: {1}",
			{FlatCapped(before.error, 120)}), tr("OK"));
		return false;
	}
	if (before.what == riftwii::GcBackupResult::What::Made)
		logf("GameCube card %s: backed up to %s/%s before the restore\n", cardPath.c_str(), folder.c_str(), before.name.c_str());
	for (const std::string& old : before.pruned)
		logf("GameCube card %s: oldest backup %s/%s deleted, to keep the newest %u\n", cardPath.c_str(), folder.c_str(),
			old.c_str(), static_cast<unsigned>(riftwii::kGcBackupsKept));
	// A card that was never made: its folder may be missing too.
	mkdir("sd:/riftwii", 0777);
	mkdir("sd:/riftwii/gc", 0777);
	mkdir("sd:/riftwii/gc/cards", 0777);
	if (!riftwii::gc_overwrite_file(cardPath, bytes, why)) {
		logf("GameCube card %s: the restore from %s could not be written: %s\n", cardPath.c_str(), from.c_str(), why.c_str());
		ShowPopup(tr("Save not restored"), tr("RiftWii could not write the memory card: {1}", {FlatCapped(why, 120)}), tr("OK"));
		return false;
	}
	logf("GameCube card %s: restored from %s\n", cardPath.c_str(), from.c_str());
	ShowPopup(tr("Save restored"),
		tr("The memory card is as it was on {1}. The save from before is kept among the older saves, so you can go back to it.", {when}),
		tr("OK"));
	return true;
}

// The game's card's backups, newest first, on a page of their own; the one
// picked, once confirmed, is put back. True: the card changed.
static bool MenuGcRestore(FrontendState& state)
{
	const bool shared = GcCardShared(state);
	const std::string cardPath = GcCardPath(state);
	const std::string folder = riftwii::gc_backup_folder(state.game_id, shared);
	const std::vector<std::string> names = riftwii::gc_backup_list(folder);
	// The Saves page offers this only with backups there; none: nothing to show.
	if (names.empty()) return false;
	std::vector<FlowRow> rows;
	std::vector<std::string> whens;
	for (const std::string& name : names) {
		riftwii::GcBackupStamp stamp;
		riftwii::gc_backup_parse(name, stamp);
		whens.push_back(GcBackupWhen(stamp));
		FlowRow row;
		row.kind = FlowRow::Kind::Action;
		row.label = whens.back();
		// The copy a restore made: picking it undoes that restore.
		if (stamp.undo) row.label += std::string(" ") + tr("(before you restored)");
		// What the copy holds, on the row's second line, read from its
		// directory alone so the list opens fast: "2 saves · 4 blocks".
		riftwii::GcCardSpace space;
		if (ReadGcCardSpace(folder + "/" + name, space)) {
			const unsigned saves = riftwii::kGcCardMaxEntries - space.free_entries;
			const unsigned used = space.blocks - riftwii::kGcCardSystemBlocks - space.free_blocks;
			row.detail = (saves == 1 ? std::string(tr("1 save")) : tr("{1} saves", {std::to_string(saves)})) +
				" \xC2\xB7 " + (used == 1 ? std::string(tr("1 block")) : tr("{1} blocks", {std::to_string(used)}));
		}
		rows.push_back(row);
	}
	bool restored = false;
	const auto note = [&](int) -> std::string {
		return shared ? tr("Press A to put the shared memory card back to how it was then, for every game on it.")
			: tr("Press A to put this game's save back to how it was then.");
	};
	const auto act = [&](int row, const PageAside&) {
		const std::size_t at = static_cast<std::size_t>(row);
		const std::string& when = whens[at];
		// The question in the title, short in every language; the date and
		// time in the body. Cancel is the second button, the one B and HOME
		// press, so backing out never changes a save.
		const std::string body = shared
			? tr("Every game's save on the shared memory card goes back to how it was on {1}. How they are now is kept as a copy first, so you can come back to them.", {when})
			: tr("Your save goes back to how it was on {1}. Your save as it is now is kept as a copy first, so you can come back to it.", {when});
		if (ShowPopup(tr("Restore this save?"), body, tr("Restore"), tr("Cancel")) != 0) return false;
		restored = RestoreGcBackup(cardPath, folder, folder + "/" + names[at], when);
		// The list has changed (the copy made first): back to the saves.
		return restored;
	};
	// No line beside the title: a long one (French) reaches it on a 16:9
	// TV, and the Saves page before this one named the game and its card.
	// Rows of two lines, the date and what the copy holds.
	RunRowPage(tr("Restore an older save"), "", rows, note, act, nullptr, 56);
	return restored;
}

// ---------------------------------------------------------------------------
// Bring your Dolphin saves: every save in Dolphin's GC folder, copied to
// sd:/riftwii/gc/dolphin, goes into the memory card the page shows.

// What bringing the saves in came to, in a sentence or three.
static std::string GcDolphinSummary(const riftwii::GcImportCount& count, bool leftOut)
{
	std::string text = tr("Added: {1}. Already on the card, so skipped: {2}.",
		{std::to_string(count.added), std::to_string(count.skipped)});
	if (count.unusable > 0) text += std::string(" ") + tr("Could not be used: {1}.", {std::to_string(count.unusable)});
	if (count.full) text += std::string(" ") + tr("The memory card is full, so the rest did not fit.");
	if (leftOut) text += std::string(" ") + tr("Some files were left out because there were too many.");
	return text;
}

// Asks, backs the card up, then adds the saves found in the folder to
// `changed` (a copy of the card). True: something was added and `changed`
// is to be written. Says why when nothing is.
static bool GcDolphinImport(const FrontendState& state, const std::string& cardPath, bool shared,
	std::vector<std::uint8_t>& changed, riftwii::GcImportCount& count, bool& leftOut)
{
	const riftwii::GcDolphinFiles found = riftwii::gc_dolphin_scan(kGcDolphinDir, riftwii::gc_dolphin_regions(state.game_id, shared));
	if (found.gci.empty() && found.raw.empty()) {
		ShowPopup(tr("No saves from Dolphin"),
			tr("RiftWii found no saves for this memory card in {1}. Copy Dolphin's GC folder there (in Dolphin: File > Open User Folder, then GC): it holds the USA, EUR and JAP folders.",
				{kGcDolphinDir}), tr("OK"));
		return false;
	}
	leftOut = found.truncated;
	const std::size_t files = found.gci.size() + found.raw.size();
	if (found.truncated)
		logf("GameCube card %s: the Dolphin folder holds more than RiftWii reads at once, so some files were left out\n",
			cardPath.c_str());
	// Cancel is the second button, the one B and HOME press, so backing out
	// never changes a save.
	if (ShowPopup(tr("Bring your Dolphin saves?"),
		    tr("RiftWii found {1} files in Dolphin's folder. Their saves are copied to this memory card, and the ones it has already are left alone. The card is kept as a copy first, so Restore an older save can undo it.",
			{std::to_string(files)}) + (found.truncated ? std::string(" ") + tr("Some files will be left out because there are too many.") : std::string()),
		    tr("Bring them"), tr("Cancel")) != 0)
		return false;

	// A file at a time: a save is at most a card's blocks, a card file 16 MB.
	for (const std::string& path : found.gci) {
		if (count.full) break;
		std::vector<std::uint8_t> gci;
		if (!ReadSmallFile(path, riftwii::kGcDolphinMaxGciBytes, gci)) {
			logf("GameCube card %s: %s could not be read\n", cardPath.c_str(), path.c_str());
			++count.unusable;
			continue;
		}
		riftwii::gc_import_one(changed, gci, count);
	}
	for (const std::string& path : found.raw) {
		if (count.full) break;
		std::vector<std::uint8_t> raw;
		if (!ReadSmallFile(path, riftwii::kGcDolphinMaxRawBytes, raw)) {
			logf("GameCube card %s: %s could not be read\n", cardPath.c_str(), path.c_str());
			++count.unusable;
			continue;
		}
		riftwii::gc_import_card(changed, raw, count);
	}
	logf("GameCube card %s: Dolphin saves found in %u files: %u to add, %u already there, %u unusable%s%s\n", cardPath.c_str(),
		static_cast<unsigned>(files), count.added, count.skipped, count.unusable, count.full ? ", the card filled up" : "",
		leftOut ? ", some files were left out" : "");
	if (count.added == 0) {
		ShowPopup(count.full ? tr("Memory card full") : tr("Nothing new to add"), GcDolphinSummary(count, leftOut), tr("OK"));
		return false;
	}
	// The copy is made only now that there is something to add (still before
	// anything is written), so a second try that finds nothing new makes none.
	const std::string folder = riftwii::gc_backup_folder(state.game_id, shared);
	const time_t now = time(nullptr);
	struct tm local;
	localtime_r(&now, &local);
	const riftwii::GcBackupResult before = riftwii::gc_backup_card(cardPath, folder, riftwii::gc_backup_stamp(local));
	if (before.what == riftwii::GcBackupResult::What::Failed) {
		logf("GameCube card %s: Dolphin saves not brought, as the card could not be backed up: %s\n", cardPath.c_str(),
			before.error.c_str());
		ShowPopup(tr("Saves not brought"),
			tr("RiftWii could not keep a copy of the memory card, so it was not changed: {1}", {FlatCapped(before.error, 120)}), tr("OK"));
		return false;
	}
	if (before.what == riftwii::GcBackupResult::What::Made)
		logf("GameCube card %s: backed up to %s/%s before bringing the Dolphin saves\n", cardPath.c_str(), folder.c_str(),
			before.name.c_str());
	for (const std::string& old : before.pruned)
		logf("GameCube card %s: oldest backup %s/%s deleted, to keep the newest %u\n", cardPath.c_str(), folder.c_str(),
			old.c_str(), static_cast<unsigned>(riftwii::kGcBackupsKept));
	return true;
}

// The game's memory card: its saves, how full it is, and adding a save.
static void MenuGcCard(FrontendState& state)
{
	const std::string path = GcCardPath(state);
	const bool shared = GcCardShared(state);
	std::vector<std::uint8_t> card;
	// A card is 2 MB; the library takes 4 to 128 Mbit ones.
	if (!ReadSmallFile(path, 16u << 20, card)) {
		ShowPopup(tr("No memory card yet"),
			shared ? tr("The shared memory card is made the first time a game that uses it starts: {1}.", {path})
				: tr("This game's memory card is made the first time the game starts: {1}.", {path}), tr("OK"));
		return;
	}
	riftwii::GcCardInfo info;
	std::string error;
	if (!riftwii::gc_card_read(card, info, error)) {
		logf("GameCube card %s: %s\n", path.c_str(), error.c_str());
		ShowPopup(tr("Memory card damaged"), tr("RiftWii cannot read {1}: {2}", {path, FlatCapped(error, 120)}), tr("OK"));
		return;
	}
	std::vector<FlowRow> rows;
	GcIconTextures icons;
	const std::string backupFolder = riftwii::gc_backup_folder(state.game_id, shared);
	bool haveBackups = false;
	bool dolphinRow = false;
	const auto rebuild = [&] {
		rows.clear();
		icons.Clear();
		haveBackups = !riftwii::gc_backup_list(backupFolder).empty();
		for (const riftwii::GcSave& save : info.saves) {
			FlowRow row;
			row.kind = FlowRow::Kind::Action;
			row.icon_column = true;
			if (save.intact) row.icon = icons.Make(card, save.slot);
			row.label = FlatCapped(GcSaveName(save, info.shift_jis), 30);
			row.value = save.block_count == 1 ? std::string(tr("1 block"))
				: tr("{1} blocks", {std::to_string(save.block_count)});
			row.dim = !save.intact;
			rows.push_back(row);
		}
		FlowRow add;
		add.kind = FlowRow::Kind::Action;
		add.label = tr("Add a save from the SD card");
		add.icon_column = true;
		rows.push_back(add);
		FlowRow restore;
		restore.kind = FlowRow::Kind::Action;
		restore.label = tr("Restore an older save");
		restore.icon_column = true;
		rows.push_back(restore);
		// Only when Dolphin's folder has been copied to the SD card; the
		// Add a save note says where to put it otherwise.
		dolphinRow = riftwii::gc_dolphin_folder_exists(kGcDolphinDir);
		if (dolphinRow) {
			FlowRow dolphin;
			dolphin.kind = FlowRow::Kind::Action;
			dolphin.label = tr("Bring your Dolphin saves");
			dolphin.icon_column = true;
			rows.push_back(dolphin);
		}
	};
	rebuild();
	// Set when the card file could not be read back after a failed write:
	// the page leaves rather than show saves that may not be there.
	bool leavePage = false;
	const auto commit = [&](const std::vector<std::uint8_t>& changed, CardChange change) {
		if (!WriteChangedBlocks(path, card, changed, change)) {
			logf("GameCube card %s: could not be written\n", path.c_str());
			// Part of it may have reached the SD card: what the page shows
			// is read from the file again, so it matches what is there.
			std::vector<std::uint8_t> onDisk;
			riftwii::GcCardInfo again;
			std::string why;
			if (ReadSmallFile(path, 16u << 20, onDisk) && riftwii::gc_card_read(onDisk, again, why)) {
				card = std::move(onDisk);
				info = again;
				rebuild();
			} else {
				logf("GameCube card %s: could not be read back after the failed write, so the page closes\n", path.c_str());
				leavePage = true;
			}
			ShowPopup(tr("Memory card not changed"), tr("RiftWii could not write {1}.", {path}), tr("OK"));
			return false;
		}
		card = changed;
		riftwii::GcCardInfo again;
		std::string why;
		if (riftwii::gc_card_read(card, again, why)) info = again;
		rebuild();
		return true;
	};
	const auto note = [&](int row) -> std::string {
		const std::size_t at = static_cast<std::size_t>(row);
		if (at == info.saves.size() + 1) {
			if (!haveBackups) return tr("RiftWii keeps a copy each time you start the game.");
			return shared ? tr("Go back to how the shared memory card was on a day you pick, for every game on it.")
				: tr("Go back to how your save was on a day you pick.");
		}
		if (dolphinRow && at == info.saves.size() + 2) return tr("Copy every save from Dolphin's GC folder in one go.");
		if (at >= info.saves.size()) {
			const std::string add = tr("Adds a .gci save file from {1}.", {kGcSavesDir});
			if (dolphinRow) return add;
			return add + "\n" + std::string(tr("To bring all of Dolphin's saves at once, copy its GC folder (in Dolphin: File > Open User Folder) to {1}.", {kGcDolphinDir}));
		}
		const riftwii::GcSave& save = info.saves[at];
		if (!save.intact) return tr("This save is damaged: its blocks do not add up. It can only be deleted.");
		// The save alone here: the card's space is in the header.
		std::string text = save.comment_detail.empty() ? std::string()
			: riftwii::gc_card_text_utf8(save.comment_detail, info.shift_jis) + "\n";
		text += tr("Saved {1}", {GcSaveDate(save.modified)}) + " \xC2\xB7 " + save.game_code + save.maker;
		return text;
	};
	const auto act = [&](int row, const PageAside& aside) {
		const std::size_t at = static_cast<std::size_t>(row);
		std::string why;
		if (at == info.saves.size() + 1) {
			// None yet: the note says when there will be; nothing to open.
			if (!haveBackups) return false;
			bool restored = false;
			aside([&] { restored = MenuGcRestore(state); });
			if (!restored) return false;
			// The card is the backup's now: read again, as the page shows it.
			std::vector<std::uint8_t> onDisk;
			riftwii::GcCardInfo again;
			if (ReadSmallFile(path, 16u << 20, onDisk) && riftwii::gc_card_read(onDisk, again, why)) {
				card = std::move(onDisk);
				info = again;
				rebuild();
				return false;
			}
			logf("GameCube card %s: could not be read back after the restore, so the page closes\n", path.c_str());
			return true;
		}
		if (dolphinRow && at == info.saves.size() + 2) {
			std::vector<std::uint8_t> changed = card;
			riftwii::GcImportCount count;
			bool leftOut = false;
			if (!GcDolphinImport(state, path, shared, changed, count, leftOut)) return false;
			if (commit(changed, CardChange::Import)) {
				ShowPopup(count.full ? tr("Memory card full") : tr("Dolphin saves brought"), GcDolphinSummary(count, leftOut), tr("OK"));
				logf("GameCube card %s: the Dolphin saves are on the card\n", path.c_str());
			}
			return leavePage;
		}
		if (at >= info.saves.size()) {
			const std::vector<std::string> names = ListGciFiles();
			if (names.empty()) {
				ShowPopup(tr("No saves on the SD card"),
					tr("Put .gci save files (from Dolphin, Nintendont or another card) in {1}, then try again.", {kGcSavesDir}), tr("OK"));
				return false;
			}
			// The picker is a page of its own: this one is taken away
			// meanwhile, so its B and its list do not see the picker's presses.
			std::string from;
			bool picked = false;
			aside([&] { picked = PickGciFile(names, from); });
			if (!picked) return false;
			std::vector<std::uint8_t> gci;
			// The largest a .gci can be: its entry and a whole card's blocks.
			if (!ReadSmallFile(from, 64 + 2043u * 0x2000, gci)) {
				ShowPopup(tr("Save not added"), tr("RiftWii could not read {1}.", {from}), tr("OK"));
				return false;
			}
			std::vector<std::uint8_t> changed = card;
			if (!riftwii::gc_card_import(changed, gci, why)) {
				logf("GameCube card %s: %s not added: %s\n", path.c_str(), from.c_str(), why.c_str());
				ShowPopup(tr("Save not added"), FlatCapped(why, 200), tr("OK"));
				return false;
			}
			if (commit(changed, CardChange::Import)) logf("GameCube card %s: %s added\n", path.c_str(), from.c_str());
			return leavePage;
		}
		const riftwii::GcSave save = info.saves[at];
		const std::string name = GcSaveName(save, info.shift_jis);
		const int choice = save.intact
			? ChooseTwo(name, note(row), tr("Copy to the SD card"), tr("As a save file for Dolphin"), tr("Delete"), tr("From this memory card"))
			: (ShowPopup(name, note(row), tr("Delete"), tr("Back")) == 0 ? 1 : -1);
		if (choice == 0) {
			std::vector<std::uint8_t> gci;
			if (!riftwii::gc_card_export(card, save.slot, gci, why)) {
				ShowPopup(tr("Save not copied"), FlatCapped(why, 200), tr("OK"));
				return false;
			}
			mkdir("sd:/riftwii", 0777);
			mkdir("sd:/riftwii/gc", 0777);
			mkdir(kGcSavesDir, 0777);
			const std::string free = FreeGciPath(kGcSavesDir, save);
			const std::string to = free.empty() ? std::string(kGcSavesDir) + "/" + GciFileName(save) : free;
			FILE* f = free.empty() ? nullptr : std::fopen(to.c_str(), "wb");
			const bool written = f && std::fwrite(gci.data(), 1, gci.size(), f) == gci.size();
			if (f) std::fclose(f);
			logf("GameCube card %s: %s %s\n", path.c_str(), to.c_str(), written ? "written" : "could not be written");
			ShowPopup(written ? tr("Save copied") : tr("Save not copied"),
				written ? tr("It is {1}.", {to}) : tr("RiftWii could not write {1}.", {to}), tr("OK"));
		} else if (choice == 1) {
			// Keep it is the second button, the one B and HOME press, so
			// backing out never deletes a save.
			if (ShowPopup(tr("Delete {1}?", {name}), tr("It is gone from the memory card for good. Copy it to the SD card first to keep it."),
				    tr("Delete"), tr("Keep it")) != 0)
				return false;
			std::vector<std::uint8_t> changed = card;
			if (!riftwii::gc_card_delete(changed, save.slot, why)) {
				ShowPopup(tr("Save not deleted"), FlatCapped(why, 200), tr("OK"));
				return false;
			}
			if (commit(changed, CardChange::Delete)) logf("GameCube card %s: %s deleted\n", path.c_str(), save.file_name.c_str());
		}
		return leavePage;
	};
	const auto space = [&] {
		return tr("{1} of {2} blocks free", {std::to_string(info.free_blocks), std::to_string(info.blocks - 5)});
	};
	RunRowPage(shared ? tr("Shared Memory Card") : tr("Memory Card"), GameTitle(state), rows, note, act, space);
}

// A GameCube game's crash screen (gcnative.hpp's SetGcCrashScreen, laid
// out as runtime/gc/gc_crash.h says), in the menu's own font and language,
// made here as the launch screen cannot show translations: a header line,
// a hint saying which button goes where, and the pieces the blob puts the
// register dump together from at the crash.
static void MakeGcCrashCard(const riftwii::wii::ImageGame& game)
{
	std::string hint;
	// The ways back say that HOME or A goes back; with none the hint says
	// to turn the Wii off.
	bool wayBack = true;
	switch (riftwii::wii::FindGcWayBack()) {
		case riftwii::wii::GcWayBack::HomebrewChannel:
			hint = tr("Press HOME or A to go back to the Homebrew Channel. RiftWii offers to send a problem report when it next starts.");
			break;
		case riftwii::wii::GcWayBack::Channel: hint = tr("Press HOME or A to go back to RiftWii and send a problem report."); break;
		case riftwii::wii::GcWayBack::WiiMenu:
			hint = tr("Press HOME or A to go back to the Wii Menu. RiftWii offers to send a problem report when it next starts.");
			break;
		default:
			hint = tr("Turn the Wii off and on again. RiftWii offers to send a problem report when it next starts.");
			wayBack = false;
			break;
	}
	// A 16:9 TV stretches the game's picture, so there the glyphs are drawn
	// 3/4 as wide, as the menu itself is.
	const bool wide = riftwii::wii::MenuWidescreen();
	const auto glyphWidth = [wide](int size) { return static_cast<FT_UInt>(wide ? size * 3 / 4 : 0); };
	const unsigned cardW = GC_CRASH_CARD_W;
	std::vector<std::uint8_t> card(static_cast<std::size_t>(cardW) * GC_CRASH_CARD_H, 0);

	// A band of the card from the left edge, at the largest size whose
	// text fits; even cut short at the smallest, what was drawn beats none.
	const auto drawBand = [&](const std::string& text, std::initializer_list<int> sizes, unsigned top, unsigned tall,
				  int& usedSize) {
		std::vector<std::uint8_t> band;
		int lines = -1;
		for (int size : sizes) {
			usedSize = size;
			band.assign(static_cast<std::size_t>(cardW) * tall, 0);
			lines = RenderTextCoverage(text.c_str(), size, band.data(), cardW, tall, glyphWidth(size), false);
			if (lines >= 1) break;
		}
		std::copy(band.begin(), band.end(), card.begin() + static_cast<std::size_t>(top) * cardW);
		return lines;
	};
	int headerSize = 0, hintSize = 0;
	const int headerLines = drawBand(tr("The game stopped."), {24, 21, 18}, 0, GC_CRASH_HEADER_H, headerSize);
	const int hintLines = drawBand(hint, {20, 18, 16, 14}, GC_CRASH_HINT_Y, GC_CRASH_HINT_H, hintSize);

	// The dump's pieces. The cell is the widest digit and a pixel; the size
	// is the largest at which the four columns fit across the card and
	// every register's name ends before its value.
	std::vector<std::string> texts(GC_CRASH_PIECES);
	for (unsigned index = 0; index < GC_CRASH_PIECE_TEXT; ++index) texts[index] = riftwii::gc_crash_piece_text(index);
	texts[GC_CRASH_PIECE_GAME] = game.id;
	texts[GC_CRASH_PIECE_VERSION] = std::string("RiftWii ") + RIFTWII_VERSION;
	texts[GC_CRASH_PIECE_CALLED] = tr("Called from");
	const unsigned pieceW = 512;
	std::vector<std::uint8_t> piece(static_cast<std::size_t>(pieceW) * GC_CRASH_STRIP_H);
	const auto measure = [&](const std::string& text, int size) {
		std::fill(piece.begin(), piece.end(), 0);
		int advance = 0;
		if (RenderTextCoverage(text.c_str(), size, piece.data(), pieceW, GC_CRASH_STRIP_H, glyphWidth(size), false, &advance) != 1)
			return -1;
		return advance;
	};
	int dumpSize = 0;
	unsigned cell = 0;
	for (int size = 18; size >= 10 && cell == 0; --size) {
		int widest = 0;
		for (unsigned digit = 0; digit < 16 && widest >= 0; ++digit) {
			const int advance = measure(texts[digit], size);
			widest = advance < 0 ? -1 : std::max(widest, advance);
		}
		if (widest <= 0) continue;
		const unsigned candidate = static_cast<unsigned>(widest) + 1;
		if (candidate * GC_CRASH_COLUMNS * GC_CRASH_COLUMN_CELLS > cardW) continue;
		// The longest names, "DSISR", "SRR1" and "R31", end half a cell
		// before the value.
		const int room = static_cast<int>(GC_CRASH_VALUE_CELL * candidate - candidate / 2);
		if (measure("DSISR", size) > room || measure("SRR1", size) > room || measure("R31", size) > room) continue;
		// The two rows that are not on the grid stay inside the card (its
		// title-safe width) too: the game and version from the third
		// column, and "Called from" with four addresses two cells apart.
		const int third = static_cast<int>(2 * GC_CRASH_COLUMN_CELLS * candidate);
		const int idLine = third + measure(texts[GC_CRASH_PIECE_GAME], size) + static_cast<int>(2 * candidate) +
			measure(texts[GC_CRASH_PIECE_VERSION], size);
		const int called = std::max(measure(texts[GC_CRASH_PIECE_CALLED], size) + static_cast<int>(2 * candidate),
			static_cast<int>(GC_CRASH_VALUE_CELL * candidate));
		const int chainLine = called + static_cast<int>(4 * 8 * candidate + 3 * 2 * candidate);
		if (idLine > static_cast<int>(cardW) || chainLine > static_cast<int>(cardW)) continue;
		dumpSize = size;
		cell = candidate;
	}
	if (cell == 0) {
		// The smallest size, overlapping or not.
		dumpSize = 10;
		cell = static_cast<unsigned>(std::max(1, measure("0", dumpSize))) + 1;
	}
	auto strip = std::make_unique<gc_crash_strip>();
	memset(strip.get(), 0, sizeof(gc_crash_strip));
	strip->cell = static_cast<std::uint16_t>(cell);
	unsigned next = 0;
	unsigned missing = 0;
	for (unsigned index = 0; index < GC_CRASH_PIECES; ++index) {
		if (texts[index].empty()) continue;
		const int advance = measure(texts[index], dumpSize);
		if (advance <= 0 || !riftwii::gc_crash_strip_add(*strip, index, piece.data(), pieceW, advance, next)) ++missing;
	}

	std::vector<std::uint8_t> screen(GC_CRASH_SCREEN_BYTES, 0);
	riftwii::gc_crash_pack(card.data(), card.size(), screen.data());
	memcpy(screen.data() + GC_CRASH_CARD_BYTES, strip.get(), sizeof(gc_crash_strip));
	logf("GameCube: the crash screen drawn%s: the header at %d px%s, the hint at %d px (%d line(s)%s), the dump at "
		"%d px in %u-pixel cells (%u of %u columns of the strip, %u piece(s) missing)\n",
		wide ? " 3/4 as wide for 16:9" : "", headerSize, headerLines >= 1 ? "" : " (cut short)", hintSize, hintLines,
		hintLines >= 1 ? "" : ", not all of it fits", dumpSize, cell, next, GC_CRASH_STRIP_W, missing);
	riftwii::wii::SetGcCrashScreen(std::move(screen), wayBack);
}

// Before a GameCube game starts: its controls the first time, and its
// crash screen. False: Back.
static bool ReadyGcStart(const FrontendState& state)
{
	const riftwii::wii::ImageGame* game = SelectedGcGame(state);
	if (!game) return true;
	if (!ShowGcControlsOnce(state)) return false;
	MakeGcCrashCard(*game);
	return true;
}

// The Mods page: each pack made for the game as a switch, its options
// under it once it is on.
static void BuildModRows(const FrontendState& state, const std::string& scanStatus,
			 std::vector<FlowRow>& rows, std::vector<RowRef>& refs)
{
	rows.clear();
	refs.clear();
	const auto add = [&](FlowRow row, RowRef ref) {
		rows.push_back(std::move(row));
		refs.push_back(ref);
	};
	std::size_t shown = 0;
	for (std::size_t i = 0; i < state.model.packages.size(); ++i) {
		const riftwii::LaunchPackage& p = state.model.packages[i];
		if (!riftwii::show_package(p)) continue;
		++shown;
		FlowRow head;
		head.kind = p.valid ? FlowRow::Kind::Toggle : FlowRow::Kind::Header;
		head.heading = true;
		head.label = PackLabel(p);
		head.value = !p.valid ? tr("Broken") : p.enabled ? tr("On") : tr("Off");
		head.on = p.enabled;
		head.dim = !p.valid;
		add(head, {RowRef::What::Pack, i});
		if (!p.valid) {
			for (const std::string& line : Chunks(p.detail, 44, 3)) {
				FlowRow note;
				note.label = line;
				note.dim = true;
				add(note, {RowRef::What::Note, i});
			}
			continue;
		}
		if (p.code_build() && riftwii::wii::IsPickedCodeBuild(p.file)) {
			FlowRow forget;
			forget.kind = FlowRow::Kind::Action;
			forget.indent = true;
			forget.label = "Remove from the list";
			add(forget, {RowRef::What::ForgetCodes, i});
		}
		// A build on the SD card itself can go into an image of its own.
		if (p.code_build() && p.gct_path.compare(0, 4, "sd:/") == 0 && !riftwii::wii::IsPickedCodeBuild(p.file)) {
			FlowRow image;
			image.kind = FlowRow::Kind::Action;
			image.indent = true;
			image.label = "Make an SD image...";
			add(image, {RowRef::What::MakeImage, i});
		}
		if (!p.enabled) continue;
		for (std::size_t o = 0; o < p.package.options.size(); ++o) {
			// An option merged across packs shows once, under the
			// first enabled pack that has it.
			if (!state.model.option_shown(i, o)) continue;
			const riftwii::Option& option = p.package.options[o];
			FlowRow row;
			row.kind = FlowRow::Kind::Option;
			row.indent = true;
			row.label = option.name;
			row.value = state.model.choice_name(i, o);
			row.on = row.value != "Off";
			add(row, {RowRef::What::Option, i, o});
		}
	}
	if (shown == 0) {
		FlowRow none;
		none.label = state.model.packages.empty() ? "No mods on the SD card" : "No mods for this game";
		add(none, {RowRef::What::Note});
		FlowRow hint;
		hint.dim = true;
		hint.label = scanStatus != riftwii::wii::kScanReady ? FlatCapped(scanStatus, 44)
			: state.model.packages.empty() ? "Put Riivolution XML in sd:/riivolution"
			: state.model.packages.size() == 1 ? tr("1 XML file is for another game")
			: tr("{1} XML files are for other games", {std::to_string(state.model.packages.size())});
		add(hint, {RowRef::What::Note});
	}
	FlowRow pick;
	pick.kind = FlowRow::Kind::Action;
	pick.label = tr("Add a code build...");
	add(pick, {RowRef::What::AddCodes});
	// Home tiles of their own for these mods (riftwii/profiles.hpp): for a
	// game on a drive, which Home lists by itself (the disc's tile is the drive's).
	if ((state.use_usb || state.use_sd) && !state.game_id.empty() && shown != 0) {
		FlowRow tile;
		tile.kind = FlowRow::Kind::Action;
		tile.label = tr("Make a Home tile with these mods...");
		add(tile, {RowRef::What::SaveTile});
		if (!state.profile_key.empty()) {
			FlowRow rename;
			rename.kind = FlowRow::Kind::Action;
			rename.label = tr("Rename this tile...");
			add(rename, {RowRef::What::RenameTile});
			FlowRow remove;
			remove.kind = FlowRow::Kind::Action;
			remove.label = tr("Remove this tile from Home");
			add(remove, {RowRef::What::RemoveTile});
		}
	}
}

static std::string GameTitle(const FrontendState& state);

// The name for a Home tile, typed on the search's keyboard (its name
// mode). False when cancelled.
static bool AskTileName(const std::string& title, std::string& name)
{
	GuiSearchKeys keys(riftwii::clean_profile_name(name), title, riftwii::kMaxProfileName);
	mainWindow->SetState(STATE::DISABLED);
	mainWindow->Append(&keys);
	keys.SetState(STATE::DEFAULT);
	ResumeGui();
	while (keys.Result() == 0) usleep(20000);
	HaltGui();
	mainWindow->Remove(&keys);
	mainWindow->SetState(STATE::DEFAULT);
	if (keys.Result() < 0) return false;
	name = keys.Text();
	return true;
}

// A new tile's name to start from: the first mod turned on (what the tile
// is for, as often as not), else the game's.
static std::string SuggestedTileName(const FrontendState& state)
{
	for (const riftwii::LaunchPackage& p : state.model.packages)
		if (p.valid && p.enabled && riftwii::show_package(p)) return PackLabel(p);
	return GameTitle(state);
}

static std::string SourceWhere(const FrontendState& state)
{
	if (state.use_usb) return "USB drive";
	if (state.use_sd) return "SD card";
	if (state.use_channel && state.channel_index < state.channels.size() && !state.channels[state.channel_index].emu_nand.empty())
		return "SD card";
	if (state.use_channel) return "Wii channels";
	return "Disc drive";
}

// The game's own name, whichever tile opened it.
static std::string GameOwnTitle(const FrontendState& state)
{
	if (state.use_usb && state.usb_index < state.usb_catalog.games.size())
		return GameName(state.usb_catalog.games[state.usb_index]);
	if (state.use_sd && state.sd_index < state.sd_catalog.games.size())
		return GameName(state.sd_catalog.games[state.sd_index]);
	if (state.use_channel && state.channel_index < state.channels.size())
		return ChannelName(state.channels[state.channel_index]);
	return riftwii::wii::GameDisplayName(state.game_id, state.disc_title.empty() ? state.game_id : state.disc_title);
}

// What the pages are headed with: a profile's tile name, else the game's.
static std::string GameTitle(const FrontendState& state)
{
	return state.profile_name.empty() ? GameOwnTitle(state) : state.profile_name;
}

// ---------------------------------------------------------------------------
// About this game: what GameTDB says about it (wii/online/gameinfo.hpp),
// the controllers it takes checked against those connected now.

static ConnectedPads PadsConnectedNow()
{
	ConnectedPads pads;
	for (int i = 0; i < 4; ++i) {
		u32 type = 0;
		if (WPAD_Probe(i, &type) != WPAD_ERR_NONE) continue;
		pads.remote = true;
		const WPADData* d = WPAD_Data(i);
		if (!d) continue;
		if (d->exp.type == WPAD_EXP_NUNCHUK) pads.nunchuk = true;
		// A Wii U Pro Controller reads as a Classic Controller here. A
		// GameCube game takes it with the Classic Controller's buttons
		// (runtime/gc/gc_pro.h), so it gets a card of its own.
		if (d->exp.type == WPAD_EXP_CLASSIC) {
			if (d->exp.classic.type == CLASSIC_TYPE_WIIU) pads.pro = true;
			else pads.classic = true;
		}
	}
	u32 boardType = 0;
	if (WPAD_Probe(WPAD_BALANCE_BOARD, &boardType) == WPAD_ERR_NONE) pads.board = true;
	// SI_Probe answers from what the ports last said (PAD_ScanPads here
	// would take a press from the menu).
	for (int i = 0; i < 4; ++i) {
		const u32 port = SI_Probe(i);
		if (!(port & SI_ERROR_NO_RESPONSE) && (port & SI_TYPE_MASK) == SI_TYPE_GC) pads.gamecube = true;
	}
	riftwii::wii::GcAdapterView adapter;
	if (riftwii::wii::GcAdapterMenuLastView(adapter) && adapter.open)
		for (unsigned i = 0; i < GCAD_PORTS; ++i)
			if (adapter.present[i]) pads.gamecube = true;
	return pads;
}

// GameTDB's name for a controller as players know it; "" for what is not
// one (it lists Mii support with them).
static std::string ControlName(const std::string& type)
{
	if (type == "wiimote") return tr("Wii Remote");
	if (type == "nunchuk") return tr("Nunchuk");
	if (type == "classiccontroller") return tr("Classic Controller");
	if (type == "gamecube") return tr("GameCube controller");
	if (type == "balanceboard") return tr("Balance Board");
	if (type == "motionplus" || type == "wiimotionplus") return tr("Wii MotionPlus");
	if (type == "wheel") return tr("Wii Wheel");
	if (type == "zapper") return tr("Wii Zapper");
	if (type == "wiispeak" || type == "microphone") return tr("Microphone (Wii Speak)");
	if (type == "keyboard") return tr("USB keyboard");
	if (type == "nintendods") return tr("Nintendo DS");
	if (type == "guitar") return tr("Guitar");
	if (type == "drums") return tr("Drums");
	if (type == "dancepad") return tr("Dance mat");
	if (type == "udraw") return tr("uDraw tablet");
	if (type == "mii") return "";
	return type;
}

// Whether RiftWii can tell `type` is connected (`connected`: it is).
static bool ControlSeen(const std::string& type, const ConnectedPads& pads, bool& connected)
{
	if (type == "wiimote") connected = pads.remote;
	else if (type == "nunchuk") connected = pads.nunchuk;
	else if (type == "classiccontroller") connected = pads.classic;
	else if (type == "gamecube") connected = pads.gamecube;
	else if (type == "balanceboard") connected = pads.board;
	else return false;
	return true;
}

static std::string ReleaseText(const riftwii::GameInfo& g)
{
	static const char* const kMonths[] = {"January", "February", "March", "April", "May", "June", "July",
		"August", "September", "October", "November", "December"};
	if (g.year == 0) return "";
	if (g.month < 1 || g.month > 12) return std::to_string(g.year);
	const std::string month = tr(kMonths[g.month - 1]);
	if (g.day < 1) return month + " " + std::to_string(g.year);
	return tr("{1} {2}, {3}", {month, std::to_string(g.day), std::to_string(g.year)});
}

// "Racing, kart racing": GameTDB's words, the first with a capital.
static std::string GenreText(const std::vector<std::string>& genres)
{
	std::string out;
	for (const std::string& g : genres) out += (out.empty() ? "" : ", ") + g;
	if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
	return out;
}

// The database on the card, downloaded while a popup counts the bytes.
static bool DownloadGameInfoNow()
{
	std::string error;
	bool ok;
	{
		PopupBox box(tr("Downloading game details"), "");
		ResumeGui();
		ok = riftwii::wii::DownloadGameInfo(error, [&](std::size_t received) {
			HaltGui();
			box.SetBody(SizeText(received));
			ResumeGui();
		});
		HaltGui();
	}
	if (!ok) ShowPopup(tr("No game details"), tr("Could not download them: {1}", {FlatCapped(error, 140)}), tr("OK"));
	return ok;
}

// The game's details, from the cache or read out of the database (with a
// popup and a bar while that takes seconds), downloading the database
// first when the player agrees. False, having said why, when there are none.
static bool FetchGameInfo(const FrontendState& state, riftwii::GameInfo& info)
{
	std::set<std::string> wanted;
	for (const riftwii::wii::ImageGame& g : state.usb_catalog.games) wanted.insert(g.id);
	for (const riftwii::wii::ImageGame& g : state.sd_catalog.games) wanted.insert(g.id);
	for (int attempt = 0; attempt < 2; ++attempt) {
		std::string error;
		std::unique_ptr<ProgressBar> bar;
		std::unique_ptr<PopupBox> box;
		const auto progress = [&](double done) {
			if (!box) {
				bar.reset(new ProgressBar(56, 280, 528));
				box.reset(new PopupBox(tr("Reading game details..."),
					tr("Once for all your games: next time they open at once."), "", tr("Stop")));
				box->Add(bar.get(), 528);
				ResumeGui();
			}
			HaltGui();
			bar->Set(done);
			const bool stop = box->Pressed();
			ResumeGui();
			return !stop;
		};
		const riftwii::wii::GameInfoResult result = riftwii::wii::FindGameInfo(state.game_id, wanted, info, error, progress);
		if (box) {
			HaltGui();
			box.reset();
			bar.reset();
		}
		switch (result) {
			case riftwii::wii::GameInfoResult::Found:
				return true;
			case riftwii::wii::GameInfoResult::NotListed:
				ShowPopup(tr("No details for this game"), tr("GameTDB's database does not list {1}.", {state.game_id}), tr("OK"));
				return false;
			case riftwii::wii::GameInfoResult::Failed:
				if (error != "stopped")
					ShowPopup(tr("No game details"), tr("Could not read them: {1}", {FlatCapped(error, 140)}), tr("OK"));
				return false;
			case riftwii::wii::GameInfoResult::NoDatabase:
				break;
		}
		if (attempt > 0) return false;
		if (!riftwii::wii::Settings().online) {
			ShowPopup(tr("No game details yet"),
				tr("They come from GameTDB's database of Wii games. Turn on Download names and cheats in Settings to get it."),
				tr("OK"));
			return false;
		}
		if (ShowPopup(tr("Download game details?"),
			    tr("They come from GameTDB's database of every Wii game: about 2 MB, kept on the SD card. Download it now?"),
			    tr("Download"), tr("Cancel")) != 0)
			return false;
		if (!DownloadGameInfoNow()) return false;
	}
	return false;
}

// Where the page's parts go: inside a TV's overscan, 5% of the screen in
// from each edge (32 across, 24 down), the box over the band's edge.
static constexpr int kHeroX = 40, kHeroY = 80, kHeroW = 150, kHeroH = 206;
static constexpr int kTilesX = 214, kTilesRight = 600;  // 40 in from each side, as the box
static constexpr int kTilesW = kTilesRight - kTilesX;


// The game's box on its page: the front of its full cover when one is
// kept (the shelf's, wii/menu/boxart.hpp), else its small cover made
// larger, else the disc drive's box for a disc, else the game's colour.
// The art fills the box with slightly rounded corners of its own (a real
// case's), over a soft drop shadow; a frame with larger corners around it
// cut the art's corners off.
class HeroCover : public GuiElement {
public:
	// `channel`: a WiiWare or Virtual Console game, drawn as its channel
	// (its own icon, playing) when GameTDB has no box or cover for it.
	HeroCover(std::string id, bool disc, GXColor hue, bool channel = false)
		: id(std::move(id)), disc(disc), channel(channel), hue(hue) {}
	HeroCover(const HeroCover&) = delete;
	HeroCover& operator=(const HeroCover&) = delete;
	~HeroCover() override { free(rounded); }
	void Draw() override {
		riftwii::wii::DrawTrace trace("the page's cover");
		const float x = kHeroX, y = kHeroY, w = kHeroW, h = kHeroH;
		if (const u8* art = RoundedArt()) {
			// A drop shadow, as if the box stood a little off the page, lit
			// from above.
			constexpr float kDrop = 6.0f;
			const float m = riftwii::kShadowMargin;
			skin::DrawNine(skin::dropShadow9, x - m + 2, y - m + kDrop, w + 2 * m - 4, h + 2 * m, riftwii::kShadowCorner);
			skin::DrawRgb5a3Part(art, roundedW, roundedH, 0.0f, 1.0f, x, y, w, h);
			return;
		}
		skin::DrawNine(skin::card9, x - 8, y - 8, w + 16, h + 16, riftwii::kCardCorner);
		const float ax = x + 4, ay = y + 4, aw = w - 8, ah = h - 8;
		if (const u8* box = id.empty() ? nullptr : riftwii::wii::BoxTexture(id)) {
			const float u0 = static_cast<float>(riftwii::kBoxSpineWidth) / riftwii::kBoxWidth;
			const float u1 = static_cast<float>(riftwii::kBoxSpineWidth + riftwii::kBoxFrontWidth) / riftwii::kBoxWidth;
			skin::DrawRgb5a3Part(box, riftwii::kBoxWidth, riftwii::kBoxHeight, u0, u1, ax, ay, aw, ah);
		} else if (const u8* cover = id.empty() ? nullptr : riftwii::wii::CoverTexture(id)) {
			skin::DrawRgb5a3Part(cover, riftwii::kCoverWidth, riftwii::kCoverHeight, 0.0f, 1.0f, ax, ay, aw, ah);
		} else if (riftwii::wii::BannerPlayer* icon = channel ? IconFor(id) : nullptr) {
			// The channel's icon as the Channels view shows it (4:3, on
			// black), across the card and centred down it.
			const float ih = aw * 3 / 4, iy = ay + (ah - ih) / 2;
			Menu_DrawRectangle(ax, ay, aw, ah, (GXColor){0, 0, 0, 255}, 1);
			icon->Step();
			icon->Draw(ax, iy, aw, ih, 255);
		} else if (disc && skin::discCase.data) {
			const float scale = aw / skin::discCase.w;
			skin::Draw(skin::discCase, ax + (aw - skin::discCase.w) / 2, ay + (ah - skin::discCase.h) / 2, 255, scale);
		} else {
			Menu_DrawRectangle(ax, ay, aw, ah, hue, 1);
			skin::Draw(skin::iconDisc, ax + (aw - 40) / 2, ay + (ah - 40) / 2);
		}
	}
private:
	// The corners' radius as drawn: about a real case's, in proportion.
	static constexpr float kArtCorner = 6.0f;
	// The box's front (or the cover) with its corners rounded off
	// (riftwii::round_corners), made again whenever the art changes.
	const u8* RoundedArt() {
		if (id.empty()) return nullptr;
		const u8* box = riftwii::wii::BoxTexture(id);
		const u8* cover = box ? nullptr : riftwii::wii::CoverTexture(id);
		const u8* from = box ? box : cover;
		if (!from) return nullptr;
		if (from == roundedFrom) return rounded;
		constexpr std::size_t kBytes = static_cast<std::size_t>(riftwii::kBoxFrontWidth) * riftwii::kBoxHeight * 2;
		if (!rounded) rounded = static_cast<u8*>(memalign(32, kBytes));
		if (!rounded) return nullptr;
		roundedW = box ? riftwii::kBoxFrontWidth : riftwii::kCoverWidth;
		roundedH = box ? riftwii::kBoxHeight : riftwii::kCoverHeight;
		const float texels = kArtCorner * roundedW / kHeroW;
		const bool made = box ? riftwii::round_corners(box, riftwii::kBoxWidth, riftwii::kBoxHeight, riftwii::kBoxSpineWidth,
						 riftwii::kBoxSpineWidth + riftwii::kBoxFrontWidth, texels, rounded)
				      : riftwii::round_corners(cover, riftwii::kCoverWidth, riftwii::kCoverHeight, 0,
						 riftwii::kCoverWidth, texels, rounded);
		if (!made) return nullptr;
		DCFlushRange(rounded, kBytes);
		GX_InvalidateTexAll();
		roundedFrom = from;
		return rounded;
	}
	std::string id;
	bool disc;
	bool channel;
	GXColor hue;
	u8* rounded = nullptr;  // kBoxFrontWidth x kBoxHeight at most, 32-aligned
	const u8* roundedFrom = nullptr;
	int roundedW = 0, roundedH = 0;
};

// The About page's body, right of the box and under it: the facts in two
// columns, the age rating as a badge in the band, the controllers as
// icons, the story as a paragraph that scrolls (Up and Down, or the
// arrows). Everything here is read, not pressed: plain text and icons.
class AboutBody : public GuiElement {
public:
	struct Fact {
		std::string name, value;
	};
	struct Control {
		std::string type, name;
		bool required = false, seen = false, connected = false;
	};
	static constexpr int kGridX = kTilesX, kGridY = 132, kColW = 186, kColGap = 14, kRowH = 38;
	static constexpr int kStoryX = 40, kStoryY = 326, kStoryW = 520, kStoryLines = 4, kLineH = 19;  // ends by y 402, clear of the prompts

	AboutBody(std::vector<Fact> facts, std::vector<Control> controls, std::string rating, std::string ratingSystem,
		const std::string& story)
		: facts(std::move(facts)), controls(std::move(controls)), rating(std::move(rating)),
		  ratingSystem(std::move(ratingSystem))
	{
		width = screenwidth;
		height = screenheight;
		for (const Fact& f : this->facts) {
			std::string upper = f.name;
			for (char& ch : upper)
				if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
			names.emplace_back(new GuiText(upper.c_str(), 12, skin::kInkDim));
			values.emplace_back(new GuiText(f.value.c_str(), 16, skin::kInk));
			values.back()->SetMaxWidth(kColW - 4);
		}
		playsWith.reset(new GuiText(Upper(tr("Plays with")).c_str(), 12, skin::kInkDim));
		storyHead.reset(new GuiText(Upper(tr("Story")).c_str(), 12, skin::kInkDim));
		ratingTxt.reset(new GuiText(this->rating.c_str(), 26, skin::kInk));
		systemTxt.reset(new GuiText(this->ratingSystem.c_str(), 11, skin::kInkSoft));
		WrapStory(story);
		for (int i = 0; i < kStoryLines; ++i) shown.emplace_back(new GuiText("", 15, skin::kInkSoft));
		for (auto* t : {playsWith.get(), storyHead.get(), ratingTxt.get(), systemTxt.get()}) t->SetParent(this);
		for (auto& t : names) t->SetParent(this);
		for (auto& t : values) t->SetParent(this);
		for (auto& t : shown) t->SetParent(this);
	}

	bool HasRating() const { return !rating.empty(); }
	// The control the pointer is on (-1 none), for the line at the foot.
	int HoveredControl() const { return hoverControl; }
	const Control& ControlAt(int i) const { return controls[static_cast<std::size_t>(i)]; }

	void Draw() override
	{
		const int alpha = GetAlpha();
		// The facts, two to a row.
		for (std::size_t i = 0; i < facts.size(); ++i) {
			const int x = kGridX + static_cast<int>(i % 2) * (kColW + kColGap);
			const int y = kGridY + static_cast<int>(i / 2) * kRowH;
			Place(*names[i], x, y);
			names[i]->Draw();
			Place(*values[i], x, y + 14);
			values[i]->Draw();
		}
		// The controllers: needed ones first, at full strength; the ones
		// that also work, fainter; a dot under each one that is connected.
		const int rows = static_cast<int>((facts.size() + 1) / 2);
		const int cy = kGridY + rows * kRowH + 4;
		iconY = cy + 15;
		if (!controls.empty()) {
			Place(*playsWith, kGridX, cy);
			playsWith->Draw();
			for (std::size_t i = 0; i < controls.size(); ++i) {
				const Control& c = controls[i];
				const float x = static_cast<float>(IconX(static_cast<int>(i)));
				const int a = c.required ? alpha : alpha * 120 / 255;
				const float lift = static_cast<int>(i) == hoverControl ? -2.0f : 0.0f;
				skin::Draw(skin::ControlIcon(c.type), x, iconY + lift, a, kIconScale);
				if (c.connected)
					Menu_DrawRectangle(x + 40 * kIconScale / 2 - 2, iconY + 40 * kIconScale + 2, 4, 4,
						skin::WithAlpha(skin::kAccent, alpha), 1);
			}
		}
		// The age rating: a badge in the band's top right corner.
		if (HasRating()) {
			const float bx = 548, by = 32, bw = 52, bh = 62;
			skin::DrawNine(skin::card9, bx - 8, by - 8, bw + 16, bh + 16, riftwii::kCardCorner, alpha);
			ratingTxt->SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
			ratingTxt->SetPosition(static_cast<int>(bx + bw / 2) - screenwidth / 2, static_cast<int>(by + 8));
			ratingTxt->Draw();
			systemTxt->SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
			systemTxt->SetPosition(static_cast<int>(bx + bw / 2) - screenwidth / 2, static_cast<int>(by + bh - 19));
			systemTxt->Draw();
		}
		// The story, a window of it at a time.
		if (!lines.empty()) {
			Place(*storyHead, kStoryX, kStoryY - 18);
			storyHead->Draw();
			for (int i = 0; i < kStoryLines; ++i) {
				const std::size_t at = static_cast<std::size_t>(first + i);
				shown[i]->SetText(at < lines.size() ? lines[at].c_str() : "");
				Place(*shown[i], kStoryX, kStoryY + i * kLineH);
				shown[i]->Draw();
			}
			if (Scrolls()) {
				if (first > 0) skin::Draw(overUp ? skin::scrollUpOver : skin::scrollUp, kArrowX - 4, kStoryY - 6, alpha);
				if (first + kStoryLines < static_cast<int>(lines.size()))
					skin::Draw(overDown ? skin::scrollDownOver : skin::scrollDown, kArrowX - 4,
						kStoryY + kStoryLines * kLineH - 30, alpha);
			}
		}
	}

	void Update(GuiTrigger* t) override
	{
		if (!t) return;
		const int most = std::max(0, static_cast<int>(lines.size()) - kStoryLines);
		if (t->Down() && first < most) ++first;
		if (t->Up() && first > 0) --first;
		if (!t->wpad || !t->wpad->ir.valid) {
			if (t->chan == 0) hoverControl = -1;
			return;
		}
		const int px = static_cast<int>(t->wpad->ir.x), py = static_cast<int>(t->wpad->ir.y);
		hoverControl = -1;
		for (int i = 0; i < static_cast<int>(controls.size()); ++i) {
			const int x = IconX(i);
			if (px >= x && px < x + kIconStep && py >= iconY && py < iconY + 40 * kIconScale) hoverControl = i;
		}
		overUp = Scrolls() && px >= kArrowX - 4 && px < kArrowX + 30 && py >= kStoryY - 6 && py < kStoryY + 28;
		overDown = Scrolls() && px >= kArrowX - 4 && px < kArrowX + 30 && py >= kStoryY + kStoryLines * kLineH - 30 &&
			py < kStoryY + kStoryLines * kLineH + 4;
		if (t->wpad->btns_d & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A)) {
			if (overUp && first > 0) --first;
			if (overDown && first < most) ++first;
		}
	}

private:
	static constexpr float kIconScale = 0.95f;
	static constexpr int kIconStep = 46;
	static constexpr int kArrowX = 572;
	static std::string Upper(std::string text)
	{
		for (char& ch : text)
			if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
		return text;
	}
	int IconX(int i) const { return kGridX + i * kIconStep; }
	bool Scrolls() const { return static_cast<int>(lines.size()) > kStoryLines; }
	// The story in lines as wide as the column, measured in the menu's
	// font, a word at a time.
	void WrapStory(const std::string& text)
	{
		GuiText measure("", 15, skin::kInkSoft);
		std::string line;
		std::size_t i = 0;
		while (i < text.size()) {
			std::size_t j = text.find(' ', i);
			if (j == std::string::npos) j = text.size();
			const std::string word = text.substr(i, j - i);
			const std::string tryLine = line.empty() ? word : line + " " + word;
			measure.SetText(tryLine.c_str());
			if (!line.empty() && measure.GetTextWidth() > kStoryW) {
				lines.push_back(line);
				line = word;
			} else {
				line = tryLine;
			}
			i = j + 1;
		}
		if (!line.empty()) lines.push_back(line);
	}

	std::vector<Fact> facts;
	std::vector<Control> controls;
	std::string rating, ratingSystem;
	std::vector<std::unique_ptr<GuiText>> names, values, shown;
	std::unique_ptr<GuiText> playsWith, storyHead, ratingTxt, systemTxt;
	std::vector<std::string> lines;
	int first = 0;
	int hoverControl = -1;
	float iconY = 0;
	bool overUp = false, overDown = false;
};

// The About page: the game's band and box as on its page, then what
// GameTDB says about it. True when its details were downloaded again
// (the page is then drawn anew with them).
static bool ShowAbout(const FrontendState& state, const riftwii::GameInfo& info)
{
	std::vector<AboutBody::Fact> facts;
	const std::string released = ReleaseText(info);
	if (!released.empty()) facts.push_back({tr("Released"), released});
	// The first two of GameTDB's genres: a column's width.
	if (!info.genres.empty())
		facts.push_back({tr("Genre"), GenreText({info.genres.begin(), info.genres.begin() + std::min<std::size_t>(2, info.genres.size())})});
	if (info.players == 1) facts.push_back({tr("Players"), tr("1 player")});
	else if (info.players > 1) facts.push_back({tr("Players"), tr("1 to {1} players", {std::to_string(info.players)})});
	if (info.online_players > 0) {
		// Nintendo's servers closed in 2014: the server the launch would use.
		const riftwii::GameSettings& game = state.model.game;
		const std::string server = game.server == "global" ? riftwii::wii::Settings().wfc_server : game.server;
		std::string online = tr("Up to {1} players", {std::to_string(info.online_players)});
		if (state.model.packs_patch_game()) online += " \xC2\xB7 " + std::string(tr("The mod's own"));
		else if (server != "off" && !server.empty()) online += " \xC2\xB7 " + ServerName(server);
		facts.push_back({tr("Online"), online});
	} else if (riftwii::game_has_no_online(state.game_id) || info.players > 0) {
		facts.push_back({tr("Online"), tr("No online play")});
	}
	if (!info.developer.empty() && info.developer == info.publisher) {
		facts.push_back({tr("Made by"), info.developer});
	} else {
		if (!info.developer.empty()) facts.push_back({tr("Developer"), info.developer});
		if (!info.publisher.empty()) facts.push_back({tr("Publisher"), info.publisher});
	}
	if (facts.size() > 6) facts.resize(6);

	const ConnectedPads pads = PadsConnectedNow();
	std::vector<AboutBody::Control> controls;
	for (int pass = 0; pass < 2; ++pass) {
		for (const riftwii::GameControl& c : info.controls) {
			if (c.required != (pass == 0)) continue;
			const std::string name = ControlName(c.type);
			if (name.empty() || controls.size() >= 9) continue;
			AboutBody::Control one;
			one.type = c.type;
			one.name = name;
			one.required = c.required;
			one.seen = ControlSeen(c.type, pads, one.connected);
			controls.push_back(one);
		}
	}

	const bool fromDisc = !state.use_usb && !state.use_sd && !state.use_channel;
	GameBanner banner(skin::HueFor(state.game_id), state.game_id, fromDisc);
	HeroCover hero(state.game_id, fromDisc, skin::HueFor(state.game_id), state.use_channel);
	AboutBody body(facts, controls, info.rating, info.rating_system, riftwii::plain_punctuation(info.synopsis));
	// The band's text as on the game page: an overline, the name.
	std::string overline = tr("About this game");
	for (char& ch : overline)
		if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
	GuiText whereTxt(overline.c_str(), 13, (GXColor){225, 228, 235, 255});
	const std::string title = GameOwnTitle(state);
	const int titleRoom = kTilesW - (body.HasRating() ? 70 : 0);
	GuiText titleTxt(title.c_str(), 28, skin::kWhite);
	int titleSize = 28;
	while (titleSize > 20 && titleTxt.GetTextWidth() > titleRoom) titleTxt.SetFontSize(--titleSize);
	if (titleTxt.GetTextWidth() > titleRoom) titleTxt.SetWrap(true, titleRoom, 2);
	OutlinedText whereOutlined(whereTxt, kTilesX + 1, 34, (GXColor){225, 228, 235, 255}, 1, 1);
	OutlinedText titleOutlined(titleTxt, kTilesX, 52, skin::kWhite, 2, 2);

	// The line at the foot: where the details come from, or the control
	// the pointer is on.
	std::string source;
	std::time_t written = 0;
	std::string sourceLine = tr("From GameTDB, the database of Wii games its community keeps.");
	if (riftwii::wii::GameInfoSource(source, written)) {
		char date[16];
		std::strftime(date, sizeof date, "%Y-%m-%d", std::localtime(&written));
		sourceLine = tr("Game details of {1}", {date}) + " \xC2\xB7 GameTDB";
	}
	GuiText footTxt(sourceLine.c_str(), 14, skin::kInkDim);
	Place(footTxt, 40, 428);
	footTxt.SetMaxWidth(300);

	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	HideButton(backBtn);
	GuiPrompts prompts;
	const int promptBack = prompts.Add("B", tr("Back"));
	const bool online = riftwii::wii::Settings().online;
	const int promptUpdate = online ? prompts.Add("-", tr("Update")) : -1;

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&banner);
	w.Append(&hero);
	w.Append(&whereOutlined);
	w.Append(&titleOutlined);
	w.Append(&body);
	w.Append(&footTxt);
	w.Append(&backBtn.button);
	w.Append(&prompts);
	mainWindow->Append(&w);
	ResumeGui();
	logf("About: %s, %u controller(s) listed, %u bytes of story\n", state.game_id.c_str(),
		static_cast<unsigned>(info.controls.size()), static_cast<unsigned>(info.synopsis.size()));

	int footFor = -2;
	bool update = false;
	bool done = false;
	while (!done)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button});
		const int hovered = body.HoveredControl();
		if (hovered != footFor) {
			footFor = hovered;
			if (hovered < 0) {
				footTxt.SetText(sourceLine.c_str());
			} else {
				const AboutBody::Control& c = body.ControlAt(hovered);
				std::string text = c.name + " \xC2\xB7 " + (c.required ? tr("Needed to play") : tr("Also works with"));
				if (c.seen) text += " \xC2\xB7 " + std::string(c.connected ? tr("Connected") : tr("Not connected"));
				footTxt.SetText(text.c_str());
			}
		}
		for (int i = 0; i < 4 && promptUpdate >= 0; ++i) {
			const bool minus = (userInput[i].wpad && (userInput[i].wpad->btns_d & (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS))) ||
				(userInput[i].pad.btns_d & PAD_TRIGGER_L);
			if (minus) update = true;
		}
		const int pressed = prompts.GetClicked();
		if (pressed == promptBack) backBtn.button.SetState(STATE::CLICKED);
		if (pressed >= 0 && pressed == promptUpdate) update = true;
		if (backBtn.Clicked() || update) done = true;
		ResumeGui();
	}
	HaltGui();
	mainWindow->Remove(&w);
	if (!update) return false;
	return DownloadGameInfoNow();
}

static void MenuAbout(const FrontendState& state, riftwii::GameInfo info)
{
	while (ShowAbout(state, info)) {
		riftwii::GameInfo fresh;
		if (!FetchGameInfo(state, fresh)) return;
		info = fresh;
	}
}

// ---------------------------------------------------------------------------
// Cheats: the game's cheat file as a list to tick, opened from the game
// page. The file is plain text (riftwii/cheats.hpp); when the Wii is
// online a missing one is fetched from the GeckoCodes archive.

// A cheat's values (its X's, Y's...) asked one by one on a hex keypad,
// its notes above it (they say what the values are), then written into
// the cheat file. False when cancelled or not written (`error` then says
// why, empty for a cancel). Called and returns with the GUI halted.
static bool AskCheatValues(const FrontendState& state, const riftwii::Cheat& c, std::string& values,
	std::string& error)
{
	error.clear();
	std::vector<riftwii::CheatField> fields = riftwii::wii::CheatFields(state.game_id, c.name, state.game_revision);
	if (fields.empty()) {
		error = tr("Its values could not be found in the file.");
		return false;
	}
	std::string notes;
	for (const std::string& n : c.notes) notes += (notes.empty() ? "" : " ") + n;
	if (notes.empty()) notes = tr("The cheat has no notes about its values: its author's page may say what they are.");
	for (riftwii::CheatField& f : fields) {
		const std::string title = tr("{1}: {2} ({3} digits)",
			{FlatCapped(c.name, 22), std::string(1, f.letter), std::to_string(f.digits)});
		GuiSearchKeys keys(f.value, title, FlatCapped(notes, 220), f.digits);
		mainWindow->SetState(STATE::DISABLED);
		mainWindow->Append(&keys);
		keys.SetState(STATE::DEFAULT);
		ResumeGui();
		while (keys.Result() == 0) usleep(20000);
		HaltGui();
		mainWindow->Remove(&keys);
		mainWindow->SetState(STATE::DEFAULT);
		if (keys.Result() < 0) return false;
		f.value = keys.Text();
	}
	if (!riftwii::wii::FillCheatValues(state.game_id, c.name, fields, state.game_revision, error)) return false;
	values.clear();
	for (const riftwii::CheatField& f : riftwii::wii::CheatFields(state.game_id, c.name, state.game_revision))
		values += std::string(values.empty() ? "" : ", ") + f.letter + " = " + f.value;
	return true;
}

static void MenuCheats(FrontendState& state)
{
	riftwii::GameSettings& game = state.model.game;
	riftwii::CheatFile file;
	std::string status;
	bool loaded = false;
	const std::string fileNote = tr("The cheats are in {1}. Edit it on a computer to add your own.",
		{CheatFileShown(state.game_id)});

	enum class Act { Use, Download, Cheat, Values, None };
	struct Ref {
		Act act;
		std::size_t cheat;
	};
	std::vector<FlowRow> rows;
	std::vector<Ref> refs;
	const auto build = [&]() {
		rows.clear();
		refs.clear();
		FlowRow use;
		use.kind = FlowRow::Kind::Toggle;
		use.label = tr("Use cheats");
		use.value = game.cheats ? tr("On") : tr("Off");
		use.on = game.cheats;
		rows.push_back(use);
		refs.push_back({Act::Use, 0});
		FlowRow download;
		download.kind = FlowRow::Kind::Action;
		download.label = loaded ? tr("Get the latest cheats") : tr("Download cheats");
		download.value = tr("Download");
		download.dim = !riftwii::wii::Settings().online;
		rows.push_back(download);
		refs.push_back({Act::Download, 0});
		if (!loaded) {
			for (const std::string& line : Chunks(status, 44, 3)) {
				FlowRow note;
				note.label = line;
				note.dim = true;
				rows.push_back(note);
				refs.push_back({Act::None, 0});
			}
			return;
		}
		for (std::size_t i = 0; i < file.cheats.size(); ++i) {
			const riftwii::Cheat& c = file.cheats[i];
			FlowRow row;
			row.kind = FlowRow::Kind::Toggle;
			row.label = FlatCapped(c.name, 48);
			if (c.needs_values) {
				// Its X's are filled in here (a tester: "Edit first" meant
				// a computer).
				row.kind = FlowRow::Kind::Action;
				row.value = tr("Set values");
			} else {
				row.on = game.cheat_names.count(c.name) != 0;
				row.value = row.on ? tr("On") : tr("Off");
			}
			rows.push_back(row);
			refs.push_back({Act::Cheat, i});
			if (c.has_template) {
				// Values filled in here: they can be changed again.
				FlowRow values;
				values.kind = FlowRow::Kind::Action;
				values.label = tr("Its values");
				values.value = tr("Change");
				values.indent = true;
				rows.push_back(values);
				refs.push_back({Act::Values, i});
			}
		}
	};
	// Picks of cheats the file no longer has are dropped, so the game page
	// counts only what will be applied.
	const auto load = [&](bool download) {
		loaded = riftwii::wii::LoadGameCheats(state.game_id, state.game_revision, download, file, status);
		if (!loaded) return;
		std::set<std::string> names;
		for (const riftwii::Cheat& c : file.cheats) names.insert(c.name);
		bool pruned = false;
		for (auto it = game.cheat_names.begin(); it != game.cheat_names.end();) {
			if (names.count(*it)) ++it;
			else {
				it = game.cheat_names.erase(it);
				pruned = true;
			}
		}
		std::string error;
		if (pruned) SaveChoices(state, error);
	};
	load(false);
	build();

	GuiText titleTxt(tr("Cheats"), 30, skin::kInk);
	Place(titleTxt, 78, 28);
	TitleBand titleBand;
	PageIcon pageIcon(&skin::iconCheats);
	const std::string gameName = FlatCapped(GameTitle(state), 40);
	GuiText gameTxt(gameName.c_str(), 16, skin::kInkDim);
	gameTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	gameTxt.SetPosition(-40, 40);
	NoteCard noteCard;
	GuiFlowList list(44, 84, 540, 5);
	list.SetCards(true);
	list.SetRows(&rows);
	list.Select(0);
	GuiText noteTxt(fileNote.c_str(), 15, skin::kInkSoft);
	Place(noteTxt, NoteCard::kTextX, NoteCard::kTextY);
	noteTxt.SetWrap(true, NoteCard::kTextW, NoteCard::kLines);
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	HideButton(backBtn);
	GuiPrompts prompts;
	const int promptBack = prompts.Add("B", tr("Back"));
	prompts.Add("A", tr("Select"));

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&pageIcon);
	w.Append(&titleTxt);
	w.Append(&gameTxt);
	w.Append(&noteCard);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
	w.Append(&prompts);
	mainWindow->Append(&w);

	const auto say = [&](const std::string& text) { noteTxt.SetText(text.c_str()); };
	// Fetching blocks for a few seconds: the message is drawn first.
	const auto fetch = [&]() {
		std::string text = tr("Downloading cheats...");
		say(text);
		ResumeGui();
		std::string error;
		const std::size_t before = loaded ? file.cheats.size() : 0;
		const bool hadFile = loaded;
		const bool ok = riftwii::wii::DownloadCheats(state.game_id, error);
		HaltGui();
		if (ok) {
			load(false);
			// Said plainly either way: the same list again looked like
			// nothing had happened (a tester).
			const std::size_t now = loaded ? file.cheats.size() : 0;
			say(!loaded ? status
				: hadFile && now == before ? tr("Downloaded: still {1} cheats, the list was already the latest.", {std::to_string(now)})
				: hadFile ? tr("Downloaded: {1} cheats now ({2} before).", {std::to_string(now), std::to_string(before)})
				: tr("{1} cheats. Turn on the ones you want.", {std::to_string(now)}));
		} else {
			logf("Cheats: download failed: %s\n", error.c_str());
			say(tr("Could not download cheats: {1}", {FlatCapped(error, 90)}));
		}
		build();
		list.Refresh();
	};
	// A game opened for the first time gets its cheats straight away, with
	// the page drawn and saying so (it was blank and still while the
	// download waited: a tester's menu sat for minutes with the network
	// down). Not when the network already failed this session.
	if (!loaded && riftwii::wii::Settings().online && !state.game_id.empty()) {
		if (riftwii::wii::NetFailed()) {
			status = tr("No cheat file yet, and the network is not up. Check the Wii's Internet settings, then choose Download.");
			logf("Cheats: none for %s yet; not downloaded, the network failed earlier\n", state.game_id.c_str());
			build();
			list.Refresh();
		} else {
			fetch();
		}
	}
	ResumeGui();

	int shownRow = -1;
	bool done = false;
	while (!done)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button});

		const int row = list.Selected();
		if (row != shownRow && row >= 0 && static_cast<std::size_t>(row) < refs.size()) {
			shownRow = row;
			const Ref& ref = refs[static_cast<std::size_t>(row)];
			if (ref.act == Act::Cheat) {
				const riftwii::Cheat& c = file.cheats[ref.cheat];
				std::string note;
				for (const std::string& n : c.notes) note += (note.empty() ? "" : " ") + n;
				if (c.needs_values) note = tr("This cheat has values to fill in (the X's): press A to set them.") + (note.empty() ? "" : " " + note);
				say(FlatCapped(note.empty() ? c.name : c.name + ": " + note, 200));
			} else if (ref.act == Act::Values) {
				std::string values;
				for (const riftwii::CheatField& f : riftwii::wii::CheatFields(state.game_id, file.cheats[ref.cheat].name, state.game_revision))
					values += std::string(values.empty() ? "" : ", ") + f.letter + " = " + f.value;
				say(tr("Values of {1}: {2}. Press A to change them.", {FlatCapped(file.cheats[ref.cheat].name, 60), values}));
			} else if (ref.act == Act::Use) {
				say(tr("Cheats are only applied when this is On."));
			} else if (ref.act == Act::Download) {
				say(riftwii::wii::Settings().online ? tr("Gets the latest cheats from the GeckoCodes archive. Your own cheats and values stay.")
						      : tr("Downloads are off in Settings."));
			} else {
				say(fileNote);
			}
		}

		int acted = list.GetClicked();
		if (acted < 0) acted = list.GetClickedBack();
		if (acted >= 0 && static_cast<std::size_t>(acted) < refs.size()) {
			const Ref ref = refs[static_cast<std::size_t>(acted)];
			bool changed = false;
			if (ref.act == Act::Use) {
				game.cheats = !game.cheats;
				changed = true;
			} else if (ref.act == Act::Download) {
				if (!riftwii::wii::Settings().online) say(tr("Downloads are off in Settings."));
				else fetch();
			} else if (ref.act == Act::Values || (ref.act == Act::Cheat && file.cheats[ref.cheat].needs_values)) {
				const riftwii::Cheat c = file.cheats[ref.cheat];
				std::string values, error;
				if (AskCheatValues(state, c, values, error)) {
					load(false);
					// Set means wanted: it is turned on.
					game.cheat_names.insert(c.name);
					game.cheats = true;
					std::string saveError;
					if (!SaveChoices(state, saveError)) logf("Cheats: %s\n", saveError.c_str());
					build();
					list.Refresh();
					list.Select(acted);
					shownRow = acted;
					say(tr("{1}: {2}. It is on.", {FlatCapped(c.name, 60), values}));
				} else if (!error.empty()) {
					say(tr("The values were not saved: {1}", {FlatCapped(error, 120)}));
				}
			} else if (ref.act == Act::Cheat) {
				const riftwii::Cheat& c = file.cheats[ref.cheat];
				{
					if (game.cheat_names.count(c.name)) game.cheat_names.erase(c.name);
					else {
						game.cheat_names.insert(c.name);
						game.cheats = true;  // picking one means cheats are wanted
					}
					changed = true;
				}
			}
			if (changed) {
				std::string error;
				if (!SaveChoices(state, error)) say(error);
				build();
				list.Refresh();
				list.Select(acted);
				shownRow = -1;
			}
		}
		if (prompts.GetClicked() == promptBack) backBtn.button.SetState(STATE::CLICKED);
		if (backBtn.Clicked()) done = true;
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
}

// What the game page says about the Mods row.
static std::string ModsNote(const FrontendState& state, const std::string& scanStatus)
{
	if (const std::string problem = riftwii::wii::ModPlaceProblem(state, true); !problem.empty())
		return FlatCapped(problem, 90);
	if (HomebrewAppPackOn(state)) return tr("CTGP Revolution does not work from RiftWii yet (see Start).");
	std::size_t enabled = 0;
	const std::size_t shown = ShownPacks(state, &enabled);
	if (shown == 0) {
		if (scanStatus != riftwii::wii::kScanReady) return FlatCapped(scanStatus, 90);
		return state.model.packages.empty() ? tr("No mods on the SD card. Put Riivolution XML in sd:/riivolution.")
						    : tr("No mods for this game.");
	}
	std::string names;
	for (const riftwii::LaunchPackage& p : state.model.packages)
		if (riftwii::show_package(p) && p.valid && p.enabled) names += (names.empty() ? "" : ", ") + PackLabel(p);
	if (names.empty())
		return shown == 1 ? tr("1 mod pack for this game. Press A to turn it on.")
				  : tr("{1} mod packs for this game. Press A to turn them on.", {std::to_string(shown)});
	return FlatCapped(tr("On: {1}", {names}), 90);
}

// ---------------------------------------------------------------------------
// Add a code build: the SD card's folders, to pick a build's code file
// (riftwii/codebuilds.txt keeps it). True with its key when one was picked.

static bool MenuPickCodes(const FrontendState& state, std::string& key)
{
	std::string folder = "sd:";
	std::vector<riftwii::wii::CodeBrowseEntry> entries;
	std::vector<FlowRow> rows;
	const std::string wanted = state.game_id + ".gct";
	const auto fill = [&]() {
		entries = riftwii::wii::BrowseForCodes(folder + "/");
		// The game's own code file first.
		std::stable_partition(entries.begin(), entries.end(), [&](const riftwii::wii::CodeBrowseEntry& e) {
			return !e.folder && strcasecmp(e.name.c_str(), wanted.c_str()) == 0;
		});
		rows.clear();
		if (folder != "sd:") {
			FlowRow up;
			up.kind = FlowRow::Kind::Action;
			up.label = "..  (up a folder)";
			rows.push_back(up);
		}
		for (const riftwii::wii::CodeBrowseEntry& e : entries) {
			FlowRow row;
			row.kind = FlowRow::Kind::Action;
			row.label = e.folder ? e.name + "/" : e.name;
			if (!e.folder) {
				row.value = "Pick";
				row.on = strcasecmp(e.name.c_str(), wanted.c_str()) == 0;
			}
			rows.push_back(row);
		}
		if (rows.empty()) {
			FlowRow none;
			none.label = "Nothing here";
			none.dim = true;
			rows.push_back(none);
		}
	};
	fill();

	GuiText titleTxt(tr("Add a code build"), 30, skin::kInk);
	Place(titleTxt, 78, 28);
	TitleBand titleBand;
	PageIcon pageIcon(&skin::iconMods);
	std::string whereText = folder + "/";
	GuiText whereTxt(whereText.c_str(), 16, skin::kInkDim);
	whereTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	whereTxt.SetPosition(-40, 40);
	NoteCard noteCard;
	GuiFlowList list(44, 84, 540, 5);
	list.SetCards(true);
	list.SetRows(&rows);
	// In a folder, the first entry (the game's code file when it is there),
	// not the way up.
	const auto firstRow = [&]() { return folder != "sd:" && rows.size() > 1 ? 1 : 0; };
	list.Select(firstRow());
	GuiText noteTxt("", 15, skin::kInkSoft);
	Place(noteTxt, NoteCard::kTextX, NoteCard::kTextY);
	noteTxt.SetWrap(true, NoteCard::kTextW, NoteCard::kLines);
	const std::string help = "Open the build's folder and pick its code file (" + state.game_id +
		".gct). Its gameconfig.txt is read from the same folder, a folder above it or the top of the card.";
	noteTxt.SetText(help.c_str());
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	HideButton(backBtn);
	GuiPrompts prompts;
	const int promptBack = prompts.Add("B", tr("Back"));
	prompts.Add("A", tr("Select"));

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&pageIcon);
	w.Append(&titleTxt);
	w.Append(&whereTxt);
	w.Append(&noteCard);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
	w.Append(&prompts);
	mainWindow->Append(&w);
	ResumeGui();

	bool picked = false;
	bool done = false;
	while (!done)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button});
		const int acted = list.GetClicked();
		if (acted >= 0 && static_cast<std::size_t>(acted) < rows.size() && !rows[static_cast<std::size_t>(acted)].dim) {
			std::size_t at = static_cast<std::size_t>(acted);
			bool moved = false;
			if (folder != "sd:" && at == 0) {
				folder = folder.substr(0, folder.rfind('/'));
				moved = true;
			} else {
				if (folder != "sd:") --at;
				const riftwii::wii::CodeBrowseEntry e = entries[at];
				if (e.folder) {
					folder += "/" + e.name;
					moved = true;
				} else {
					std::string error;
					if (riftwii::wii::PickCodeBuild(state.game_id, folder + "/" + e.name, key, error)) {
						picked = true;
						done = true;
					} else {
						noteTxt.SetText(FlatCapped(error, 200).c_str());
					}
				}
			}
			if (moved) {
				fill();
				whereText = folder + "/";
				whereTxt.SetText(whereText.c_str());
				list.Refresh();
				list.Select(firstRow());
				noteTxt.SetText(help.c_str());
			}
		}
		if (prompts.GetClicked() == promptBack) backBtn.button.SetState(STATE::CLICKED);
		if (backBtn.Clicked()) done = true;
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return picked;
}

// ---------------------------------------------------------------------------
// Mods: the packs made for the game, opened from the game page.

// A mod's picture beside its row while it is pointed at (or focused): the
// PNG next to the mod (wii/menu/modpicture.hpp), else the game's own cover,
// else the disc; with no picture of its own, the file name to add. Fades
// in and out like the hover names.
// A mod's own picture, small, below the list beside Back: never over a
// row or its switch (a tester's pointer kept landing on it), and only
// for a mod that has one (the "Add a picture" stand-in confused).
class ModPicturePopup : public GuiElement {
public:
	static constexpr int kInset = 4;
	static constexpr int kCaption = 38;
	static constexpr float kScale = 0.55f;
	static constexpr int kPicW = static_cast<int>(riftwii::wii::kModPictureW * kScale);
	static constexpr int kPicH = static_cast<int>(riftwii::wii::kModPictureH * kScale);
	static constexpr int kW = kPicW + 2 * kInset;
	static constexpr int kH = kPicH + 2 * kInset + kCaption;

	explicit ModPicturePopup(std::string gameId)
		: gameId(std::move(gameId)), caption(tr("Add a picture:"), 13, skin::kInkDim), name("", 14, skin::kInkSoft) {
		// Without a parent a text's position is the screen's: centred on x.
		caption.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		name.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		caption.SetMaxWidth(kW - 8);
	}
	// What to show, beside a row whose top is at `rowTop`: below it, else
	// above it (beside the Back button there is room down to the bottom).
	void Show(const u8* picture, const std::string& hint, int rowTop) {
		(void)hint;
		(void)rowTop;
		tex = picture;
		hinted = false;
		if (!picture) {
			wanted = false;
			return;
		}
		x = 590 - kW;
		y = 312;
		wanted = true;
	}
	void Hide() { wanted = false; }
	// Whether it is up (or coming up) across screen rows `top` to `bottom`,
	// and its frame's left edge: text below the list keeps clear of it.
	bool Covers(int top, int bottom) const {
		return wanted && y - riftwii::kHintBoxMargin < bottom && y + (hinted ? kH : kH - kCaption) + riftwii::kHintBoxMargin > top;
	}
	int Left() const { return x - riftwii::kHintBoxMargin; }
	// Once a loop pass: the fade.
	void Step() { alpha = std::max(0, std::min(255, alpha + (wanted ? 40 : -40))); }
	void Draw() override {
		if (alpha <= 0) return;
		// The hint's lines only when there is a hint.
		const skin::Tex frame = skin::ArtFrame(kW, hinted ? kH : kH - kCaption);
		skin::Draw(frame, x - riftwii::kHintBoxMargin, y - riftwii::kHintBoxMargin, alpha);
		const int px = x + kInset, py = y + kInset;
		if (tex) {
			// Menu_DrawImg scales about the picture's middle: put it on the box's.
			Menu_DrawImg(px + kPicW / 2.0f - riftwii::wii::kModPictureW / 2.0f,
				py + kPicH / 2.0f - riftwii::wii::kModPictureH / 2.0f, riftwii::wii::kModPictureW,
				riftwii::wii::kModPictureH, const_cast<u8*>(tex), 0, kScale, kScale, static_cast<u8>(alpha));
		} else if (const u8* cover = riftwii::wii::CoverTexture(gameId)) {
			// 80x112 at 1.5 fills the 120x168 box; it scales about its centre.
			skin::DrawRgb5a3(cover, riftwii::kCoverWidth, riftwii::kCoverHeight,
				px + (riftwii::wii::kModPictureW - riftwii::kCoverWidth) / 2.0f,
				py + (riftwii::wii::kModPictureH - riftwii::kCoverHeight) / 2.0f, alpha, 1.5f);
		} else if (skin::discCase.data) {
			// No cover (the disc's game not read yet): the disc drive's box, the box's height.
			const float k = static_cast<float>(riftwii::wii::kModPictureH) / skin::discCase.h;
			skin::Draw(skin::discCase, px + (riftwii::wii::kModPictureW - skin::discCase.w) / 2.0f,
				py + (riftwii::wii::kModPictureH - skin::discCase.h) / 2.0f, alpha, k);
		} else {
			skin::Draw(skin::iconDisc, px + (riftwii::wii::kModPictureW - skin::iconDisc.w) / 2,
				py + (riftwii::wii::kModPictureH - skin::iconDisc.h) / 2, alpha);
		}
		if (!hinted) return;
		const int ty = py + riftwii::wii::kModPictureH + 4;
		caption.SetPosition(x + kW / 2, ty);
		name.SetPosition(x + kW / 2, ty + 16);
		caption.SetAlpha(alpha);
		name.SetAlpha(alpha);
		caption.Draw();
		name.Draw();
	}

private:
	std::string gameId;
	GuiText caption, name;
	const u8* tex = nullptr;
	int x = 0, y = 0, alpha = 0;
	bool wanted = false, hinted = false;
};

static void MenuMods(FrontendState& state, std::string& scanStatus)
{
	std::vector<FlowRow> rows;
	std::vector<RowRef> refs;
	BuildModRows(state, scanStatus, rows, refs);

	GuiText titleTxt(tr("Mods"), 30, skin::kInk);
	Place(titleTxt, 78, 28);
	TitleBand titleBand;
	PageIcon pageIcon(&skin::iconMods);
	const std::string gameName = FlatCapped(GameTitle(state), 40);
	GuiText gameTxt(gameName.c_str(), 16, skin::kInkDim);
	gameTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	gameTxt.SetPosition(-40, 40);
	NoteCard noteCard;
	GuiFlowList list(44, 84, 540, 5);
	list.SetCards(true);
	list.SetRows(&rows);
	list.Select(0);
	GuiText noteTxt("", 15, skin::kInkSoft);
	Place(noteTxt, NoteCard::kTextX, NoteCard::kTextY);
	noteTxt.SetWrap(true, NoteCard::kTextW, NoteCard::kLines);
	int noteWidth = NoteCard::kTextW;  // narrower while the picture is beside it
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	HideButton(backBtn);
	GuiPrompts prompts;
	const int promptBack = prompts.Add("B", tr("Back"));
	prompts.Add("A", tr("Select"));
	ModPicturePopup picture(state.game_id);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&pageIcon);
	w.Append(&titleTxt);
	w.Append(&gameTxt);
	w.Append(&noteCard);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
	w.Append(&prompts);
	w.Append(&picture);
	mainWindow->Append(&w);
	ResumeGui();

	const auto say = [&](const std::string& text) { noteTxt.SetText(text.c_str()); };
	int shownRow = -1;
	// The picture comes up once the pointer rests on a mod's row a moment.
	int pictureRow = -1, pictureRest = 0;
	bool done = false;
	while (!done)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button});

		const int row = list.Selected();
		{
			const bool modRow = row >= 0 && static_cast<std::size_t>(row) < refs.size() &&
					    refs[static_cast<std::size_t>(row)].what == RowRef::What::Pack &&
					    state.model.packages[refs[static_cast<std::size_t>(row)].pkg].valid;
			// The pointer off the list (moved away fast): no picture.
			bool pointerOff = false;
			for (int i = 0; i < 4; ++i) {
				const WPADData* p = userInput[i].wpad;
				if (p->ir.valid && (p->ir.y < 82 || p->ir.y > 82 + 5 * GuiFlowList::kRowHeight)) pointerOff = true;
			}
			if (row != pictureRow || pointerOff) {
				pictureRow = row;
				pictureRest = 0;
				picture.Hide();
			} else if (modRow && ++pictureRest == 20) {
				const riftwii::LaunchPackage& p = state.model.packages[refs[static_cast<std::size_t>(row)].pkg];
				picture.Show(riftwii::wii::ModPicture(p), riftwii::wii::ModPictureName(p), list.RowTop(row));
			}
			picture.Step();
			// The note's lines wrap short of the picture.
			const int width = picture.Covers(NoteCard::kY, NoteCard::kY + NoteCard::kH)
				? picture.Left() - 8 - NoteCard::kTextX : NoteCard::kTextW;
			if (width != noteWidth) {
				noteWidth = width;
				noteTxt.SetWrap(true, width, NoteCard::kLines);
			}
		}
		if (row != shownRow && row >= 0 && static_cast<std::size_t>(row) < refs.size()) {
			shownRow = row;
			const RowRef& ref = refs[static_cast<std::size_t>(row)];
			if (ref.what == RowRef::What::Pack) {
				// Named by its section: the file's name too, to find it on the card.
				const riftwii::LaunchPackage& p = state.model.packages[ref.pkg];
				say(PackSummary(p) + (riftwii::pack_title(p).empty() ? std::string() : "\n" + p.file));
			}
			else if (ref.what == RowRef::What::Option) {
				const riftwii::Option& o = state.model.packages[ref.pkg].package.options[ref.opt];
				say(o.section.empty() ? PackName(state.model.packages[ref.pkg].file) : o.section);
			} else if (ref.what == RowRef::What::AddCodes) {
				say("For mods made of Gecko codes, like Project+ builds: pick the build's code file on the SD card.");
			} else if (ref.what == RowRef::What::ForgetCodes) {
				say("Takes this build off the list. Its files stay on the SD card.");
			} else if (ref.what == RowRef::What::MakeImage) {
				say("Copies this build into an image in sd:/riftwii, a card of its own for the game, so the game can be on the SD card too.");
			} else if (ref.what == RowRef::What::SaveTile) {
				say(tr("Puts this game on Home again as a tile of its own, which starts it with the mods and settings set here."));
			} else if (ref.what == RowRef::What::RenameTile) {
				say(tr("This page is the \"{1}\" tile's: what is changed here is changed for it only.", {state.profile_name}));
			} else if (ref.what == RowRef::What::RemoveTile) {
				say(tr("Takes the \"{1}\" tile off Home. The game and its mods stay.", {state.profile_name}));
			} else say("");
		}

		int acted = list.GetClicked();
		int direction = +1;
		if (acted < 0) {
			acted = list.GetClickedBack();
			direction = -1;
		}
		if (acted >= 0 && static_cast<std::size_t>(acted) < refs.size()) {
			const RowRef ref = refs[static_cast<std::size_t>(acted)];
			bool changed = false;
			if (ref.what == RowRef::What::Pack) {
				riftwii::LaunchPackage& p = state.model.packages[ref.pkg];
				if (!p.valid) say("This XML cannot be read; fix it on the card and come back.");
				else changed = state.model.set_enabled(ref.pkg, !p.enabled);
				if (!changed && p.valid) say("This pack cannot be turned on.");
				// The game gets one SD card: a code build turned on turns off
				// the ones on another card (another .raw image, or the SD card).
				if (changed && p.enabled && p.code_build()) {
					const auto card = [](const riftwii::LaunchPackage& b) {
						return b.gct_path.compare(0, 5, "vsd:/") == 0 ? riftwii::wii::VsdImageOfKey(b.file) : std::string();
					};
					const std::string mine = card(p);
					bool others = false;
					for (std::size_t i = 0; i < state.model.packages.size(); ++i) {
						const riftwii::LaunchPackage& o = state.model.packages[i];
						if (i == ref.pkg || !o.enabled || !o.code_build() || strcasecmp(card(o).c_str(), mine.c_str()) == 0) continue;
						state.model.set_enabled(i, false);
						others = true;
					}
					if (others) say(mine.empty() ? "Turned off the code builds in .raw images: the game gets the SD card."
								      : "Turned off the other code builds: the game gets " + mine + " as its SD card.");
				}
			} else if (ref.what == RowRef::What::Option) {
				changed = state.model.cycle(ref.pkg, ref.opt, direction);
			} else if (ref.what == RowRef::What::SaveTile || ref.what == RowRef::What::RenameTile) {
				const bool renaming = ref.what == RowRef::What::RenameTile;
				std::string name = renaming ? state.profile_name : SuggestedTileName(state);
				std::string key, error;
				if (!AskTileName(renaming ? tr("Rename the tile") : tr("Name the new tile"), name)) {
					// cancelled
				} else if (renaming ? riftwii::wii::RenameProfile(state, name, error)
						    : riftwii::wii::CreateProfile(state, name, key, error)) {
					const std::string named = renaming ? state.profile_name : riftwii::wii::Profiles().find(key)->name;
					say(renaming ? tr("Renamed \"{1}\".", {named})
						     : tr("\"{1}\" is on Home now: its tile starts the game with these mods. Its own picture can go in sd:/riftwii/profiles/{2}.png.", {named, key}));
					gameTxt.SetText(FlatCapped(GameTitle(state), 40).c_str());
					shownRow = acted;  // the answer stays up
				} else {
					say(error);
					shownRow = acted;
				}
			} else if (ref.what == RowRef::What::RemoveTile) {
				std::string error;
				const std::string name = state.profile_name;
				if (ShowPopup(tr("Remove this tile?"),
					    tr("\"{1}\" goes from Home, with its own mods and settings, and its picture in sd:/riftwii/profiles if it has one. The game and its mods stay.", {name}),
					    tr("Remove"), tr("Cancel")) != 0) {
					// kept
				} else if (riftwii::wii::RemoveProfile(state, error)) {
					// The page is the game's own again: its choices are read back.
					scanStatus = riftwii::wii::ScanPackages(state);
					BuildModRows(state, scanStatus, rows, refs);
					list.Refresh();
					list.Select(0);
					gameTxt.SetText(FlatCapped(GameTitle(state), 40).c_str());
					say(tr("Removed \"{1}\". This page is the game's own again.", {name}));
					shownRow = 0;
				} else {
					say(error);
				}
			} else if (ref.what == RowRef::What::AddCodes || ref.what == RowRef::What::ForgetCodes ||
				   ref.what == RowRef::What::MakeImage) {
				std::string key, error;
				bool listChanged = false;
				if (ref.what == RowRef::What::AddCodes) {
					transition::Hold(transition::Kind::SlideForward);
					mainWindow->Remove(&w);
					listChanged = MenuPickCodes(state, key);
					transition::Hold(transition::Kind::SlideBack);
					mainWindow->Append(&w);
				} else if (ref.what == RowRef::What::MakeImage) {
					listChanged = MakeSdImage(state, state.model.packages[ref.pkg], key);
				} else if (riftwii::wii::ForgetCodeBuild(state.model.packages[ref.pkg].file, error)) {
					listChanged = true;
				} else {
					say(error);
				}
				if (listChanged) {
					scanStatus = riftwii::wii::ScanPackages(state);
					// A new pick, or the build in its new image, is turned on
					// at once; the game gets one card, so a build in an image
					// turns off the ones on the SD card.
					for (std::size_t i = 0; i < state.model.packages.size(); ++i)
						if (!key.empty() && state.model.packages[i].file == key && !state.model.packages[i].enabled)
							state.model.set_enabled(i, true);
					if (ref.what == RowRef::What::MakeImage && !key.empty()) {
						for (std::size_t i = 0; i < state.model.packages.size(); ++i) {
							const riftwii::LaunchPackage& o = state.model.packages[i];
							if (o.enabled && o.code_build() && o.file != key &&
							    riftwii::wii::VsdImageOfKey(o.file) != riftwii::wii::VsdImageOfKey(key))
								state.model.set_enabled(i, false);
						}
					}
					changed = true;
				}
			}
			if (changed) {
				std::string error;
				if (!SaveChoices(state, error)) say(error);
				BuildModRows(state, scanStatus, rows, refs);
				list.Refresh();
				list.Select(acted);
				shownRow = -1;
			}
		}
		if (prompts.GetClicked() == promptBack) backBtn.button.SetState(STATE::CLICKED);
		if (backBtn.Clicked()) done = true;
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	riftwii::wii::ForgetModPictures();
}

// ---------------------------------------------------------------------------
// The game page: the game's box, its name and facts, and tiles

// The Play tile's note: what Start does; for a game still to be made from
// its patch, that it is made first, once.
static std::string PlayTileNote(const FrontendState& state)
{
	const riftwii::wii::ImageGame* patch = SelectedGcPatch(state);
	// Said first, as the page opens on Play: the game would start, but
	// could not save.
	if (SelectedGcGame(state) && GcCardFull(state)) return GcCardFullNote(state);
	if (!patch) return tr("Starts the game with the mods and settings shown here.");
	const riftwii::wii::ImageGame* original = GcOriginal(state, *patch);
	const std::string originalName = original ? ImageGameName(*original) : FlatCapped(patch->gc_patch_source, 40);
	return tr("Made from {1} on first start, once; every start after that is immediate.", {originalName});
}

// The Saves tile's note for a GameCube game.
static std::string GcSaveNote(const FrontendState& state)
{
	const std::string card = GcCardPath(state);
	const std::string where = GcCardShared(state)
		? tr("Saves go to the shared memory card on the SD card: {1}.", {card})
		: tr("Saves go to this game's own memory card on the SD card: {1}.", {card});
	// Two lines at most: the full card's note takes the place of where it is.
	if (GcCardFull(state)) return GcCardFullNote(state);
	return where + " " + tr("Press A to see its saves.");
}

// The band's line of facts: the ID, the year, the genre, how many play,
// online, how often it was played here (GameTDB's from the cache only).
static std::string GameFacts(const FrontendState& state)
{
	std::vector<std::string> parts;
	// A profile's page is headed with its own name: which game it starts.
	if (!state.profile_key.empty()) parts.push_back(GameOwnTitle(state));
	if (!state.game_id.empty()) parts.push_back(state.game_id);
	if (SelectedGcGame(state)) parts.push_back(tr("GameCube"));
	riftwii::GameInfo info;
	if (!state.game_id.empty() && riftwii::wii::PeekGameInfo(state.game_id, info)) {
		if (info.year) parts.push_back(std::to_string(info.year));
		if (!info.genres.empty()) parts.push_back(GenreText({info.genres[0]}));
		if (info.players == 1) parts.push_back(tr("1 player"));
		else if (info.players > 1) parts.push_back(tr("1 to {1} players", {std::to_string(info.players)}));
		if (info.online_players > 0) parts.push_back(tr("Online"));
	}
	const std::string played = riftwii::wii::PlayNote(riftwii::wii::TileKey(state));
	if (!played.empty()) parts.push_back(played);
	std::string out;
	for (const std::string& part : parts) out += (out.empty() ? "" : " \xC2\xB7 ") + part;
	return out;
}

// How many of the game's own settings differ from Settings.
static int ChangedGameSettings(const riftwii::GameSettings& g)
{
	int changed = 0;
	for (const std::string* v : {&g.video_width, &g.deflicker, &g.borders, &g.video_mode, &g.aspect, &g.language,
		     &g.cios, &g.server})
		if (*v != "global") ++changed;
	if (g.region_video == "on") ++changed;
	if (g.region_strings == "on") ++changed;
	if (riftwii::kGcDriveTimingReady && g.gc_disc_reads == "drive") ++changed;
	if (g.gc_card == "shared") ++changed;
	if (g.gc_progressive != "global") ++changed;
	if (riftwii::kGcWidescreenReady && g.gc_widescreen != "global") ++changed;
	if (g.gc_language != "global") ++changed;
	if (g.gc_sharp == "on") ++changed;
	if (g.gc_width != "global") ++changed;
	if (g.gc_speed == "60") ++changed;
	if (g.rumble == "off") ++changed;
	if (g.speaker == "off") ++changed;
	return changed;
}

enum class GameTile { Play, Mods, Cheats, Saves, Favorite, Options, About };

// The game page's tiles: Play first and largest, the mods beside it, then
// what is changed most (cheats, saves), then the rest; the favourite
// switch under the box.
static void BuildGameTiles(const FrontendState& state, std::vector<TileSpec>& tiles, std::vector<GameTile>& what)
{
	tiles.clear();
	what.clear();
	const auto add = [&](GameTile kind, int x, int y, int w, int h, const std::string& label, const std::string& value,
				 const skin::Tex* icon, bool on) {
		TileSpec t;
		t.x = x;
		t.y = y;
		t.w = w;
		t.h = h;
		t.label = label;
		t.value = value;
		t.icon = icon;
		t.on = on;
		t.primary = kind == GameTile::Play;
		tiles.push_back(t);
		what.push_back(kind);
	};
	std::size_t enabled = 0;
	const std::size_t shown = ShownPacks(state, &enabled);
	const std::string modsOn = enabled == 0 ? std::string() : tr("Mods on: {1}", {std::to_string(enabled)});
	const riftwii::GameSettings& game = state.model.game;
	const int changed = ChangedGameSettings(game);
	const bool favorite = riftwii::wii::Settings().favorites.count(riftwii::wii::TileKey(state)) != 0;
	const riftwii::wii::ImageGame* gamecube = SelectedGcGame(state);
	const riftwii::wii::ImageGame* toBuild = SelectedGcPatch(state);
	// A GameCube game saves to its memory card on the SD card, whatever the
	// save mode. Its cheats are picked as a Wii game's are.
	const bool cardFull = gamecube && GcCardFull(state);
	const std::string saves = cardFull ? std::string(tr("Memory card full"))
		: gamecube ? std::string(tr("Memory Card")) + " \xC2\xB7 SD"
		: !state.model.pack_save_owner().empty() ? std::string(tr("Kept by the pack"))
		: std::string(tr(SaveLabel(state.model.save_mode).c_str()));
	const std::string cheats = CheatsValue(game);
	// Until a patch's game is made: what making it takes, where the mods
	// count would be.
	std::string play = tr("Play"), playValue = modsOn;
	if (toBuild) {
		play = tr("Build and play");
		const std::uint64_t bytes = GcBuildBytes(*toBuild);
		if (bytes != 0)
			playValue = SizeText(bytes) + " \xC2\xB7 " + GcBuildTime(bytes);
	}

	// The lower rows: two tiles across the tiles' width, 12 apart.
	constexpr int kHalfW = (kTilesW - 12) / 2, kRightX = kTilesRight - kHalfW;
	if (state.use_channel) {
		// A WiiWare or Virtual Console game: no packs are made for them,
		// and its saves stay where the Wii keeps them. Play across the
		// row, the cheats and the game's settings under it.
		add(GameTile::Play, kTilesX, 132, kTilesRight - kTilesX, 84, tr("Play"), "", &skin::iconPlay, false);
		add(GameTile::Cheats, kTilesX, 228, kHalfW, 70, tr("Cheats"), CheatsValue(game), &skin::iconCheats,
			game.cheats && !game.cheat_names.empty());
		add(GameTile::Options, kRightX, 228, kHalfW, 70, tr("Options"),
			changed == 0 ? tr("Default") : tr("{1} changed", {std::to_string(changed)}), &skin::iconGear, changed != 0);
		add(GameTile::Favorite, kHeroX, 310, kHeroW, 70, tr("Favourite"), favorite ? tr("On") : tr("Off"),
			favorite ? &skin::iconStarOn : &skin::iconStar, favorite);
		add(GameTile::About, kTilesX, 310, kHalfW, 70, tr("About"), "", &skin::iconInfo, false);
		return;
	}
	add(GameTile::Play, kTilesX, 132, 252, 84, play, playValue, &skin::iconPlay, false);
	// "On" alone: the tile is narrow ("1 switched on" was cut to "1 switc..."),
	// and Play beside it says how many.
	add(GameTile::Mods, kTilesX + 264, 132, kTilesRight - kTilesX - 264, 84, tr("Mods"),
		shown == 0 ? tr("None") : enabled == 0 ? tr("Off") : tr("On"),
		&skin::iconMods, enabled != 0);
	add(GameTile::Cheats, kTilesX, 228, kHalfW, 70, tr("Cheats"), cheats, &skin::iconCheats,
		game.cheats && !game.cheat_names.empty());
	add(GameTile::Saves, kRightX, 228, kHalfW, 70, tr("Saves"), saves, &skin::iconSaves,
		!gamecube && state.model.save_mode != "nand");
	// A full card is a warning, in the theme's colour for one.
	tiles.back().warn = cardFull;
	add(GameTile::Favorite, kHeroX, 310, kHeroW, 70, tr("Favourite"), favorite ? tr("On") : tr("Off"),
		favorite ? &skin::iconStarOn : &skin::iconStar, favorite);
	add(GameTile::Options, kTilesX, 310, kHalfW, 70, tr("Options"),
		changed == 0 ? tr("Default") : tr("{1} changed", {std::to_string(changed)}), &skin::iconGear, changed != 0);
	add(GameTile::About, kRightX, 310, kHalfW, 70, tr("About"), "", &skin::iconInfo, false);
}

// A game's own settings (the game page's Game settings tile): the picture,
// the language, the online server and the rest, as a list: they are many,
// and each is changed with Left and Right. `coverAsked`: the Cover row was
// pressed, and the game page downloads it.
static void MenuGameSettings(FrontendState& state, bool& coverAsked)
{
	std::vector<FlowRow> rows;
	std::vector<RowRef> refs;
	BuildGameRows(state, rows, refs, true);

	GuiText titleTxt(tr("Game settings"), 30, skin::kInk);
	Place(titleTxt, 78, 28);
	TitleBand titleBand;
	PageIcon pageIcon(&skin::iconGear);
	const std::string gameName = FlatCapped(GameTitle(state), 40);
	GuiText gameTxt(gameName.c_str(), 16, skin::kInkDim);
	gameTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	gameTxt.SetPosition(-40, 40);
	NoteCard noteCard;
	GuiFlowList list(44, 84, 540, 5);
	list.SetCards(true);
	list.SetRows(&rows);
	list.Select(1);
	GuiText noteTxt("", 15, skin::kInkSoft);
	Place(noteTxt, NoteCard::kTextX, NoteCard::kTextY);
	noteTxt.SetWrap(true, NoteCard::kTextW, NoteCard::kLines);
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	HideButton(backBtn);
	GuiPrompts prompts;
	const int promptBack = prompts.Add("B", tr("Back"));
	const int promptChange = prompts.Add("A", tr("Change"));

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&pageIcon);
	w.Append(&titleTxt);
	w.Append(&gameTxt);
	w.Append(&noteCard);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
	w.Append(&prompts);
	mainWindow->Append(&w);
	ResumeGui();

	const auto say = [&](const std::string& text) { noteTxt.SetText(text.c_str()); };
	int shownRow = -1;
	bool done = false;
	while (!done)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button});

		const int row = list.Selected();
		if (row != shownRow && row >= 0 && static_cast<std::size_t>(row) < refs.size()) {
			shownRow = row;
			const RowRef& ref = refs[static_cast<std::size_t>(row)];
			// A dimmed row only says why it cannot change.
			const bool fixedRow = static_cast<std::size_t>(row) < rows.size() && rows[static_cast<std::size_t>(row)].dim;
			prompts.SetAction(promptChange, fixedRow ? tr("About this") : tr("Change"));
			if (ref.what == RowRef::What::Width)
				say(tr("How wide the picture is drawn. 720 fills the screen from side to side."));
			else if (ref.what == RowRef::What::Deflicker)
				say(tr("A filter that softens the picture to hide flicker. Off gives the sharpest picture."));
			else if (ref.what == RowRef::What::Borders) {
				const std::string seen = riftwii::wii::BorderNote(state.game_id);
				const std::string how = tr("Remove takes away the bars at the sides; Remove all also the top and bottom (experimental).");
				say(seen.empty() ? how : seen + " " + how);
			}
			else if (ref.what == RowRef::What::VideoMode)
				say(tr("The TV signal the game sends. PAL 50 Hz needs a TV that takes it, 480p a component cable."));
			else if (ref.what == RowRef::What::RegionVideo)
				say(tr("For a US or Japanese game that shows no picture on a console from another region: the game is told the video hardware matches its region."));
			else if (ref.what == RowRef::What::Aspect)
				say(tr("Makes the game use 4:3 or widescreen 16:9 whatever the Wii's TV setting says. Not every game can be changed."));
			else if (ref.what == RowRef::What::Rumble)
				say(tr("Off: the Wii Remotes never rumble in this game."));
			else if (ref.what == RowRef::What::Speaker)
				say(tr("Off: no sound from the Wii Remotes' speakers in this game."));
			else if (ref.what == RowRef::What::RegionStrings)
				say(tr("For a game from another region (an import): the game sees its own region's country names where it looks for the console's."));
			else if (ref.what == RowRef::What::Language)
				say(tr("The language the game is told the console uses. Pick one the game has: some games stop without it."));
			else if (ref.what == RowRef::What::Cover)
				say(tr("Downloads this game's box art from GameTDB now."));
			else if (ref.what == RowRef::What::GcDiscReads)
				say(tr("Fast suits almost every game; a few need the GameCube's own, slower speed (experimental, it can freeze some games)."));
			else if (ref.what == RowRef::What::GcCard)
				say(tr("Save to a card for this game alone, or to one card shared by all your games."));
			else if (ref.what == RowRef::What::GcProgressive && riftwii::gc_pal_game(state.game_id))
				say(tr("480p is for US and Japanese games; this European game keeps its own picture."));
			else if (ref.what == RowRef::What::GcProgressive && state.model.game.gc_progressive == "global" &&
				 !riftwii::gc_progressive_on(state.model.game, state.game_id, riftwii::wii::GcConsoleNow()))
				say(tr("Default turns 480p on when the Wii is set to 480p and a component cable is seen; pick On (480p) if your TV takes it anyway."));
			else if (ref.what == RowRef::What::GcProgressive)
				say(tr("Gives a sharper, steadier picture on a component cable, even in games with no 480p mode; some games ask first."));
			else if (ref.what == RowRef::What::GcWidescreen)
				say(tr("Fills a widescreen TV with the game world; menus stay stretched, and some games show wrong colors (experimental)."));
			else if (ref.what == RowRef::What::GcSharp)
				say(tr("Turns off the soft blur some games add."));
			else if (ref.what == RowRef::What::GcWidth)
				say(tr("How wide the picture is drawn. 720 fills the screen from side to side."));
			else if (ref.what == RowRef::What::GcSpeed)
				say(tr("Smoother plays at 60 Hz in games that have a 60 Hz mode, on a TV that takes it; the others stay at 50 Hz."));
			else if (ref.what == RowRef::What::GcLanguage)
				say(tr("The language this game plays in, leaving the Wii's own setting alone (most US and Japanese games have only one)."));
			else if (ref.what == RowRef::What::GcControls)
				say(tr("Shows which Wii Remote, Nunchuk or Classic Controller buttons act as the GameCube controller's, and how to leave the game."));
			else if (ref.what == RowRef::What::Memory) {
				const riftwii::ExtrasBudget budget = riftwii::wii::EstimateExtras(state);
				std::string text = tr("Taken from the game's own memory by what RiftWii adds to this launch: {1}.", {budget.list()});
				if (budget.heavy())
					text += std::string(" ") + tr("That is a lot: if the game crashes or stays black only with RiftWii, turn off what it does not need (screenshots, the GameCube adapter, mods) and try again.");
				say(text);
			}
			else if (ref.what == RowRef::What::Cios)
				say(tr("The d2x cIOS the game runs under. Automatic uses the menu's, else the first of 249, 250 and 251 that works."));
			else if (ref.what == RowRef::What::Server && riftwii::game_has_no_online(state.game_id))
				say(tr("This game has no online play, so it needs no server."));
			else if (ref.what == RowRef::What::Server && state.model.packs_patch_game())
				say(tr("With a pack on, the game keeps the pack's own online setup. Turn the packs off to choose here."));
			else if (ref.what == RowRef::What::Server)
				say(tr("The online server the game uses in place of Nintendo's, which closed. Custom uses wfc_domain in settings.txt."));
			else say("");
		}

		int acted = list.GetClicked();
		int direction = +1;
		if (acted < 0) {
			acted = list.GetClickedBack();
			direction = -1;
		}
		if (acted >= 0 && static_cast<std::size_t>(acted) < refs.size()) {
			const RowRef ref = refs[static_cast<std::size_t>(acted)];
			riftwii::GameSettings& game = state.model.game;
			bool changed = true;
			if (ref.what == RowRef::What::Width) game.video_width = StepValue(kWidths, game.video_width, direction);
			else if (ref.what == RowRef::What::Deflicker) game.deflicker = StepValue(kDeflickers, game.deflicker, direction);
			else if (ref.what == RowRef::What::Borders) game.borders = StepValue(kBorderModes, game.borders, direction);
			else if (ref.what == RowRef::What::VideoMode) game.video_mode = StepValue(kVideoModes, game.video_mode, direction);
			else if (ref.what == RowRef::What::RegionVideo) game.region_video = game.region_video == "on" ? "off" : "on";
			else if (ref.what == RowRef::What::Aspect) game.aspect = StepValue(kAspects, game.aspect, direction);
			else if (ref.what == RowRef::What::Rumble) game.rumble = game.rumble == "off" ? "on" : "off";
			else if (ref.what == RowRef::What::Speaker) game.speaker = game.speaker == "off" ? "on" : "off";
			else if (ref.what == RowRef::What::RegionStrings) game.region_strings = game.region_strings == "on" ? "off" : "on";
			else if (ref.what == RowRef::What::Language) game.language = StepValue(kGameLanguages, game.language, direction);
			else if (ref.what == RowRef::What::Cios) game.cios = StepValue(kCiosChoices, game.cios, direction);
			else if (ref.what == RowRef::What::GcDiscReads) game.gc_disc_reads = game.gc_disc_reads == "fast" ? "drive" : "fast";
			else if (ref.what == RowRef::What::GcCard) {
				// Asked when switching, where it matters: the saves stay behind.
				if (AskGcCardSwitch()) game.gc_card = game.gc_card == "shared" ? "own" : "shared";
				else changed = false;
			}
			else if (ref.what == RowRef::What::GcProgressive && riftwii::gc_pal_game(state.game_id)) {
				// The row is dimmed: nothing to change, only why.
				say(tr("480p is for US and Japanese games; this European game keeps its own picture."));
				changed = false;
			}
			else if (ref.what == RowRef::What::GcProgressive) {
				std::string next = StepValue(kGcProgressiveChoices, game.gc_progressive, direction);
				// Without a component cable seen nothing says the TV takes
				// 480p (the Wii's own setting may be from another cable):
				// asked once, when 480p is picked; Cancel goes on past it.
				riftwii::GameSettings picked = game;
				picked.gc_progressive = next;
				const riftwii::GcConsoleSettings console = riftwii::wii::GcConsoleNow();
				if (!console.component_cable && riftwii::gc_progressive_on(picked, state.game_id, console) &&
				    !AskGc480p())
					next = StepValue(kGcProgressiveChoices, next, direction);
				game.gc_progressive = next;
			}
			else if (ref.what == RowRef::What::GcWidescreen) game.gc_widescreen = StepValue(kGcWidescreenChoices, game.gc_widescreen, direction);
			else if (ref.what == RowRef::What::GcSharp) game.gc_sharp = game.gc_sharp == "on" ? "off" : "on";
			else if (ref.what == RowRef::What::GcWidth) game.gc_width = StepValue(kWidths, game.gc_width, direction);
			else if (ref.what == RowRef::What::GcSpeed) game.gc_speed = game.gc_speed == "60" ? "50" : "60";
			else if (ref.what == RowRef::What::GcLanguage) game.gc_language = StepValue(kGcLanguageChoices, game.gc_language, direction);
			else if (ref.what == RowRef::What::GcControls) {
				ShowGcControls(false);
				changed = false;
			}
			else if (ref.what == RowRef::What::Server && riftwii::game_has_no_online(state.game_id)) {
				say(tr("This game has no online play, so it needs no server."));
				changed = false;
			} else if (ref.what == RowRef::What::Server && state.model.packs_patch_game()) {
				say(tr("With a pack on, the game keeps the pack's own online setup. Turn the packs off to choose here."));
				changed = false;
			} else if (ref.what == RowRef::What::Server) {
				game.server = StepValue(kServers, game.server, direction);
			} else if (ref.what == RowRef::What::Cover && !state.game_id.empty()) {
				// Back on the game page, which shows the cover as it comes.
				coverAsked = true;
				done = true;
				changed = false;
			} else {
				changed = false;
			}
			if (changed) {
				std::string error;
				if (!SaveChoices(state, error)) say(error);
				BuildGameRows(state, rows, refs, true);
				list.Refresh();
				list.Select(acted);
				shownRow = -1;
			}
		}
		if (prompts.GetClicked() == promptBack) backBtn.button.SetState(STATE::CLICKED);
		if (backBtn.Clicked()) done = true;
		ResumeGui();
	}
	HaltGui();
	mainWindow->Remove(&w);
}

static int MenuHome(FrontendState& state)
{
	int menu = MENU_NONE;

	// Start on a banner, which plays on meanwhile: the packs are read from
	// the card and the rows worked out with the GUI going (none of it is
	// on screen), which stopped the banner otherwise. The widgets are made
	// with it halted, as ever.
	// A tester's Wii froze here with the Indigo theme. With settings.txt's
	// debug_gx on, the page's steps are logged and, for the next minute,
	// the menu watchdog logs where the GUI thread is once a second
	// (wii/menu/drawtrace.hpp).
	if (riftwii::wii::g_gxWatch) {
		riftwii::wii::DrawTraceArm(60);
		logf("Game page: building\n");
	}
	const bool buildBehindBanner = g_startOnOpen && g_lingerBanner;
	if (buildBehindBanner) ResumeGui();
	std::string scanStatus;
	try {
		scanStatus = ScanPackages(state);
	} catch (...) {
		scanStatus = "Package scan failed; go back and try again";
	}
	// A disc's game (or one Home did not reach yet): its cover, on the
	// network's thread (the loop below starts it and takes the answer).
	bool coverStart = riftwii::wii::Settings().online && riftwii::wii::Settings().home_tiles != "names" &&
	                  !g_coversOff && !riftwii::wii::NetFailed() && g_coversChecked.insert(state.game_id).second &&
	                  riftwii::wii::CoverWanted(state.game_id);
	// The player asked with the Cover row: the answer is said, and it is
	// asked even after downloads stopped.
	bool coverAsked = false;
	// The shelf's box, larger, when Home has not fetched it yet.
	bool boxStart = riftwii::wii::Settings().online && riftwii::wii::Settings().home_tiles != "names" &&
	                !g_coversOff && !riftwii::wii::NetFailed() && riftwii::wii::BoxWanted(state.game_id);
	std::vector<TileSpec> tiles;
	std::vector<GameTile> tileWhat;
	BuildGameTiles(state, tiles, tileWhat);
	if (riftwii::wii::g_gxWatch) logf("Game page: tiles built\n");
	if (buildBehindBanner) HaltGui();
	// A channel's page shows its icon (HeroCover): loaded here when Home's
	// Channels view did not, its banner stored from the Wii first if need be.
	if (state.use_channel && state.channel_index < state.channels.size() && !IconFor(state.game_id)) {
		std::unique_ptr<riftwii::wii::BannerPlayer> icon = LoadIcon(state.game_id);
		std::string why;
		if (!icon && riftwii::wii::StoreChannelBanner(state.channels[state.channel_index], why))
			icon = LoadIcon(state.game_id);
		if (icon) KeepIcon(state.game_id, std::move(icon));
	}

	// A console's game page: the game's box on the left, over the band's
	// edge; its name and a line of facts in the band; tiles to the right,
	// Play first and largest. All inside a TV's overscan.
	const bool fromDisc = !state.use_usb && !state.use_sd && !state.use_channel;
	GameBanner banner(skin::HueFor(state.game_id), state.game_id, fromDisc);
	HeroCover hero(state.game_id, fromDisc, skin::HueFor(state.game_id), state.use_channel);
	// The band's text, as a store page heads a game: where it is as a small
	// capital overline, the name large (as large as fits on one line, two
	// smaller lines for a long one), the facts under it; each outlined in
	// black over a drop shadow (OutlinedText), so it reads on bright art.
	std::string where = tr(SourceWhere(state).c_str());
	for (char& ch : where)
		if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
	// Centred in the band (y 24, the overscan's edge, to 120), the facts
	// ending some 16 above its lower edge.
	constexpr int kWhereY = 30;
	const GXColor kWhereInk = {225, 228, 235, 255}, kFactsInk = {235, 237, 242, 255};
	GuiText whereTxt(where.c_str(), 13, kWhereInk);
	const std::string title = GameTitle(state);
	GuiText titleTxt(title.c_str(), 28, skin::kWhite);
	int titleSize = 28;
	while (titleSize > 22 && titleTxt.GetTextWidth() > kTilesW) titleTxt.SetFontSize(--titleSize);
	// A name too long for one line: two lines of 19 (to y 88), the facts
	// a little lower.
	const bool titleWraps = titleTxt.GetTextWidth() > kTilesW;
	if (titleWraps) {
		titleSize = 19;
		titleTxt.SetFontSize(titleSize);
		titleTxt.SetWrap(true, kTilesW, 2);
	}
	const int titleY = titleWraps ? 44 : 48, factsY = titleWraps ? 92 : 86;
	const std::string facts = GameFacts(state);
	GuiText factsTxt(facts.c_str(), 15, kFactsInk);
	factsTxt.SetMaxWidth(kTilesW);
	OutlinedText whereOutlined(whereTxt, kTilesX + 1, kWhereY, kWhereInk, 1, 1);
	OutlinedText titleOutlined(titleTxt, kTilesX, titleY, skin::kWhite, 2, 2);
	OutlinedText factsOutlined(factsTxt, kTilesX, factsY, kFactsInk, 1, 1);

	if (riftwii::wii::g_gxWatch) logf("Game page: texts made\n");
	GuiTiles tileGrid;
	tileGrid.SetTiles(&tiles);
	tileGrid.Select(0);


	// B and + (Start) as ever, named by the prompts along the bottom.
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 50, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	SkinButton startBtn(skin::pillPrimary, skin::pillPrimaryOver, 4, 346, 406, "Start",
		WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_START | PAD_BUTTON_X, WIIDRC_BUTTON_PLUS);
	HideButton(backBtn);
	HideButton(startBtn);
	GuiPrompts prompts;
	// The same words as the Play tile: Build and play while the game is still
	// to be made from its patch (rebuildTiles keeps it so).
	const int promptPlay = prompts.Add("+", SelectedGcPatch(state) ? tr("Build and play") : tr("Play"));
	const int promptBack = prompts.Add("B", tr("Back"));
	prompts.Add("A", tr("Select"));
	// Two lines over the prompts: a cIOS remedy or a compile error must
	// stay readable in full. The second line is cut short of the prompts.
	PromptsNote statusTxt(prompts, 40, 392, 568, 2, 15, skin::kInkSoft);
	statusTxt.SetText(ModsNote(state, scanStatus));
	// Dump (a development aid) has no button: the Wii Remote's 2 or a
	// GameCube pad's Z.
	GuiTrigger trigDump;
	trigDump.SetButtonOnlyTrigger(-1, WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_X, PAD_TRIGGER_Z);
	GuiButton dumpBtn(0, 0);
	dumpBtn.SetTrigger(&trigDump);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&banner);
	w.Append(&hero);
	w.Append(&whereOutlined);
	w.Append(&titleOutlined);
	w.Append(&factsOutlined);
	w.Append(&tileGrid);
	w.Append(&statusTxt);
	w.Append(&backBtn.button);
	w.Append(&startBtn.button);
	w.Append(&prompts);
	w.Append(&dumpBtn);
	// Start on the banner screen: this page's Start, pressed for the player,
	// so its checks and warnings still stand (the page stays when one does).
	bool fromBanner = false;
	// Until that Start is handled, the page is built out of sight under the
	// banner, which plays on: it only shows if a check keeps the player on
	// it (the status says why); a launch goes from the banner to its own
	// screen.
	bool bannerHeld = false;
	if (g_startOnOpen) {
		g_startOnOpen = false;
		fromBanner = true;
		bannerHeld = g_lingerBanner != nullptr;
		logf("Game page: Start from the banner screen\n");
		startBtn.button.SetState(STATE::CLICKED);
	}
	// Under a banner still playing, the page stays off the screen until
	// Start is handled. Off, not hidden: GuiWindow::SetVisible(true) would
	// show every element again, the ones the page keeps hidden too.
	if (!bannerHeld) mainWindow->Append(&w);
	if (riftwii::wii::g_gxWatch) logf("Game page: shown, the GUI runs\n");
	ResumeGui();

	const auto say = [&](const std::string& text) { statusTxt.SetText(text); };
	const auto saveOrSay = [&]() {
		std::string error;
		if (SaveChoices(state, error)) return true;
		say(error);
		return false;
	};
	// Start's save: with a banner playing on screen, written with the GUI
	// running, so the banner does not stop (the page is out of sight).
	const auto saveForStart = [&]() {
		if (!bannerHeld) return saveOrSay();
		std::string error;
		ResumeGui();
		const bool saved = SaveChoices(state, error);
		HaltGui();
		if (!saved) say(error);
		return saved;
	};
	// Start's cIOS check reads the slots from the NAND: with a banner
	// playing on screen, with the GUI running (it stopped for 0.2 s).
	const auto ciosCheck = [&]() {
		if (bannerHeld) ResumeGui();
		const riftwii::wii::CiosCheck check = riftwii::wii::image_cios_check(riftwii::wii::SelectedSource(state).game,
			riftwii::wii::SelectedSource(state).cios_slot,
			!state.model.selections().empty() || state.model.save_mode != "nand");
		if (bannerHeld) HaltGui();
		return check;
	};
	const auto rebuildTiles = [&]() {
		BuildGameTiles(state, tiles, tileWhat);
		tileGrid.Refresh();
		factsTxt.SetText(GameFacts(state).c_str());
		prompts.SetAction(promptPlay, SelectedGcPatch(state) ? tr("Build and play") : tr("Play"));
	};
	int shownTile = -1;
	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button, &startBtn.button});
		bool startHandled = false;

		if (coverStart && (coverAsked || (!g_coversOff && !riftwii::wii::NetFailed()))) {
			if (riftwii::wii::CoverFetchGame() == state.game_id || riftwii::wii::StartCoverFetch(state.game_id))
				coverStart = false;
		} else if (coverStart) {
			coverStart = false;
		}
		{
			std::string id, error;
			riftwii::wii::CoverFetch got = riftwii::wii::CoverFetch::NotFound;
			if (riftwii::wii::TakeCoverFetch(id, got, error)) {
				if (got == riftwii::wii::CoverFetch::Stored) riftwii::wii::ForgetCover(id);
				if (id != state.game_id) {
					// One Home started before this page opened.
					if (got == riftwii::wii::CoverFetch::Failed) {
						logf("Covers: stopped: %s\n", error.c_str());
						g_coversOff = true;
					}
				} else {
					if (got == riftwii::wii::CoverFetch::Stored) {
						g_coversOff = false;
					} else if (got == riftwii::wii::CoverFetch::Failed && !coverAsked) {
						logf("Covers: stopped: %s\n", error.c_str());
						g_coversOff = true;
					}
					if (coverAsked) {
						if (got == riftwii::wii::CoverFetch::Stored) say(tr("Cover downloaded."));
						else if (got == riftwii::wii::CoverFetch::NotFound) say(tr("GameTDB has no cover for this game."));
						else say(tr("Could not download the cover: {1}", {FlatCapped(error, 100)}));
						coverAsked = false;
					}
				}
			}
		}
		// The full box, once the cover's download is done.
		if (boxStart && !coverStart &&
		    (riftwii::wii::BoxFetchGame() == state.game_id || riftwii::wii::StartBoxFetch(state.game_id)))
			boxStart = false;
		{
			std::string id, error;
			riftwii::wii::CoverFetch got = riftwii::wii::CoverFetch::NotFound;
			if (riftwii::wii::TakeBoxFetch(id, got, error) && got == riftwii::wii::CoverFetch::Stored)
				riftwii::wii::ForgetBox(id);
		}

		const int lit = tileGrid.Selected();
		if (lit != shownTile && lit >= 0 && static_cast<std::size_t>(lit) < tileWhat.size()) {
			shownTile = lit;
			switch (tileWhat[static_cast<std::size_t>(lit)]) {
				case GameTile::Play:
					say(state.use_channel ? std::string(tr("Starts the game with the cheats and settings shown here.")) : PlayTileNote(state));
					break;
				case GameTile::Mods: say(ModsNote(state, scanStatus)); break;
				case GameTile::Cheats: say(tr("Cheat codes for this game. Press A to choose them.")); break;
				case GameTile::Saves: say(SelectedGcGame(state) ? GcSaveNote(state) : SaveNote(state.model, state.game_id)); break;
				case GameTile::Favorite: say(tr("Favourites have their own view on Home: press 1 there until it shows.")); break;
				case GameTile::Options:
					say(SelectedGcGame(state) ? std::string(tr("This game's own options: how its disc is read, and its controls."))
								  : std::string(tr("This game's own picture, language, online server and the rest. The ones not changed follow Settings.")));
					break;
				case GameTile::About:
					say(tr("What GameTDB says about the game: when it came out, how many can play, the controllers it takes and its story."));
					break;
			}
		}

		const int acted = tileGrid.GetClicked();
		if (acted >= 0 && static_cast<std::size_t>(acted) < tileWhat.size()) {
			const GameTile tile = tileWhat[static_cast<std::size_t>(acted)];
			if (tile == GameTile::Play) {
				startBtn.button.SetState(STATE::CLICKED);
			} else if (tile == GameTile::Saves && SelectedGcGame(state)) {
				transition::Hold(transition::Kind::SlideForward);
				mainWindow->Remove(&w);
				MenuGcCard(state);
				transition::Hold(transition::Kind::SlideBack);
				mainWindow->Append(&w);
				rebuildTiles();
				shownTile = -1;
			} else if (tile == GameTile::Saves && !state.model.pack_save_owner().empty()) {
				say(SaveNote(state.model, state.game_id));
			} else if (tile == GameTile::Saves) {
				static const char* const modes[] = {"nand", "separate", "fresh", "emunand"};
				const int count = static_cast<int>(sizeof(modes) / sizeof(modes[0]));
				int at = 0;
				while (at < count && state.model.save_mode != modes[at]) ++at;
				state.model.save_mode = modes[(at + 1) % count];
				saveOrSay();
				rebuildTiles();
				say(SaveNote(state.model, state.game_id, true));
			} else if (tile == GameTile::Favorite && !state.game_id.empty()) {
				std::set<std::string>& favorites = riftwii::wii::Settings().favorites;
				const std::string key = riftwii::wii::TileKey(state);
				if (favorites.count(key) != 0) favorites.erase(key);
				else favorites.insert(key);
				if (!riftwii::wii::SaveSettings()) say(tr("Could not save the settings to the SD card."));
				rebuildTiles();
			} else if (tile == GameTile::About) {
				// Asks first when there is no database yet, so the page
				// slides in only once there is something to show.
				riftwii::GameInfo info;
				if (FetchGameInfo(state, info)) {
					transition::Hold(transition::Kind::SlideForward);
					mainWindow->Remove(&w);
					MenuAbout(state, info);
					transition::Hold(transition::Kind::SlideBack);
					mainWindow->Append(&w);
				}
				rebuildTiles();
				shownTile = -1;
			} else if (tile == GameTile::Mods || tile == GameTile::Cheats || tile == GameTile::Options) {
				transition::Hold(transition::Kind::SlideForward);
				mainWindow->Remove(&w);
				bool askedCover = false;
				if (tile == GameTile::Mods) MenuMods(state, scanStatus);
				else if (tile == GameTile::Cheats) MenuCheats(state);
				else MenuGameSettings(state, askedCover);
				transition::Hold(transition::Kind::SlideBack);
				mainWindow->Append(&w);
				// The Mods page renames a profile's tile, or removes it (the
				// page is the game's own then).
				if (tile == GameTile::Mods) titleTxt.SetText(GameTitle(state).c_str());
				rebuildTiles();
				shownTile = -1;
				if (askedCover && !state.game_id.empty()) {
					// Started at the top of the loop, or as soon as the
					// network's thread is free; the answer is said when it comes.
					say(tr("Downloading the cover..."));
					coverStart = true;
					coverAsked = true;
					shownTile = acted;
				}
			}
		}
		const int prompt = prompts.GetClicked();
		if (prompt == promptBack) backBtn.button.SetState(STATE::CLICKED);
		else if (prompt == promptPlay) startBtn.button.SetState(STATE::CLICKED);

		if (backBtn.Clicked()) {
			if (saveOrSay()) menu = MENU_SOURCE;
			else backBtn.button.ResetState();
		} else if (startBtn.Clicked()) {
			startBtn.button.ResetState();
			startHandled = true;
			if (state.game_id.empty()) {
				say("No game is selected; go back and pick one.");
			} else if (state.use_channel) {
				// The Wii's own game: none of the drives' checks apply.
				if (saveForStart() && state.channel_index < state.channels.size()) {
					const riftwii::wii::NandChannel& channel = state.channels[state.channel_index];
					logf("Wii channels: %s (%s) started from its page\n", channel.game_id.c_str(), ChannelName(channel).c_str());
					riftwii::wii::SetChannelToStart(channel);
					menu = MENU_PLAY_CHANNEL;
				}
			} else if (!TurnOffEmptyPacks(state)) {
				// stays on the page, the pack as it was
			} else if (state.use_usb && !state.usb_catalog.cios_note.empty()) {
				// No cIOS in any candidate slot: refuse while the remedy is
				// still readable on screen.
				say(FlatCapped(state.usb_catalog.cios_note, 150));
			} else if (state.use_sd && !state.sd_catalog.cios_note.empty()) {
				say(FlatCapped(state.sd_catalog.cios_note, 150));
			} else if (riftwii::wii::CiosCheck cios; (state.use_usb || state.use_sd) &&
				   (cios = ciosCheck()).verdict != riftwii::wii::CiosCheck::Verdict::Ok &&
				   (cios.verdict == riftwii::wii::CiosCheck::Verdict::Refused || !g_oldCiosReminded)) {
				// The cIOS the game would start on: refused while the menu is
				// up, or d2x v11's earlier betas reminded once, then started.
				if (cios.verdict == riftwii::wii::CiosCheck::Verdict::Refused) {
					logf("cIOS: refused: %s\n", cios.why.c_str());
					ShowPopup(tr("This cIOS can't start games"), cios.why, tr("OK"));
				} else {
					g_oldCiosReminded = true;
					ShowOldCiosReminder(cios.slot, cios.name);
					startBtn.button.SetState(STATE::CLICKED);
				}
			} else if (const std::string problem = riftwii::wii::ModPlaceProblem(state); !problem.empty()) {
				// Mods where they cannot work: not even tried. In a popup: the
				// remedy takes more than the status line's two rows.
				ShowPopup(tr("This code mod can't start"), problem, tr("OK"));
			} else if (std::string codes_error; !riftwii::wii::CheckCodeBuilds(state, codes_error)) {
				// In a popup: what to do takes more than the status line's two rows.
				ShowPopup(tr("This code mod can't start"), codes_error, tr("OK"));
			} else if (!PrepareGcGameInMenu(state)) {
				// stopped or failed: the popup said why; stays on the page
			} else if (!ReadyGcStart(state)) {
				// Back: stays on the page (a game just made stays made)
			} else if (!LaunchNote(state).empty() && !state.warning_shown) {
				state.warning_shown = true;
				// In a popup: the status line holds two rows, which cut
				// the CTGP warning off before it said what to do.
				if (fromBanner) {
					// Start on the banner screen: Play goes on with it.
					if (ShowPopup(LaunchNoteTitle(state), LaunchNote(state), tr("Play"), tr("Not now")) == 0)
						startBtn.button.SetState(STATE::CLICKED);
				} else {
					ShowPopup(LaunchNoteTitle(state), LaunchNote(state) + " " + tr("Press Start again to play."), tr("OK"));
				}
			} else if (!PickCodeModStart(state)) {
				// B: back on the game page
			} else if (!saveForStart()) {
				// the status shows why
			} else if (!riftwii::needs_launch_pipeline(!state.model.selections().empty(), state.model.save_mode)) {
				menu = MENU_BOOT;  // no resident work: boot the game as it is
			} else if (state.use_usb || state.use_sd || (riftwii::wii::MenuCiosSlot() == 0 && PacksOnUsb(state))) {
				// cIOS reload and F9 happen after the GUI exits; compilation
				// follows the virtual DI probe in RunLaunch. So does a disc's
				// with packs on the USB drive under a non-d2x Menu IOS: they
				// are read through d2x, loaded for the launch.
				menu = MENU_LAUNCH;
			} else {
				// A physical disc: compile now, while problems can still be
				// shown here, then boot the compiled selection.
				say("Preparing the mods...");
				ResumeGui();
				std::string error;
				state.has_compiled = false;
				const bool ok = CompileSelection(state.model.selections(), state.compiled, error);
				HaltGui();
				state.has_compiled = ok;
				if (ok) menu = MENU_LAUNCH;
				else {
					logf("Compile failed: %s\n", error.c_str());
					say(FlatCapped(error, 130) + " " + tr("(Log: sd:/riftwii/session.log)"));
				}
			}
		} else if (dumpBtn.GetState() == STATE::CLICKED) {
			menu = MENU_DUMP;
		}
		if (bannerHeld && startHandled) {
			// Start handled: staying on the page, it comes in now (the
			// launch screen takes the banner's place itself; Play on the
			// "Before you play" popup pressed Start again).
			if (menu != MENU_NONE) {
				bannerHeld = false;
			} else if (startBtn.button.GetState() != STATE::CLICKED) {
				bannerHeld = false;
				DropLingerBanner();
				mainWindow->Append(&w);
			}
		}
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	if (menu != MENU_LAUNCH && menu != MENU_BOOT && menu != MENU_PLAY_CHANNEL) DropLingerBanner();  // the launch screen drops it otherwise
	// Cheats and video settings go with every launch, mods or not, and
	// with a channel's.
	if (menu == MENU_LAUNCH || menu == MENU_BOOT || menu == MENU_PLAY_CHANNEL) {
		// The page is gone: a banner still on screen plays on meanwhile
		// (cheats and settings read, the play recorded, all on the card).
		const bool playing = g_lingerBanner != nullptr;
		if (playing) ResumeGui();
		riftwii::wii::PrepareLaunchExtras(state);
		riftwii::wii::RecordPlay(riftwii::wii::TileKey(state));
		riftwii::wii::RememberLaunchedTile(riftwii::wii::TileKey(state));
		riftwii::wii::TagGame(state.game_id, true);
		if (playing) HaltGui();
	}
	return menu;
}

// ---------------------------------------------------------------------------
// Settings

static const char* const kLanguages[] = {"auto", "en", "es", "ja", "pt", "it", "fr", "ko"};

// Each language by its own name, as players look for it.
static std::string LanguageName(const std::string& lang)
{
	if (lang == "en") return "English";
	if (lang == "es") return "Español";
	if (lang == "ja") return "日本語";
	if (lang == "pt") return "Português";
	if (lang == "it") return "Italiano";
	if (lang == "fr") return "Français";
	// With its English name too: RiftWii's own font has no Hangul.
	if (lang == "ko") return "한국어 (Korean)";
	return tr("Wii: {1}", {LanguageName(riftwii::wii::MenuLanguage())});
}

// ---------------------------------------------------------------------------
// The GameCube adapter check (Settings)

static std::string PadButtons(const gcad_pad& pad)
{
	static const struct { std::uint16_t bit; const char* name; } kButtons[] = {
		{GCAD_PAD_A, "A"}, {GCAD_PAD_B, "B"}, {GCAD_PAD_X, "X"}, {GCAD_PAD_Y, "Y"}, {GCAD_PAD_START, "Start"},
		{GCAD_PAD_Z, "Z"}, {GCAD_PAD_L, "L"}, {GCAD_PAD_R, "R"}, {GCAD_PAD_UP, "Up"}, {GCAD_PAD_DOWN, "Down"},
		{GCAD_PAD_LEFT, "Left"}, {GCAD_PAD_RIGHT, "Right"}};
	std::string s;
	for (const auto& b : kButtons) {
		if (!(pad.buttons & b.bit)) continue;
		if (!s.empty()) s += " ";
		s += b.name;
	}
	char sticks[96];
	std::snprintf(sticks, sizeof(sticks), "stick %d,%d  C %d,%d  L %u  R %u", pad.stick_x, pad.stick_y,
		pad.substick_x, pad.substick_y, pad.trigger_l, pad.trigger_r);
	return (s.empty() ? std::string("-") : s) + "   " + sticks;
}

static std::string AdapterNote(const std::string& mode)
{
	// Experimental: confirmed in the menu on a Wii U, not yet in games.
	if (mode == "off") return tr("Experimental. The adapter is left alone.");
	if (mode == "on" && riftwii::wii::is_wii_u())
		return tr("WARNING: on this Wii U, games from the SD card or a USB drive can freeze at 97% with this on. Use Automatic unless you are testing the adapter.");
	if (mode == "on") return tr("Experimental. Always on, even with no adapter plugged in, so it can be plugged in during a game. It needs IOS 58 or a d2x cIOS.");
	return tr("Experimental. With the adapter plugged in when a game starts, its controllers fill the empty ports in games that take a GameCube controller. Needs IOS 58 or a d2x cIOS.");
}

static std::string AdapterStatus(const riftwii::wii::GcAdapterView& v)
{
	switch (v.link) {
		case GCAD_LINK_POLL: return tr("Adapter: working");
		case GCAD_LINK_SETUP:
		case GCAD_LINK_SETTLE:
		case GCAD_LINK_CTRL:
		case GCAD_LINK_INIT: return tr("Adapter: starting...");
		case GCAD_LINK_BUSY: return tr("Adapter: another program is using it, waiting");
		case GCAD_LINK_FAILED: return tr("Adapter: it did not answer ({1}), trying again", {std::to_string(v.last_error)});
		default: return tr("Adapter: not found. Plug in its black USB plug.");
	}
}

static void GcAdapterTestPage()
{
	GuiText titleTxt(tr("GameCube adapter"), 30, skin::kInk);
	Place(titleTxt, 40, 28);
	TitleBand titleBand;
	Panel panel(skin::panelSettings, 34, 76);
	GuiText statusTxt("", 18, skin::kInk);
	Place(statusTxt, 56, 96);
	GuiText hidTxt("", 15, skin::kInkDim);
	Place(hidTxt, 56, 122);
	GuiText portTxt[GCAD_PORTS] = {GuiText("", 17, skin::kInkSoft), GuiText("", 17, skin::kInkSoft),
		GuiText("", 17, skin::kInkSoft), GuiText("", 17, skin::kInkSoft)};
	for (unsigned p = 0; p < GCAD_PORTS; ++p) Place(portTxt[p], 56, 154 + static_cast<int>(p) * 32);
	GuiText noteTxt(tr("Press buttons on a controller in the adapter to see them here. In a game that supports the GameCube controller, the adapter's controllers fill the ports that have none plugged in."),
		15, skin::kInkDim);
	Place(noteTxt, 56, 288);
	noteTxt.SetWrap(true, 528, 3);
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 326, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	HideButton(backBtn);
	GuiPrompts prompts;
	const int promptBack = prompts.Add("B", tr("Back"));
	// Which IOS reaches the adapter: games run on a d2x cIOS, the menu on
	// IOS 58 (a Wii U's front ports work in the menu only).
	SkinButton checkBtn(skin::pill, skin::pillOver, 4, 198, 380, tr("Check each cIOS"),
		WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y, WIIDRC_BUTTON_X);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&titleTxt);
	w.Append(&panel);
	w.Append(&statusTxt);
	w.Append(&hidTxt);
	for (auto& t : portTxt) w.Append(&t);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
	w.Append(&prompts);
	w.Append(&checkBtn.button);
	mainWindow->Append(&w);
	std::string why;
	// The menu may already run it (its controllers work the menu); then
	// it keeps running after this page.
	const bool was_running = riftwii::wii::GcAdapterRunning();
	const bool started = riftwii::wii::GcAdapterStart(why);
	if (!started) {
		statusTxt.SetText(tr("This IOS has no USB HID (IOS{1}). Choose IOS 58 or a d2x cIOS as the Menu IOS.",
			{std::to_string(IOS_GetVersion())}).c_str());
	}
	ResumeGui();

	bool done = false;
	while (!done)
	{
		usleep(20000);
		HaltGui();
		ClearStaleButtons({&backBtn.button, &checkBtn.button});
		if (checkBtn.Clicked()) {
			checkBtn.button.ResetState();
			if (ShowPopup(tr("Check each cIOS?"),
				    tr("Games from the SD card or a USB drive run on a d2x cIOS, not on the menu's IOS 58. RiftWii restarts, asks each IOS whether it sees the adapter where it is plugged in now, and shows the answer on Home. Then send a problem report."),
				    tr("Check"), tr("Cancel")) == 0) {
				const bool front = riftwii::wii::is_wii_u() &&
					ShowPopup(tr("Which port?"), tr("Which USB port is the adapter plugged into?"), tr("Front"), tr("Back")) == 0;
				riftwii::wii::SaveUsbCheckPort(front ? "front" : "back");
				if (started && !was_running) riftwii::wii::GcAdapterStop();
				logf("Settings: USB check for the adapter (%s port); restarting\n", front ? "front" : "back");
				riftwii::wii::WarmRestart(riftwii::wii::RestartKind::UsbCheck, tr("Checking the USB ports"));
				statusTxt.SetText(tr("RiftWii could not restart. Start it again from the Homebrew Channel."));
			}
		}
		if (started) {
			riftwii::wii::GcAdapterView v;
			riftwii::wii::GcAdapterPoll(v);
			statusTxt.SetText(AdapterStatus(v).c_str());
			char hid[96];
			std::snprintf(hid, sizeof(hid), "USB HID v%u%s on IOS%d, %u USB device(s), %u reports", v.version,
				v.through_ogc ? " (libogc)" : "", IOS_GetVersion(), v.listed, v.reports);
			hidTxt.SetText(hid);
			for (unsigned p = 0; p < GCAD_PORTS; ++p) {
				const std::string port = tr("Port {1}", {std::to_string(p + 1)}) + ":  ";
				const std::string line = v.present[p] ? port + PadButtons(v.pads[p])
					: port + (v.link == GCAD_LINK_POLL ? tr("nothing plugged in") : std::string("-"));
				portTxt[p].SetText(line.c_str());
			}
		}
		if (prompts.GetClicked() == promptBack) backBtn.button.SetState(STATE::CLICKED);
		if (backBtn.Clicked()) done = true;
		ResumeGui();
	}
	HaltGui();
	if (started && !was_running) riftwii::wii::GcAdapterStop();
	mainWindow->Remove(&w);
	ResumeGui();
}

// Settings > Credits and license: RiftWii's license notice, who its
// parts come from and the GNU GPL in full (wii/menu/credits.hpp).
static void CreditsPage()
{
	GuiText titleTxt(tr("Credits and license"), 30, skin::kInk);
	Place(titleTxt, 40, 28);
	TitleBand titleBand;
	GuiText versionTxt("GPL-3.0-or-later", 15, skin::kInkDim);
	versionTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	versionTxt.SetPosition(-40, 40);
	// About 900 lines of 60 characters (64 ran past the list's 548 px with wide letters): moved into
	// the rows, not copied (the MEM1 heap is small).
	std::vector<FlowRow> rows;
	{
		std::vector<std::string> lines = riftwii::wii::CreditsLines(60);
		rows.resize(lines.size());
		for (std::size_t i = 0; i < lines.size(); ++i) rows[i].label = std::move(lines[i]);
	}
	Panel panel(skin::panelSettings, 34, 76);
	GuiFlowList list(46, 82, 548, 6);
	list.SetRows(&rows);
	list.Select(0);
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	HideButton(backBtn);
	GuiPrompts prompts;
	const int promptBack = prompts.Add("B", tr("Back"));

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&titleTxt);
	w.Append(&versionTxt);
	w.Append(&panel);
	w.Append(&list);
	w.Append(&backBtn.button);
	w.Append(&prompts);
	mainWindow->Append(&w);
	ResumeGui();
	// For whoever reads all the way to the end of the license.
	static bool readItAll = false;
	bool done = false;
	while (!done)
	{
		usleep(20000);
		HaltGui();
		ClearStaleButtons({&backBtn.button});
		list.GetClicked();
		list.GetClickedBack();
		if (prompts.GetClicked() == promptBack) backBtn.button.SetState(STATE::CLICKED);
		if (backBtn.Clicked()) done = true;
		if (!done && !readItAll && list.AtEnd()) {
			readItAll = true;
			logf("Credits: read to the end of the license\n");
			ShowPopup(tr("Achievement unlocked: License Enthusiast"),
				tr("Congratulations! You read all 5,644 words of the GNU GPL, version 3. Are you really that bored? Respect to the developers whose work RiftWii builds on: every one of you is credited above. The GPL is there to protect people who share their code, not to be waved around only when it's handy for picking a fight. Your reward: absolutely nothing, as the license says (\"WITHOUT ANY WARRANTY\")."),
				tr("Fair enough"));
		}
		ResumeGui();
	}
	HaltGui();
	mainWindow->Remove(&w);
	ResumeGui();
}

// Settings' sections: the hub's tiles, each a short list of its own.
enum class SettingsSection { Picture, Games, Controllers, HomeScreen, Look, Online, Finding, More, Count };

static std::string SectionName(SettingsSection s)
{
	switch (s) {
		case SettingsSection::Picture: return tr("Picture");
		case SettingsSection::Games: return tr("Games");
		case SettingsSection::Controllers: return tr("Controllers");
		case SettingsSection::HomeScreen: return tr("Home");
		case SettingsSection::Look: return tr("Menu");
		case SettingsSection::Online: return tr("Online");
		case SettingsSection::Finding: return tr("Drives");
		default: return tr("Help");
	}
}

static const skin::Tex* SectionIcon(SettingsSection s)
{
	switch (s) {
		case SettingsSection::Picture: return &skin::iconPicture;
		case SettingsSection::Games: return &skin::iconPlay;
		case SettingsSection::Controllers: return &skin::iconControllers;
		case SettingsSection::HomeScreen: return &skin::iconHome;
		case SettingsSection::Look: return &skin::iconSound;
		case SettingsSection::Online: return &skin::iconOnline;
		case SettingsSection::Finding: return &skin::iconDrives;
		default: return &skin::iconHelp;
	}
}

// One section of Settings (the hub's tile): its rows only. `backToHub`:
// left with Back, for the hub (any other way out goes where it says).
static int MenuSettingsSection(FrontendState& state, SettingsSection section, bool& backToHub)
{
	int menu = MENU_NONE;
	riftwii::LoaderSettings& settings = riftwii::wii::Settings();
	// The theme and font this run of the menu shows (read at the first
	// visit, before anything here changes them): stepping through them
	// only saves; leaving Settings asks for the one restart.
	static const std::string runningTheme = settings.theme;
	static const std::string runningFont = settings.menu_font;

	const std::vector<int> iosChoices = riftwii::wii::MenuIosChoices();
	int iosSlot = riftwii::wii::LoadMenuIos();
	const bool iosChoosable = iosChoices.size() > 1 || iosSlot != 0;

	bool netOn = riftwii::wii::NetworkPacksEnabled();
	// The themes on the card (wii/menu/menutheme.hpp), read once per visit.
	const std::vector<riftwii::wii::ThemeEntry> themes = riftwii::wii::ListMenuThemes();
	const auto themeName = [&](const std::string& folder) -> std::string {
		if (folder == "default") return tr("Default");
		if (folder == riftwii::kTimeThemeFolder) return std::string("Would You Look at the Time");
		for (const riftwii::wii::ThemeEntry& t : themes)
			if (t.folder == folder) return t.name;
		return folder;
	};
	enum RowAction { kLanguage, kWidth, kDeflicker, kBorders, kVideoMode, kAspect, kGameLanguage, kGameCios, kServer, kHomeTiles, kChannelCorners, kPagePeek, kGridSpacing, kHomeSource, kHomeSort, kPlayHistory, kDiscTile, kWidescreen, kScreenSize, kTheme, kClockFormat, kNoClock, kFont, kSounds, kMusic, kLaunchMusic, kReturnTo, kShots, kOnline, kNames, kGcAdapter, kGcRumble, kWiiRumble, kGcTest, kGcBackups, kIos, kNet, kResync, kWads,
		kRescan, kUpdate, kReport, kTests, kWiiChannel, kUsbHelp, kWhatsNew, kTutorial, kCredits, kResetAll, kExit, kNone, kHeading };
	// The RiftWii channel on the Wii Menu (wii/launch/channel.hpp).
	unsigned channelVersion = 0;
	const bool channelThere = riftwii::wii::ChannelInstalled(channelVersion);
	std::string channelWhy;
	const bool channelCan = riftwii::wii::ChannelInstallerPresent(channelWhy);
	std::vector<FlowRow> rows;
	std::vector<RowAction> actions;
	const auto build = [&]() {
		rows.clear();
		actions.clear();
		const auto option = [&](const std::string& label, const std::string& value, bool on, RowAction action,
					FlowRow::Kind kind = FlowRow::Kind::Option) {
			FlowRow row;
			row.kind = kind;
			row.label = label;
			row.value = value;
			row.on = on;
			rows.push_back(row);
			actions.push_back(action);
		};
		// A group's name: a larger row the focus passes over.
		const auto heading = [&](const char* name) {
			FlowRow row;
			row.kind = FlowRow::Kind::Info;
			row.heading = true;
			row.label = name;
			rows.push_back(row);
			// Its own: kNone's note is the Menu IOS row's ("No d2x cIOS was
			// found"), which a heading under the pointer showed.
			actions.push_back(kHeading);
		};
		heading(tr("Games"));
		option(tr("Picture width"), WidthName(settings.video_width), settings.video_width != "game", kWidth);
		option(tr("Deflicker"), DeflickerName(settings.deflicker), settings.deflicker != "game", kDeflicker);
		option(tr("Black borders"), BordersName(settings.borders), settings.borders != "keep", kBorders);
		option(tr("Video mode"), VideoModeName(settings.video_mode), settings.video_mode != "game", kVideoMode);
		option(tr("Aspect ratio"), AspectName(settings.aspect), settings.aspect != "game", kAspect);
		option(tr("Game language"), GameLanguageName(settings.game_language), settings.game_language != "console",
			kGameLanguage);
		option(tr("Game cIOS"), CiosName(settings.game_cios), settings.game_cios != "auto", kGameCios);
		option(tr("Online server"), ServerName(settings.wfc_server), settings.wfc_server != "off", kServer);
		option(tr("Wii Menu button"), settings.return_to == "menu" ? tr("Wii Menu") : tr("Back to RiftWii"),
			settings.return_to != "menu", kReturnTo);
		option(tr("In-game screenshots"), settings.screenshots == "demo" ? std::string("Demo")
			: settings.screenshots == "on" ? tr("On") : tr("Off"), settings.screenshots != "off", kShots,
			FlowRow::Kind::Toggle);
		option(tr("Back up saves"), settings.gc_backups == "off" ? tr("Off") : tr("On"), settings.gc_backups != "off",
			kGcBackups, FlowRow::Kind::Toggle);
		option(tr("GameCube adapter"), settings.gc_adapter == "demo" ? std::string("Demo")
			: settings.gc_adapter == "on" ? tr("On") : settings.gc_adapter == "off" ? tr("Off") : tr("Automatic"),
			settings.gc_adapter != "off", kGcAdapter);
		option(tr("GameCube rumble"), settings.gc_rumble == "off" ? tr("Off") : tr("On"), settings.gc_rumble != "off",
			kGcRumble, FlowRow::Kind::Toggle);
		option(tr("Wii Remote rumble"), settings.wiimote_rumble == "off" ? tr("Off") : tr("On"),
			settings.wiimote_rumble != "off", kWiiRumble, FlowRow::Kind::Toggle);
		FlowRow gcTest;
		gcTest.kind = FlowRow::Kind::Action;
		gcTest.label = tr("Check the GameCube adapter");
		gcTest.value = tr("Test");
		rows.push_back(gcTest);
		actions.push_back(kGcTest);
		heading(tr("Menu"));
		option(tr("Language"), LanguageName(settings.language), settings.language != "auto", kLanguage);
		option(tr("Home tiles"), settings.home_tiles == "names" ? tr("Names") : settings.home_tiles == "shelf" ? tr("Shelf")
			: settings.home_tiles == "carousel" ? tr("Carousel") : settings.home_tiles == "covers3d" ? tr("3D covers")
			: settings.home_tiles == "channels" ? tr("Channels") : tr("Covers"),
			settings.home_tiles != "names", kHomeTiles);
		option(tr("Grid spacing"), settings.grid_spacing == "roomy" ? tr("Roomy") : tr("Normal"),
			settings.grid_spacing == "roomy", kGridSpacing);
		// Only the Channels view has channel corners and neighbouring pages to show.
		if (settings.home_tiles == "channels") {
			option(tr("Channel corners"), ChannelCornersName(settings.channel_corners), settings.channel_corners != "theme",
				kChannelCorners);
			option(tr("Pages at the sides"), settings.page_peek == "off" ? tr("Off") : tr("On"),
				settings.page_peek != "off", kPagePeek, FlowRow::Kind::Toggle);
		}
		option(tr("Games from"), HomeSourceName(settings.home_source), settings.home_source != "all", kHomeSource);
		option(tr("Home order"), HomeSortName(settings.home_sort), settings.home_sort != "az", kHomeSort);
		option(tr("Play history"), settings.play_history == "off" ? tr("Off") : tr("On"), settings.play_history != "off",
			kPlayHistory, FlowRow::Kind::Toggle);
		option(tr("Disc Channel"), settings.home_disc == "off" ? tr("Off") : tr("On"), settings.home_disc != "off",
			kDiscTile, FlowRow::Kind::Toggle);
		option(tr("Widescreen menu"), settings.menu_widescreen == "on" ? std::string("16:9")
			: settings.menu_widescreen == "off" ? std::string("4:3")
			: std::string(tr("Automatic")) + (riftwii::wii::MenuWidescreen() ? " (16:9)" : " (4:3)"),
			settings.menu_widescreen != "off", kWidescreen);
		option(tr("Screen size"), std::to_string(settings.screen_size) + "%", settings.screen_size != 100, kScreenSize);
		option(tr("Theme"), themeName(settings.theme), settings.theme != "default", kTheme);
		option(tr("Clock"), settings.clock == "12" ? tr("12-hour") : settings.clock == "24" ? tr("24-hour") : tr("Automatic"),
			settings.clock != "auto", kClockFormat);
		option(tr("Disable clock on Home screen"), tr("Off"), false, kNoClock, FlowRow::Kind::Toggle);
		option(tr("Menu font"), settings.menu_font == "wii" ? tr("Wii Menu") : "RiftWii", settings.menu_font == "wii", kFont);
		option(tr("Menu sounds"), MenuSoundsName(settings.menu_sounds), settings.menu_sounds != "off", kSounds);
		option(tr("Menu music"), settings.menu_music == "off" ? tr("Off") : tr("On"), settings.menu_music != "off",
			kMusic, FlowRow::Kind::Toggle);
		option(tr("Launch music"), settings.launch_music == "off" ? tr("Fade out") : tr("Keep playing"),
			settings.launch_music != "off", kLaunchMusic);
		// Without menu music there is nothing to keep or fade.
		if (settings.menu_music == "off") rows.back().dim = true;
		FlowRow ios;
		ios.kind = iosChoosable ? FlowRow::Kind::Option : FlowRow::Kind::Info;
		ios.label = iosChoosable ? "Menu IOS" : "Menu IOS: IOS 58 (no d2x cIOS found)";
		if (iosChoosable) {
			ios.value = MenuIosLabel(iosSlot);
			ios.on = iosSlot != 0;
		}
		ios.dim = !iosChoosable;
		rows.push_back(ios);
		actions.push_back(iosChoosable ? kIos : kNone);
		heading(tr("Finding games"));
		FlowRow rescan;
		rescan.kind = FlowRow::Kind::Action;
		rescan.label = "Look for games again";
		rescan.value = "Rescan";
		rows.push_back(rescan);
		actions.push_back(kRescan);
		FlowRow net;
		net.kind = FlowRow::Kind::Toggle;
		net.label = "Find network packs (RiiFS)";
		net.value = netOn ? tr("On") : tr("Off");
		net.on = netOn;
		rows.push_back(net);
		actions.push_back(kNet);
		FlowRow resync;
		resync.kind = FlowRow::Kind::Action;
		resync.label = "Copy network packs again";
		resync.value = "Resync";
		rows.push_back(resync);
		actions.push_back(kResync);
		// WiiWare and Virtual Console games from .wad files, into the
		// emulated NAND RiftWii lists channels from.
		const std::size_t wadCount = riftwii::wii::list_wad_files().size();
		FlowRow wads;
		wads.kind = FlowRow::Kind::Action;
		wads.label = tr("Install WAD files");
		wads.value = wadCount == 0 ? tr("None") : tr("{1} found", {std::to_string(wadCount)});
		wads.dim = wadCount == 0;
		rows.push_back(wads);
		actions.push_back(kWads);
		heading(tr("Online"));
		option(tr("Download names and cheats"), settings.online ? tr("On") : tr("Off"), settings.online, kOnline,
			FlowRow::Kind::Toggle);
		FlowRow names;
		names.kind = FlowRow::Kind::Action;
		names.label = tr("Get the latest game names");
		names.value = tr("Update");
		names.dim = !settings.online;
		rows.push_back(names);
		actions.push_back(kNames);
		FlowRow update;
		update.kind = FlowRow::Kind::Action;
		update.label = tr("Check for a new version");
		update.value = tr("Check");
		update.dim = !settings.online;
		rows.push_back(update);
		actions.push_back(kUpdate);
		heading(tr("More"));
		// Only while settings.txt has test switches (debug_off), which turn
		// parts of RiftWii off and were left in for days by a tester.
		const auto tests = settings.other.find("debug_off");
		if (tests != settings.other.end() && !tests->second.empty()) {
			FlowRow testRow;
			testRow.kind = FlowRow::Kind::Action;
			testRow.label = tr("Test switches are on");
			testRow.value = tr("Clear");
			rows.push_back(testRow);
			actions.push_back(kTests);
		}
		FlowRow report;
		report.kind = FlowRow::Kind::Action;
		report.label = tr("Send a problem report");
		report.value = tr("Send");
		rows.push_back(report);
		actions.push_back(kReport);
		FlowRow whatsNew;
		whatsNew.kind = FlowRow::Kind::Action;
		whatsNew.label = tr("What's new");
		whatsNew.value = tr("Show");
		rows.push_back(whatsNew);
		actions.push_back(kWhatsNew);
		FlowRow usbHelp;
		usbHelp.kind = FlowRow::Kind::Action;
		usbHelp.label = tr("USB drive help");
		usbHelp.value = tr("Show");
		rows.push_back(usbHelp);
		actions.push_back(kUsbHelp);
		FlowRow tutorial;
		tutorial.kind = FlowRow::Kind::Action;
		tutorial.label = tr("Tutorial");
		tutorial.value = tr("Show");
		rows.push_back(tutorial);
		actions.push_back(kTutorial);
		FlowRow wiiChannel;
		wiiChannel.kind = FlowRow::Kind::Action;
		wiiChannel.label = tr("RiftWii channel on the Wii Menu");
		wiiChannel.value = channelThere ? tr("Installed") : tr("Add");
		wiiChannel.dim = !channelCan;
		rows.push_back(wiiChannel);
		actions.push_back(kWiiChannel);
		FlowRow resetAll;
		resetAll.kind = FlowRow::Kind::Action;
		resetAll.label = tr("Reset all settings");
		resetAll.value = tr("Reset");
		rows.push_back(resetAll);
		actions.push_back(kResetAll);
		FlowRow credits;
		credits.kind = FlowRow::Kind::Action;
		credits.label = tr("Credits and license");
		credits.value = tr("View");
		rows.push_back(credits);
		actions.push_back(kCredits);
		FlowRow exitRow;
		exitRow.kind = FlowRow::Kind::Action;
		exitRow.label = "Leave RiftWii";
		exitRow.value = "Exit";
		rows.push_back(exitRow);
		actions.push_back(kExit);

		// This section's rows alone, without the group names (the hub's
		// tile named the section).
		const auto sectionOf = [](RowAction a) {
			switch (a) {
				case kWidth: case kDeflicker: case kBorders: case kVideoMode: case kAspect:
					return SettingsSection::Picture;
				case kGameLanguage: case kGameCios: case kReturnTo: case kShots: case kGcBackups:
					return SettingsSection::Games;
				case kGcAdapter: case kGcRumble: case kWiiRumble: case kGcTest:
					return SettingsSection::Controllers;
				case kHomeTiles: case kChannelCorners: case kPagePeek: case kGridSpacing: case kHomeSource: case kHomeSort:
				case kPlayHistory: case kDiscTile: case kClockFormat:
				case kNoClock:
					return SettingsSection::HomeScreen;
				case kLanguage: case kWidescreen: case kScreenSize: case kTheme: case kFont: case kSounds: case kMusic: case kLaunchMusic:
				case kIos: case kNone:
					return SettingsSection::Look;
				case kOnline: case kNames: case kServer: case kUpdate:
					return SettingsSection::Online;
				case kRescan: case kNet: case kResync: case kWads:
					return SettingsSection::Finding;
				case kHeading:
				case kExit:  // the hub's Leave RiftWii tile
					return SettingsSection::Count;
				default:
					return SettingsSection::More;
			}
		};
		std::vector<FlowRow> kept;
		std::vector<RowAction> keptActions;
		for (std::size_t i = 0; i < rows.size(); ++i) {
			if (sectionOf(actions[i]) != section) continue;
			kept.push_back(rows[i]);
			keptActions.push_back(actions[i]);
		}
		rows.swap(kept);
		actions.swap(keptActions);
	};
	build();

	const std::string sectionTitle = SectionName(section);
	GuiText titleTxt(sectionTitle.c_str(), 30, skin::kInk);
	Place(titleTxt, 78, 28);
	TitleBand titleBand;
	PageIcon pageIcon(SectionIcon(section));
	GuiText versionTxt("RiftWii " RIFTWII_VERSION, 15, skin::kInkDim);
	const auto saveSettings = [&]() {
		if (!riftwii::wii::SaveSettings()) return std::string(tr("Cannot write sd:/riftwii/settings.txt"));
		return std::string();
	};
	// New names: the title list again, and the scanned games renamed.
	bool namesStale = false;
	const auto renameGames = [&]() {
		riftwii::wii::ReloadTitles();
		riftwii::wii::RenameGames(state.usb_catalog);
		riftwii::wii::RenameGames(state.sd_catalog);
	};
	versionTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	versionTxt.SetPosition(-40, 40);
	NoteCard noteCard;
	GuiFlowList list(44, 84, 540, 5);
	list.SetCards(true);
	list.SetRows(&rows);
	list.Select(0);
	GuiText noteTxt("", 15, skin::kInkSoft);
	Place(noteTxt, NoteCard::kTextX, NoteCard::kTextY);
	noteTxt.SetWrap(true, NoteCard::kTextW, NoteCard::kLines);

	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	HideButton(backBtn);
	GuiPrompts prompts;
	const int promptBack = prompts.Add("B", tr("Back"));
	prompts.Add("A", tr("Change"));

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&pageIcon);
	w.Append(&titleTxt);
	w.Append(&versionTxt);
	w.Append(&noteCard);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
	w.Append(&prompts);
	mainWindow->Append(&w);
	ResumeGui();

	// What each row does, shown when it takes the focus (the first row
	// keeps the page's own note until the focus moves).
	const auto help = [&](RowAction action) -> std::string {
		switch (action) {
			case kLanguage: return tr("The menu's language. Wii follows the console's own setting.");
			case kWidth: return tr("How wide the picture is drawn. 720 fills the screen from side to side.");
			case kDeflicker: return tr("A filter that softens the picture to hide flicker. Off gives the sharpest picture.");
			case kBorders: return tr(kBordersNote);
			case kVideoMode: return tr("The TV signal the game sends. PAL 50 Hz needs a TV that takes it, 480p a component cable.");
			case kAspect: return tr("Makes the game use 4:3 or widescreen 16:9 whatever the Wii's TV setting says. Not every game can be changed.");
			case kGameLanguage: return tr("The language the game is told the console uses. Pick one the game has: some games stop without it.");
			case kGameCios: return tr("The d2x cIOS the game runs under. Automatic uses the menu's, else the first of 249, 250 and 251 that works.");
			case kHomeTiles: return tr("Covers: each game's box art from GameTDB (fetched when downloads are on). 3D covers: GameTDB's pictures of the cases, round a ring with no end. Carousel: the boxes fanned out on an arc. Shelf: the boxes on a shelf. Channels: each game's animated icon and banner, as on the Wii Menu. Names: the names only.");
			case kChannelCorners: return tr("How round the corners of the Channels view's tiles are. Theme keeps the theme's own; the others are the same in every theme.");
			case kGridSpacing: return tr("Roomy makes the Names and Channels tiles a little smaller, with wider gaps between them and at the sides, as on the Wii Menu.");
			case kPagePeek: return tr("Channels: the edges of the previous and the next page at the sides of the screen, as on the Wii Menu. Point at one and press A to turn to that page.");
			case kWidescreen: return tr("On a 16:9 TV the menu is drawn narrower, so covers and pictures keep their shape. Automatic follows the Wii's own TV setting.");
			case kScreenSize: return tr("Makes the menu smaller on screen, so nothing is cut off at the TV's edges. Lower it until the whole menu shows.");
			case kTheme: return tr("The menu's colours and pictures. Themes are folders in sd:/riftwii/themes (docs/THEMES.md on GitHub).");
			case kNoClock: return tr("Turns off the clock on Home.");
			case kClockFormat: return tr("How Home's clock writes the time: 12-hour (with AM and PM) or 24-hour. Automatic writes it as the menu's language does.");
			case kFont: return tr("The letters the menu is written in: RiftWii's own, or the Wii Menu's, read from this Wii.");
			case kSounds: return tr("How loud the menu's clicks are. Quiet softens the tick the pointer makes moving onto something.");
			case kReturnTo: return ReturnToNote();
			case kShots: return tr("Experimental. In a game, hold 1 and press HOME (GameCube controller: hold L and R, press Down). Pictures go to sd:/riftwii/screenshots when RiftWii next starts. Some games and mods don't work with it.");
			case kDiscTile: return DiscTileNote();
			case kGcBackups: return tr("Before a GameCube game starts, its memory card is copied to the SD card. The last five copies are kept, for Restore an older save on the game's Saves page.");
			case kPlayHistory: return tr("Counts the games you start from RiftWii, for Recently played, Home order and a game's page. Off: nothing more is counted, and the counts are not shown.");
			case kHomeSource: return HomeSourceNote(settings.home_source);
			case kHomeSort: return tr("The order of the games on Home. Last played and Most played put the games you played from RiftWii first, the rest after them A to Z. Your order is yours: on Home, hold B and press A on a game to move it.");
			case kLaunchMusic: return tr("Keep playing: the menu's music plays on under the launch screen and fades out as the game starts. Fade out: it fades out when you start a game.");
			case kMusic: return (riftwii::wii::MenuMusicFound() || settings.menu_music != "on") ? tr("Music while the menu is open: music.ogg from sd:/riftwii, or the one in RiftWii's own folder.") : tr("Music is on, but there is none to play: put a music.ogg in sd:/riftwii, or pick a theme with music.");
			case kServer: return tr("The online server the game uses in place of Nintendo's, which closed. Custom uses wfc_domain in settings.txt.");
			case kOnline:
				return settings.online ? tr("Game names and cheats are downloaded when the Wii is online.")
					: tr("Nothing is downloaded. Names and cheats already on the card are still used.");
			case kNames: return tr("Downloads the newest game names from GameTDB.");
			case kGcAdapter: return AdapterNote(settings.gc_adapter);
			case kGcRumble: return tr("Off: GameCube controllers don't rumble in games, in the adapter or the Wii's own ports.");
			case kWiiRumble: return tr("Off: Wii Remotes don't rumble in any game. A game's own page also has Rumble, for that game only.");
			case kGcTest: return tr("Shows live what the controllers in the adapter are pressing.");
			case kTutorial: return tr("The short tour of RiftWii's basics that a new SD card starts with.");
			case kUsbHelp: return tr("What a USB drive needs to work with RiftWii, step by step.");
			case kWhatsNew: return tr("This version's main changes. The release notes on GitHub have all of them.");
			case kTests: {
				const auto tests = settings.other.find("debug_off");
				return tr("settings.txt turns parts of RiftWii off for testing (debug_off = {1}). Clear them unless the RiftWii developers asked you to keep them.",
					{tests != settings.other.end() ? tests->second : std::string()});
			}
			case kCredits: return tr("Who RiftWii's parts come from, its license (the GNU GPL, version 3 or later) and where its source is.");
			case kResetAll: return tr("Every setting back to how RiftWii comes. Games' own pages, favourites, play history and mods are kept.");
			case kIos: return MenuIosNote(iosSlot);
			case kNet:
				return netOn ? "Looks for a PC running a RiiFS server when the games are read. Rescan to look now."
					: "Only servers named by <network> in an XML on the card are used.";
			case kResync: return "The next launch copies every file of its network packs again.";
			case kWads: return tr("Installs the .wad files in the wad folder of the SD card into the emulated NAND USB Loader GX keeps its channels in, and lists them on Home. The Wii itself is not changed.");
			case kRescan: return tr("Reads the SD card and the USB drive again.");
			case kUpdate: return tr("This is RiftWii {1}. Looks on GitHub for a newer release.", {RIFTWII_VERSION});
			case kReport: return tr("Something went wrong? Sends what it takes to find out to a paste site, and shows a link to pass on.");
			case kWiiChannel:
				if (!channelCan) return std::string(tr(channelWhy.c_str())) + ".";
				return tr("A Wii Menu channel that starts RiftWii from the SD card. It holds no copy of RiftWii, so updates keep working. Opens the channel installer, to add, update or remove it.");
			case kExit: return tr("Pick where to go: the Homebrew Channel, the Wii Menu, Priiloader, or power off.");
			case kHeading: return "";
			case kNone:  // the Menu IOS row when there is nothing to choose
				return tr("No d2x cIOS was found in slots 248 to 252, so the menu runs under IOS 58. Install d2x to play games from SD or USB.");
			default: return "";
		}
	};
	int shownRow = -1;
	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button});
		if (prompts.GetClicked() == promptBack) backBtn.button.SetState(STATE::CLICKED);
		const int focused = list.Selected();
		if (focused != shownRow && focused >= 0 && static_cast<std::size_t>(focused) < actions.size()) {
			shownRow = focused;
			noteTxt.SetText(help(actions[static_cast<std::size_t>(focused)]).c_str());
		}
		int acted = list.GetClicked();
		int direction = +1;
		if (acted < 0) {
			acted = list.GetClickedBack();
			direction = -1;
		}
		if (acted >= 0 && static_cast<std::size_t>(acted) < actions.size()) {
			const auto rebuild = [&]() {
				build();
				list.Refresh();
				list.Select(acted);
			};
			const auto note = [&](const std::string& text) { noteTxt.SetText(text.c_str()); };
			// Saves the settings; the note is `text`, or why they were not saved.
			const auto saveAndNote = [&](const std::string& text) {
				const std::string error = saveSettings();
				note(error.empty() ? text : error);
			};
			switch (actions[static_cast<std::size_t>(acted)]) {
				case kLanguage: {
					settings.language = StepValue(kLanguages, settings.language, direction);
					const std::string error = saveSettings();
					const std::string lang = riftwii::wii::MenuLanguage();
					const bool drawable = riftwii::wii::MenuLanguageDrawable(lang);
					riftwii::wii::SetMenuLanguage(drawable ? lang : "en");
					namesStale = true;  // fetched once, on the way out
					titleTxt.SetText(SectionName(section).c_str());
					// Korean is drawn only by the Wii Menu's font of a Korean
					// Wii: said in English, which every font has.
					if (error.empty() && !drawable)
						note(riftwii::wii::kKoreanNeedsFont);
					else
						note(error.empty() ? tr("Game names follow the language when they are downloaded.") : error);
					rebuild();
					break;
				}
				case kWidth:
					settings.video_width = StepValue(kWidths, settings.video_width, direction, false);
					saveAndNote(tr("How wide the picture is drawn. 720 fills the screen from side to side."));
					rebuild();
					break;
				case kDeflicker:
					settings.deflicker = StepValue(kDeflickers, settings.deflicker, direction, false);
					saveAndNote(tr("A filter that softens the picture to hide flicker. Off gives the sharpest picture."));
					rebuild();
					break;
				case kBorders:
					settings.borders = StepValue(kBorderModes, settings.borders, direction, false);
					saveAndNote(tr(kBordersNote));
					rebuild();
					break;
				case kVideoMode:
					settings.video_mode = StepValue(kVideoModes, settings.video_mode, direction, false);
					saveAndNote(tr("The TV signal the game sends. PAL 50 Hz needs a TV that takes it, 480p a component cable."));
					rebuild();
					break;
				case kAspect:
					settings.aspect = StepValue(kAspects, settings.aspect, direction, false);
					saveAndNote(tr("Makes the game use 4:3 or widescreen 16:9 whatever the Wii's TV setting says. Not every game can be changed."));
					rebuild();
					break;
				case kGameLanguage:
					settings.game_language = StepValue(kGameLanguages, settings.game_language, direction, false);
					saveAndNote(tr("The language the game is told the console uses. Pick one the game has: some games stop without it."));
					rebuild();
					break;
				case kGameCios:
					settings.game_cios = StepValue(kCiosChoices, settings.game_cios, direction, false);
					saveAndNote(tr("The d2x cIOS the game runs under. Automatic uses the menu's, else the first of 249, 250 and 251 that works."));
					rebuild();
					break;
				case kServer:
					settings.wfc_server = StepValue(kServers, settings.wfc_server, direction, false);
					saveAndNote(tr("The online server the game uses in place of Nintendo's, which closed. Custom uses wfc_domain in settings.txt."));
					rebuild();
					break;
				case kPagePeek:
					settings.page_peek = settings.page_peek == "off" ? "on" : "off";
					saveAndNote(tr("Channels: the edges of the previous and the next page at the sides of the screen, as on the Wii Menu. Point at one and press A to turn to that page."));
					rebuild();
					break;
				case kPlayHistory:
					settings.play_history = settings.play_history == "off" ? "on" : "off";
					saveAndNote(tr("Counts the games you start from RiftWii, for Recently played, Home order and a game's page. Off: nothing more is counted, and the counts are not shown."));
					rebuild();
					break;
				case kHomeSource: {
					static const char* const kSourceChoices[] = {"all", "sd", "usb", "wii", "gamecube"};
					constexpr int kSources = 5;
					int at = 0;
					while (at < kSources && settings.home_source != kSourceChoices[at]) ++at;
					settings.home_source = kSourceChoices[((at % kSources) + kSources + direction) % kSources];
					saveAndNote(HomeSourceNote(settings.home_source));
					rebuild();
					break;
				}
				case kHomeSort: {
					static const char* const kSortChoices[] = {"az", "recent", "most", "custom"};
					int at = 0;
					while (at < 4 && settings.home_sort != kSortChoices[at]) ++at;
					settings.home_sort = kSortChoices[((at % 4) + 4 + direction) % 4];
					saveAndNote(tr("The order of the games on Home. Last played and Most played put the games you played from RiftWii first, the rest after them A to Z. Your order is yours: on Home, hold B and press A on a game to move it."));
					rebuild();
					break;
				}
				case kSounds: {
					static const char* const kSoundChoices[] = {"normal", "quiet", "off"};
					int at = 0;
					while (at < 3 && settings.menu_sounds != kSoundChoices[at]) ++at;
					settings.menu_sounds = kSoundChoices[((at % 3) + 3 + direction) % 3];
					ApplyMenuSounds();
					saveAndNote(tr("How loud the menu's clicks are. Quiet softens the tick the pointer makes moving onto something."));
					rebuild();
					break;
				}
				case kReturnTo:
					if (riftwii::wii::debug_off("returnto")) {
						// The test switch first, which a tester had left in
						// settings.txt for days: taken out of debug_off (the
						// line goes when nothing is left in it).
						auto it = settings.other.find("debug_off");
						std::string kept;
						std::size_t at = 0;
						while (it != settings.other.end() && at < it->second.size()) {
							const std::size_t end = std::min(it->second.find_first_of(", ", at), it->second.size());
							const std::string word = it->second.substr(at, end - at);
							if (!word.empty() && word != "returnto") kept += (kept.empty() ? "" : ", ") + word;
							at = end + 1;
						}
						if (kept.empty()) settings.other.erase(it);
						else it->second = kept;
						logf("Settings: debug_off returnto taken out (%s left)\n", kept.empty() ? "nothing" : kept.c_str());
					} else {
						settings.return_to = settings.return_to == "menu" ? "riftwii" : "menu";
					}
					saveAndNote(ReturnToNote());
					rebuild();
					break;
				case kGcBackups:
					settings.gc_backups = settings.gc_backups == "off" ? "on" : "off";
					saveAndNote(tr("Before a GameCube game starts, its memory card is copied to the SD card. The last five copies are kept, for Restore an older save on the game's Saves page."));
					rebuild();
					break;
				case kShots:
					settings.screenshots = settings.screenshots == "off" ? "on" : "off";
					saveAndNote(tr("Experimental. In a game, hold 1 and press HOME (GameCube controller: hold L and R, press Down). Pictures go to sd:/riftwii/screenshots when RiftWii next starts. Some games and mods don't work with it."));
					rebuild();
					break;
				case kDiscTile:
					settings.home_disc = settings.home_disc == "off" ? "on" : "off";
					saveAndNote(DiscTileNote());
					rebuild();
					break;
				case kMusic:
					settings.menu_music = settings.menu_music == "off" ? "on" : "off";
					if (settings.menu_music == "off") riftwii::wii::MenuMusicStop();
					else riftwii::wii::MenuMusicStart();
					saveAndNote((riftwii::wii::MenuMusicFound() || settings.menu_music != "on") ? tr("Music while the menu is open: music.ogg from sd:/riftwii, or the one in RiftWii's own folder.") : tr("Music is on, but there is none to play: put a music.ogg in sd:/riftwii, or pick a theme with music."));
					rebuild();
					break;
				case kLaunchMusic:
					settings.launch_music = settings.launch_music == "off" ? "on" : "off";
					saveAndNote(tr("Keep playing: the menu's music plays on under the launch screen and fades out as the game starts. Fade out: it fades out when you start a game."));
					rebuild();
					break;
				case kHomeTiles:
					settings.home_tiles = settings.home_tiles == "covers" ? "covers3d" : settings.home_tiles == "covers3d" ? "carousel"
						: settings.home_tiles == "carousel" ? "shelf"
						: settings.home_tiles == "shelf" ? "channels" : settings.home_tiles == "channels" ? "names" : "covers";
					saveAndNote(tr("Covers: each game's box art from GameTDB (fetched when downloads are on). 3D covers: GameTDB's pictures of the cases, round a ring with no end. Carousel: the boxes fanned out on an arc. Shelf: the boxes on a shelf. Channels: each game's animated icon and banner, as on the Wii Menu. Names: the names only."));
					rebuild();
					break;
				case kChannelCorners:
					settings.channel_corners = StepValue(kChannelCornerChoices, settings.channel_corners, direction);
					skin::SetChannelCorners(settings.channel_corners);
					saveAndNote(tr("How round the corners of the Channels view's tiles are. Theme keeps the theme's own; the others are the same in every theme."));
					rebuild();
					break;
				case kGridSpacing:
					settings.grid_spacing = settings.grid_spacing == "roomy" ? "normal" : "roomy";
					saveAndNote(tr("Roomy makes the Names and Channels tiles a little smaller, with wider gaps between them and at the sides, as on the Wii Menu."));
					rebuild();
					break;
				case kWidescreen: {
					static const char* const kWide[] = {"auto", "on", "off"};
					int at = 0;
					while (at < 3 && settings.menu_widescreen != kWide[at]) ++at;
					settings.menu_widescreen = kWide[((at % 3) + 3 + direction) % 3];
					riftwii::wii::ApplyMenuDisplay();
					skin::EnsureBackgrounds();
					saveAndNote(tr("On a 16:9 TV the menu is drawn narrower, so covers and pictures keep their shape. Automatic follows the Wii's own TV setting."));
					rebuild();
					break;
				}
				case kScreenSize:
					// 100% down to 80%, 5 at a time, round and round.
					settings.screen_size += direction < 0 ? -5 : 5;
					if (settings.screen_size > 100) settings.screen_size = 80;
					if (settings.screen_size < 80) settings.screen_size = 100;
					riftwii::wii::ApplyMenuDisplay();
					saveAndNote(tr("Makes the menu smaller on screen, so nothing is cut off at the TV's edges. Lower it until the whole menu shows."));
					rebuild();
					break;
				case kTheme: {
					// The theme in use is always one of the choices, and so is
					// Default: Disable clock puts on the Time theme, built in,
					// with no themes on the card, and there was no way back.
					if (themes.empty() && settings.theme == "default") {
						note(tr("No themes in sd:/riftwii/themes. The zip's themes folder has one to copy there."));
						break;
					}
					std::vector<std::string> folders{"default"};
					for (const riftwii::wii::ThemeEntry& t : themes) folders.push_back(t.folder);
					if (std::find(folders.begin(), folders.end(), settings.theme) == folders.end())
						folders.push_back(settings.theme);
					std::size_t at = 0;
					while (at < folders.size() && folders[at] != settings.theme) ++at;
					if (at == folders.size()) at = 0;
					const int n = static_cast<int>(folders.size());
					settings.theme = folders[static_cast<std::size_t>(((static_cast<int>(at) + direction) % n + n) % n)];
					const std::string error = saveSettings();
					rebuild();
					if (!error.empty()) {
						note(error);
						break;
					}
					note(settings.theme == runningTheme ? tr("This is the theme on screen now.")
						: tr("The menu restarts to show it when you leave Settings."));
					break;
				}
				case kClockFormat: {
					static const char* const kClocks[] = {"auto", "12", "24"};
					int at = 0;
					while (at < 3 && settings.clock != kClocks[at]) ++at;
					settings.clock = kClocks[((at % 3) + 3 + direction) % 3];
					saveAndNote(tr("How Home's clock writes the time: 12-hour (with AM and PM) or 24-hour. Automatic writes it as the menu's language does."));
					rebuild();
					break;
				}
				case kNoClock: {
					// It does not. Clocks, everywhere.
					if (settings.theme == riftwii::kTimeThemeFolder) {
						note("Would you look at the time.");
						break;
					}
					settings.theme = riftwii::kTimeThemeFolder;
					const std::string error = saveSettings();
					rebuild();
					if (!error.empty()) {
						note(error);
						break;
					}
					logf("Settings: someone tried to turn the clock off\n");
					if (ShowPopup("Would you look at the time", tr("Nothing o'clock. The clock stays."),
						    tr("Restart"), tr("Later")) == 0) {
						riftwii::wii::WarmRestart(riftwii::wii::RestartKind::Theme,
							tr("Theme: {1}", {themeName(settings.theme)}));
						note(tr("RiftWii could not restart. Start it again from the Homebrew Channel."));
					} else {
						note(tr("The menu restarts to show it when you leave Settings."));
					}
					break;
				}
				case kFont: {
					settings.menu_font = settings.menu_font == "wii" ? "riftwii" : "wii";
					const std::string error = saveSettings();
					rebuild();
					if (!error.empty()) {
						note(error);
						break;
					}
					note(settings.menu_font == runningFont ? tr("This is the font on screen now.")
						: tr("The menu restarts to show it when you leave Settings."));
					break;
				}
				case kOnline:
					settings.online = !settings.online;
					saveAndNote(settings.online ? tr("Game names and cheats are downloaded when the Wii is online.")
						: tr("Nothing is downloaded. Names and cheats already on the card are still used."));
					rebuild();
					break;
				case kNames: {
					if (!settings.online) {
						note(tr("Downloads are off. Turn on Download names and cheats first."));
						break;
					}
					note(tr("Downloading game names..."));
					ResumeGui();
					std::string error;
					const bool ok = riftwii::wii::UpdateTitles(riftwii::wii::MenuLanguage(), true, error);
					HaltGui();
					if (ok) {
						renameGames();
						note(tr("Game names updated."));
					} else {
						logf("Titles: update failed: %s\n", error.c_str());
						note(tr("Could not download game names: {1}", {FlatCapped(error, 90)}));
					}
					break;
				}
				case kGcRumble:
					settings.gc_rumble = settings.gc_rumble == "off" ? "on" : "off";
					saveAndNote(tr("Off: GameCube controllers don't rumble in games, in the adapter or the Wii's own ports."));
					rebuild();
					break;
				case kWiiRumble:
					settings.wiimote_rumble = settings.wiimote_rumble == "off" ? "on" : "off";
					saveAndNote(tr("Off: Wii Remotes don't rumble in any game. A game's own page also has Rumble, for that game only."));
					rebuild();
					break;
				case kGcAdapter: {
					// Right: Automatic, On, Off, and round again; left the other
					// way (Automatic to Off without passing On).
					static const char* const kAdapterChoices[] = {"auto", "on", "off"};
					int at = 0;
					while (at < 3 && settings.gc_adapter != kAdapterChoices[at]) ++at;
					const std::string next = kAdapterChoices[((at % 3) + 3 + direction) % 3];
					// On a Wii U, On can freeze a game's start (d2x's /dev/usb/hid
					// sometimes never answers and takes the cIOS with it): two
					// windows before it is set.
					if (next == "on" && riftwii::wii::is_wii_u() &&
						(ShowPopup(tr("WARNING: this can freeze your Wii U"), tr("On a Wii U, the GameCube adapter in games can freeze the console at 97% while a game from the SD card or a USB drive starts. It works some times and freezes others, and RiftWii cannot tell beforehand. If it freezes, hold the power button to turn the console off. Automatic is safe: it leaves the adapter out of those games."), tr("Turn it on anyway"), tr("Cancel")) != 0 ||
						 ShowPopup(tr("Are you sure?"), tr("Some game launches WILL freeze and need the power button. Only turn this on to test the adapter, and send a problem report when it freezes. You can set it back to Automatic here at any time."), tr("Yes, turn it on"), tr("Keep it as it is")) != 0)) {
						riftwii::wii::logf("GameCube adapter: On refused at the warnings\n");
						note(tr("Left as it was."));
						break;
					}
					settings.gc_adapter = next;
					saveAndNote(AdapterNote(settings.gc_adapter));
					rebuild();
					break;
				}
				case kGcTest:
					transition::Hold(transition::Kind::SlideForward);
					mainWindow->Remove(&w);
					ResumeGui();
					GcAdapterTestPage();
					HaltGui();
					transition::Hold(transition::Kind::SlideBack);
					mainWindow->Append(&w);
					break;
				case kTutorial:
					ShowTutorial();
					break;
				case kUsbHelp:
					ShowUsbHelp(false);
					break;
				case kWhatsNew:
					ShowWhatsNew();
					break;
				case kResetAll: {
					if (ShowPopup(tr("Reset all settings?"),
						    tr("Every setting goes back to how RiftWii comes, and RiftWii restarts. Games' own pages, favourites, play history and mods are kept."),
						    tr("Reset"), tr("Cancel")) != 0)
						break;
					logf("Settings: all reset to the defaults\n");
					settings = riftwii::LoaderSettings{};
					const std::string error = saveSettings();
					rebuild();
					if (!error.empty()) {
						note(error);
						break;
					}
					riftwii::wii::WarmRestart(riftwii::wii::RestartKind::Theme, tr("Settings reset to the defaults"));
					note(tr("RiftWii could not restart. Start it again from the Homebrew Channel."));
					break;
				}
				case kTests:
					logf("Settings: test switches cleared (debug_off = %s)\n", settings.other["debug_off"].c_str());
					settings.other.erase("debug_off");
					saveAndNote(tr("Test switches cleared: games start with all of RiftWii's fixes again."));
					rebuild();
					break;
				case kCredits:
					transition::Hold(transition::Kind::SlideForward);
					mainWindow->Remove(&w);
					ResumeGui();
					CreditsPage();
					HaltGui();
					transition::Hold(transition::Kind::SlideBack);
					mainWindow->Append(&w);
					break;
				case kIos: {
					// Step to the next installed choice (IOS58, then each d2x slot).
					std::size_t at = 0;
					while (at < iosChoices.size() && iosChoices[at] != iosSlot) ++at;
					const int n = static_cast<int>(iosChoices.size());
					iosSlot = iosChoices[static_cast<std::size_t>((static_cast<int>(at % n) + direction + n) % n)];
					if (riftwii::wii::SaveMenuIos(iosSlot)) {
						logf("Menu IOS set to %s\n", MenuIosLabel(iosSlot).c_str());
						noteTxt.SetText(MenuIosNote(iosSlot).c_str());
					} else {
						noteTxt.SetText("Cannot write sd:/riftwii/menu_ios.txt");
					}
					build();
					list.Refresh();
					list.Select(acted);
					break;
				}
				case kNet:
					netOn = !netOn;
					riftwii::wii::SetNetworkPacksEnabled(netOn);
					noteTxt.SetText(netOn
						? "Looks for a PC running a RiiFS server when the games are read. Rescan to look now."
						: "Only servers named by <network> in an XML on the card are used.");
					build();
					list.Refresh();
					list.Select(acted);
					break;
				case kResync:
					riftwii::wii::ForceNextSync();
					noteTxt.SetText("The next launch copies every file of its network packs again.");
					break;
				case kRescan:
					g_scanned = false;
					menu = MENU_SOURCE;
					break;
				case kWads:
					if (InstallWads()) {
						g_scanned = false;
						menu = MENU_SOURCE;
					}
					break;
				case kUpdate: {
					if (!settings.online) {
						note(tr("Downloads are off. Turn on Download names and cheats first."));
						break;
					}
					note(tr("Asking GitHub..."));
					ResumeGui();
					std::string latest, error;
					bool newer = false;
					const bool ok = riftwii::wii::CheckForUpdate(true, latest, newer, error);
					HaltGui();
					if (!ok) {
						logf("Update check: %s\n", error.c_str());
						note(tr("Could not check: {1}", {FlatCapped(error, 90)}));
					} else if (newer) {
						RunUpdate(latest);
						note(riftwii::wii::UpdateInstalled(latest)
							? tr("RiftWii {1} is installed. Start RiftWii again to use it.", {latest})
							: tr("RiftWii {1} is out: {2}", {latest, riftwii::wii::kReleasesPage}));
					} else if (latest.empty()) {
						note(tr("No release is out yet. This is RiftWii {1}.", {RIFTWII_VERSION}));
					} else {
						note(tr("RiftWii {1} is the newest version.", {RIFTWII_VERSION}));
					}
					break;
				}
				case kReport:
					if (ShowPopup(tr("Send a problem report?"), tr(kReportWhat), tr("Send"), tr("Cancel")) == 0)
						SendReport("sent from Settings");
					break;
				case kWiiChannel: {
					if (!channelCan) {
						note(std::string(tr(channelWhy.c_str())) + ".");
						break;
					}
					if (ShowPopup(tr("Open the channel installer?"),
							tr("RiftWii closes and the channel installer opens. It adds, updates or removes the RiftWii channel, then brings you back here."),
							tr("Open"), tr("Cancel")) == 0) {
						menu = MENU_CHANNEL;
					}
					break;
				}
				case kExit:
					if (ShowHomeMenu() > 0) menu = MENU_EXIT;
					break;
				default:
					break;
			}
		}
		if (menu == MENU_NONE && backBtn.Clicked()) {
			menu = MENU_SOURCE;
			backToHub = true;
		}
		// Leaving with another theme or font than the one on screen: one
		// restart for both (Later leaves them for the next start).
		if (menu == MENU_SOURCE && (settings.theme != runningTheme || settings.menu_font != runningFont)) {
			const bool theme = settings.theme != runningTheme;
			const bool font = settings.menu_font != runningFont;
			const std::string what = theme && font ? tr("{1} and the new font", {themeName(settings.theme)})
				: theme ? themeName(settings.theme)
				: settings.menu_font == "wii" ? tr("the Wii Menu's font") : tr("RiftWii's font");
			if (ShowPopup(tr("Restart the menu?"),
				    tr("RiftWii's menu restarts to show {1}. Your games and settings stay as they are.", {what}),
				    tr("Restart"), tr("Later")) == 0) {
				logf("Settings: theme %s, menu font %s; restarting the menu\n", settings.theme.c_str(),
					settings.menu_font.c_str());
				riftwii::wii::WarmRestart(theme ? riftwii::wii::RestartKind::Theme : riftwii::wii::RestartKind::MenuFont,
					theme ? tr("Theme: {1}", {themeName(settings.theme)})
						: tr("Menu font: {1}", {settings.menu_font == "wii" ? tr("Wii Menu") : std::string("RiftWii")}));
				noteTxt.SetText(tr("RiftWii could not restart. Start it again from the Homebrew Channel."));
				menu = MENU_NONE;
			}
		}
		if (menu != MENU_NONE && menu != MENU_EXIT && namesStale) {
			// The game names in the new language (downloaded if the
			// Wii is online and the list is not on the card yet).
			noteTxt.SetText(tr("Downloading game names..."));
			ResumeGui();
			renameGames();
			HaltGui();
		}
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

// Settings: a tile for each section, with what it is set to now; A opens
// the section's short list. `focus`: the tile lit, kept between visits.
// -1 when the player goes back to Home, kLeaveRiftWii to leave RiftWii.
static constexpr int kLeaveRiftWii = -3;
static int SettingsHub(FrontendState& state, int& focus, bool fromSection)
{
	const riftwii::LoaderSettings& settings = riftwii::wii::Settings();
	std::string theme = settings.theme == "default" ? std::string(tr("Default")) : settings.theme;
	if (settings.theme == riftwii::kTimeThemeFolder) theme = "Would You Look at the Time";
	for (const riftwii::wii::ThemeEntry& t : riftwii::wii::ListMenuThemes())
		if (t.folder == settings.theme) theme = t.name;
	const std::size_t games = state.usb_catalog.games.size() + state.sd_catalog.games.size();
	const std::string tiles = settings.home_tiles == "names" ? tr("Names") : settings.home_tiles == "shelf" ? tr("Shelf")
			: settings.home_tiles == "carousel" ? tr("Carousel") : settings.home_tiles == "covers3d" ? tr("3D covers")
		: settings.home_tiles == "channels" ? tr("Channels") : tr("Covers");

	struct Section {
		SettingsSection id;
		const skin::Tex* icon;
		std::string value;
		const char* about;
	};
	const Section sections[] = {
		{SettingsSection::Picture, &skin::iconPicture, VideoModeName(settings.video_mode),
			"How games are drawn on your TV: picture width, the flicker filter, black borders, the video mode and the aspect ratio."},
		{SettingsSection::Games, &skin::iconPlay, GameLanguageName(settings.game_language),
			"The language games are told the console uses, the cIOS they run under, where the Wii Menu button leads, and in-game screenshots."},
		{SettingsSection::Controllers, &skin::iconControllers, "",
			"The GameCube controller adapter for Wii U, and rumble for GameCube controllers and Wii Remotes."},
		{SettingsSection::HomeScreen, &skin::iconHome, tiles,
			"How Home shows your games: covers, a shelf or channels, from which drive, in what order, play history and the clock."},
		{SettingsSection::Look, &skin::iconSound, theme,
			"The menu's language, theme, letters, size on the TV, sounds and music."},
		{SettingsSection::Online, &skin::iconOnline, settings.online ? tr("Downloads on") : tr("Downloads off"),
			"Downloads of game names, covers and cheats, the online server for games, and RiftWii's updates."},
		{SettingsSection::Finding, &skin::iconDrives, tr("{1} games", {std::to_string(games)}),
			"Reading the SD card and the USB drive again, and mods from a PC on the network (RiiFS)."},
		{SettingsSection::More, &skin::iconHelp, RIFTWII_VERSION,
			"Problem reports, what's new, the tutorial, the RiftWii channel, resetting the settings and the credits."},
		// Not a section: the HOME Menu's ways out, as a tile of its own.
		{SettingsSection::Count, &skin::iconPower, "",
			"Pick where to go: the Homebrew Channel, the Wii Menu, Priiloader, or power off."},
	};
	// Three across, three down, inside a TV's overscan.
	std::vector<TileSpec> tilesSpec;
	constexpr int kLeft = 40, kTop = 88, kW = 178, kH = 78, kGap = 13;  // x 40 to 600, inside a TV's overscan
	for (int i = 0; i < static_cast<int>(sizeof(sections) / sizeof(sections[0])); ++i) {
		TileSpec t;
		t.x = kLeft + (i % 3) * (kW + kGap);
		t.y = kTop + (i / 3) * (kH + kGap);
		t.w = kW;
		t.h = kH;
		t.label = sections[i].id == SettingsSection::Count ? std::string(tr("Exit")) : SectionName(sections[i].id);
		t.value = sections[i].value;
		t.icon = sections[i].icon;
		tilesSpec.push_back(t);
	}

	GuiText titleTxt(tr("Settings"), 30, skin::kInk);
	Place(titleTxt, 40, 28);
	TitleBand titleBand;
	GuiText versionTxt("RiftWii " RIFTWII_VERSION, 15, skin::kInkDim);
	versionTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	versionTxt.SetPosition(-40, 40);
	GuiTiles tileGrid;
	tileGrid.SetTiles(&tilesSpec);
	tileGrid.Select(focus);
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	HideButton(backBtn);
	GuiPrompts prompts;
	const int promptBack = prompts.Add("B", tr("Back"));
	prompts.Add("A", tr("Open"));
	// Three lines; the third is cut short of the prompts.
	PromptsNote noteTxt(prompts, 44, 360, 556, 3, 16, skin::kInkSoft);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&titleTxt);
	w.Append(&versionTxt);
	w.Append(&tileGrid);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
	w.Append(&prompts);
	if (fromSection) transition::Hold(transition::Kind::SlideBack);
	mainWindow->Append(&w);
	ResumeGui();

	int picked = -2;
	int shown = -1;
	while (picked == -2)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button});
		const int lit = tileGrid.Selected();
		if (lit != shown && lit >= 0 && lit < static_cast<int>(tilesSpec.size())) {
			shown = lit;
			noteTxt.SetText(tr(sections[lit].about));
		}
		const int clicked = tileGrid.GetClicked();
		if (clicked >= 0 && sections[clicked].id == SettingsSection::Count) {
			focus = clicked;
			if (ShowHomeMenu() > 0) picked = kLeaveRiftWii;
		} else if (clicked >= 0) {
			focus = clicked;
			picked = clicked;
		}
		if (prompts.GetClicked() == promptBack) backBtn.button.SetState(STATE::CLICKED);
		if (picked == -2 && backBtn.Clicked()) picked = -1;
		ResumeGui();
	}
	HaltGui();
	if (picked >= 0) transition::Hold(transition::Kind::SlideForward);
	mainWindow->Remove(&w);
	return picked;
}

static int MenuSettings(FrontendState& state)
{
	static int focus = 0;
	bool fromSection = false;
	for (;;) {
		const int picked = SettingsHub(state, focus, fromSection);
		if (picked == kLeaveRiftWii) return MENU_EXIT;
		if (picked < 0) return MENU_SOURCE;
		bool backToHub = false;
		const int menu = MenuSettingsSection(state, static_cast<SettingsSection>(picked), backToHub);
		if (!backToHub) return menu;
		fromSection = true;
	}
}

// ---------------------------------------------------------------------------
// No SD card: RiftWii keeps its settings, logs and saves there, so it
// does not run without one (USB mode is not supported yet). A QR code
// leads to SD cards to buy. Try again restarts RiftWii (wii/core/restart.hpp):
// the restart's fresh IOS lets go of a card the last run left held.

static bool g_noSdFromUsb = false;

void SetNoSdCard(bool fromUsb) { g_noSdFromUsb = fromUsb; }

static int MenuNeedsSd()
{
	// Started again after a failed launch, a crash or Try again: a card is
	// most likely in the slot, just not answering.
	const bool restarted = riftwii::wii::CurrentRestartNote().kind != riftwii::wii::RestartKind::None;
	const bool canRetry = !g_noSdFromUsb && riftwii::wii::CanRestart();
	GuiText titleTxt(tr("RiftWii needs an SD card"), 28, skin::kInk);
	Place(titleTxt, 40, 36);
	GuiText subTxt(g_noSdFromUsb ? tr("USB mode is not supported yet")
		: restarted          ? tr("The SD card could not be read")
				     : tr("No SD card was found"),
		18, skin::kAccentInk);
	Place(subTxt, 40, 74);
	Panel card(skin::panelSettings, 34, 120);
	GuiText bodyTxt(g_noSdFromUsb
			? tr("RiftWii was started from a USB drive. It keeps its settings, logs and saves on the SD card, so for now it needs one to run. Copy the sd-card folder from the RiftWii zip to a FAT32 SD card, put the card in the Wii and start RiftWii from it.")
			: restarted
			? tr("RiftWii started again and could not read the SD card this time. Press Try again. If that doesn't help, turn the Wii off, push the card in firmly and start RiftWii again.")
			: tr("RiftWii keeps its settings, logs and saves on the SD card and could not read one. Put a FAT32 SD card in the Wii with the sd-card folder from the RiftWii zip on it, then start RiftWii again."),
		16, skin::kInkSoft);
	Place(bodyTxt, 56, 142);
	bodyTxt.SetWrap(true, 330, 10);
	constexpr int kModule = 5;
	riftwii::QrCode code;
	riftwii::make_qr("https://www.amazon.com/s?k=16gb+sd+card", code);
	const int kQrSide = QrImage::Side(code, kModule);
	QrImage qr(code, 588 - 16 - kQrSide, 138, kModule);
	GuiText scanTxt(tr("Need a card? Scan this."), 14, skin::kInkDim);
	scanTxt.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	scanTxt.SetPosition(588 - 16 - kQrSide / 2 - screenwidth / 2, 138 + kQrSide + 8);
	SkinButton exitBtn(skin::pill, skin::pillOver, 4, canRetry ? kPairLeftX : 198, 412, tr("Exit"),
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B | WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, PAD_BUTTON_B | PAD_BUTTON_START,
		WIIDRC_BUTTON_B | WIIDRC_BUTTON_HOME);
	SkinButton retryBtn(skin::pillPrimary, skin::pillPrimaryOver, 4, kPairRightX, 412, tr("Try again"),
		WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, WIIDRC_BUTTON_A);
	retryBtn.text.SetColor(skin::kAccentInk);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&subTxt);
	w.Append(&card);
	w.Append(&bodyTxt);
	w.Append(&qr);
	w.Append(&scanTxt);
	w.Append(&exitBtn.button);
	if (canRetry) w.Append(&retryBtn.button);
	mainWindow->Append(&w);
	ResumeGui();
	for (;;) {
		usleep(THREAD_SLEEP);
		HaltGui();
		ClearStaleButtons({&exitBtn.button, &retryBtn.button});
		if (canRetry && retryBtn.Clicked()) {
			// Returns only if it cannot restart (checked above).
			riftwii::wii::WarmRestart(riftwii::wii::RestartKind::SdRetry, tr("The SD card was read."), false);
			retryBtn.button.ResetState();
		}
		const bool done = exitBtn.Clicked();
		if (done) break;
		ResumeGui();
	}
	mainWindow->Remove(&w);
	return MENU_EXIT;
}

// ---------------------------------------------------------------------------
// Launch frame: stays on screen while the boot log prints into its card.

static void ShowLaunchFrame(const FrontendState& state, int action)
{
	const std::string title = action == MENU_CHANNEL ? std::string(tr("The RiftWii channel")) : GameTitle(state);
	const char* doing = action == MENU_DUMP ? tr("Dumping files from")
		: action == MENU_CHANNEL ? tr("Opening the installer for") : tr("Starting");
	// The game page's band in the game's colour, as the page it starts from.
	GameBanner banner(skin::HueFor(action == MENU_CHANNEL ? std::string("RIFTWII") : state.game_id),
		action == MENU_CHANNEL ? std::string() : state.game_id, !state.use_usb && !state.use_sd && !state.use_channel);
	GuiText doingTxt(doing, 16, skin::WithAlpha(skin::kWhite, 200));
	Place(doingTxt, 40, 10);
	GuiText titleTxt(title.c_str(), 28, skin::kWhite);
	Place(titleTxt, 40, 30);
	titleTxt.SetWrap(true, 560, 2);
	// As wide as a popup on a widescreen menu: the log printed into it
	// (wii/main.cpp, EnterConsolePhase) gets the same room.
	Panel card(skin::panelSettings, 34, 160, PopupExtra());
	// The launch waits for the start's update check or a cover download
	// first (wii/main.cpp): say so, as nothing else is on the screen yet.
	// Said here, in the frame, not by the console: the job's thread is
	// still running.
	GuiText footTxt(riftwii::wii::NetBackgroundBusy() && !riftwii::wii::CoverFetchGame().empty() ? tr("Waiting for a cover download to finish...")
		: riftwii::wii::NetBackgroundBusy() && !riftwii::wii::BoxFetchGame().empty() ? tr("Waiting for a box art download to finish...")
		: riftwii::wii::UpdatePacksBusy() ? tr("Stopping the theme download...")
		: riftwii::wii::NetBackgroundBusy() ? tr("Waiting for the update check to finish...")
		: action == MENU_CHANNEL ? tr("RiftWii starts again when it is done.")
		: tr("The game takes over the screen when it is ready."), 15, skin::kInkDim);
	Place(footTxt, 0, 444, true);

	HaltGui();
	hidePointers = true;
	skin::SetOnHome(false);
	GuiWindow w(screenwidth, screenheight);
	w.Append(&banner);
	w.Append(&doingTxt);
	w.Append(&titleTxt);
	w.Append(&card);
	w.Append(&footTxt);
	mainWindow->Append(&w);
	DropLingerBanner();  // a banner Start was pressed on gives way to this
	ResumeGui();
	// Faded in all the way first: this frame stays on screen.
	transition::Settle();
	usleep(100000);  // a few frames, so both framebuffers show it
	HaltGui();
	mainWindow->Remove(&w);
}

void RefreshLaunchFrame(const FrontendState& state, int action)
{
	ShowLaunchFrame(state, action);
}

int MainMenu(int menu, FrontendState& state)
{
	int currentMenu = menu;

	const u64 skinStart = gettime();
	skin::Init();
	logf("Startup: menu art drawn in %u ms\n", static_cast<unsigned>(diff_msec(skinStart, gettime())));
	ApplyMenuSounds();
	riftwii::wii::ApplyMenuDisplay();
	const u64 musicStart = gettime();
	riftwii::wii::MenuMusicStart();
	logf("Startup: music started in %u ms\n", static_cast<unsigned>(diff_msec(musicStart, gettime())));
	riftwii::wii::LoadThemeSounds();  // before the first GuiSound
	soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	mainWindow = new GuiWindow(screenwidth, screenheight);
	backdrop = new skin::GuiBackdrop();
	mainWindow->Append(backdrop);
	// No cuts: a screen or popup that comes or goes crossfades, unless
	// it asked for more (riftwii::wii::transition::Begin).
	riftwii::wii::transition::Init();
	GuiWindow::changed = [](GuiWindow* window, GuiElement*) {
		if (window == mainWindow) riftwii::wii::transition::BeginAuto();
	};
	riftwii::wii::transition::Begin(riftwii::wii::transition::Kind::FromBlack);
	riftwii::wii::GuiScriptLoad(riftwii::wii::CurrentRestartNote().kind == riftwii::wii::RestartKind::None
	                                ? "sd:/riftwii/guiscript.txt"
	                                : "sd:/riftwii/guiscript-restart.txt");

	ResumeGui();

	while(currentMenu != MENU_EXIT && currentMenu != MENU_LAUNCH && currentMenu != MENU_BOOT && currentMenu != MENU_DUMP &&
		currentMenu != MENU_CHANNEL && currentMenu != MENU_PLAY_CHANNEL)
	{
		logf("Screen: %s\n", ScreenName(currentMenu));
		skin::SetOnHome(currentMenu == MENU_SOURCE);
		riftwii::wii::mem::WatchHeap((std::string("on the way to the ") + ScreenName(currentMenu) + " screen").c_str());
		static int previousMenu = MENU_NONE;
		if (previousMenu != MENU_NONE) {
			if (currentMenu == MENU_OPTIONS) transition::Begin(transition::Kind::SlideForward);
			else if (previousMenu == MENU_OPTIONS) transition::Begin(transition::Kind::SlideBack);
			else if (previousMenu == MENU_HOME && currentMenu == MENU_SOURCE && g_flightOn) {
				// From the page's cover back onto the shelf, where the game
				// stands lit; its place stays empty until it lands.
				g_flight = ShelfFocusedPose(g_flight.id, g_flight.hue);
				SetShelfFlying(g_flight.id);
				transition::SetActor(FlightBack, FlightLanded);
				transition::Begin(transition::Kind::Fade);
			} else if (previousMenu == MENU_HOME && currentMenu == MENU_SOURCE)
				transition::Begin(transition::Kind::ZoomOut, g_openRect);
		}
		previousMenu = currentMenu;
		switch (currentMenu)
		{
			case MENU_OPTIONS:
				currentMenu = MenuSettings(state);
				break;
			case MENU_HOME:
				g_homeNotice.clear();
				currentMenu = MenuHome(state);
				break;
			case MENU_NEEDS_SD:
				currentMenu = MenuNeedsSd();
				break;
			case MENU_SOURCE:
			default:
				currentMenu = MenuSource(state);
				break;
		}
	}

	logf("Screen: %s\n", ScreenName(currentMenu));
	if (currentMenu == MENU_LAUNCH || currentMenu == MENU_BOOT || currentMenu == MENU_DUMP || currentMenu == MENU_CHANNEL ||
		currentMenu == MENU_PLAY_CHANNEL) {
		// The GUI thread is halted after this; main prints the boot log
		// into the card of the frame left on screen.
		ShowLaunchFrame(state, currentMenu);
		return currentMenu;
	}

	ExitRequested = g_leave;
	ResumeGui();
	while(1) usleep(THREAD_SLEEP);
	return MENU_EXIT;
}
