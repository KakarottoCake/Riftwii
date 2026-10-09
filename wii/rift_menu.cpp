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
#include <atomic>
#include <ctime>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <set>
#include <unordered_map>
#include <sstream>
#include <wiiuse/wpad.h>
#include <ogc/lwp_watchdog.h>

#include "libwiigui/gui.h"
#include "transition.hpp"
#include "gui_flowlist.hpp"
#include "memlimits.hpp"
#include "modpicture.hpp"
#include "vsdimage.hpp"
#include "bannerplay.hpp"
#include "banners.hpp"
#include "bannersound.hpp"
#include "riftwii/titles.hpp"
#include "riftwii/bnr.hpp"
#include "boxart.hpp"
#include "covers.hpp"
#include "riftwii/coverart.hpp"
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
#include "screenshot.hpp"
#include "boot.hpp"
#include "demo.h"
#include "input.h"
#include "riftwii/apply.hpp"
#include "riftwii/patch.hpp"
#include "codebuilds.hpp"
#include "vsdmake.hpp"
#include "gameextras.hpp"
#include "i18n.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "ios_reload.hpp"
#include "menuios.hpp"
#include "online.hpp"
#include "reportsend.hpp"
#include "modplan.hpp"
#include "restart.hpp"
#include "riitagsend.hpp"
#include "channel.hpp"
#include "riftwii/settingsfile.hpp"
#include "netpacks.hpp"
#include "netsock.hpp"
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
		default: return "?";
	}
}

static GuiWindow * mainWindow = nullptr;
static skin::GuiBackdrop * backdrop = nullptr;
static GuiSound * soundOver = nullptr;
static lwp_t guithread = LWP_THREAD_NULL;
static std::atomic<bool> guiHalt{true};
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

static void ResumeGui()
{
	// Whatever held the cover loader (an unmount, a raw read, an IOS
	// reload) was the menu's own work, and it is done.
	riftwii::wii::CoverLoaderRelease();
	guiHalt = false;
	LWP_ResumeThread(guithread);
}

static void HaltGui()
{
	guiHalt = true;
	while(!LWP_ThreadIsSuspended(guithread))
		usleep(THREAD_SLEEP);
}

// The crash screen (wii/crash.cpp) draws over the menu's last frame: the
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

	while(1)
	{
		if(guiHalt)
		{
			LWP_SuspendThread(LWP_GetSelf());
		}
		else
		{
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
			riftwii::wii::transition::FrameStart();
			mainWindow->Draw();
			// The old screen over the new one while it comes in, and this
			// frame kept for the next change (before the pointers).
			riftwii::wii::transition::FrameEnd();

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

			Menu_Render();
			riftwii::wii::GuiScriptAfterFrame(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight());
			riftwii::wii::ScreenshotAfterFrame(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight());

			if (!idle && !swallowInput && !riftwii::wii::transition::Busy())
				for(i = 0; i < 4; i++)
					mainWindow->Update(&userInput[i]);

			if(ExitRequested)
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

void InitGUIThreads()
{
	dimAllowed = CONF_Init() >= 0 && CONF_GetScreenSaverMode() == 1;
	SYS_SetPowerCallback(PowerPressed);
	WPAD_SetPowerButtonCallback(RemotePowerPressed);
	if (LWP_CreateThread(&guithread, UpdateGUI, nullptr, nullptr, 24576, 70) < 0)
		ExitApp();
	HaltGui();
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
		if (userInput[i].wpad->ir.valid) return true;
	return false;
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

static bool MatchesSearch(const std::string& name, const std::string& id)
{
	// Every word typed must be in the name or the game ID, in any order.
	std::istringstream words(g_search);
	std::string word;
	while (words >> word) {
		if (!strcasestr(name.c_str(), word.c_str()) && !strcasestr(id.c_str(), word.c_str())) return false;
	}
	return true;
}

struct HomeEntry {
	enum class Kind { Disc, Usb, Sd } kind;
	std::size_t index;
};

static std::string GameName(const riftwii::wii::ImageGame& g)
{
	if (!g.display.empty()) return g.display;
	if (!g.title.empty()) return g.title;
	return g.id;
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
		entries.push_back({HomeEntry::Kind::Disc, 0});
	}
	struct Row { std::string name; HomeEntry entry; const riftwii::wii::ImageGame* game; };
	std::vector<Row> rows;
	for (std::size_t i = 0; i < state.usb_catalog.games.size(); ++i)
		rows.push_back({GameName(state.usb_catalog.games[i]), {HomeEntry::Kind::Usb, i}, &state.usb_catalog.games[i]});
	for (std::size_t i = 0; i < state.sd_catalog.games.size(); ++i)
		rows.push_back({GameName(state.sd_catalog.games[i]), {HomeEntry::Kind::Sd, i}, &state.sd_catalog.games[i]});
	std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
		// A leading "The" is not sorted on (The Legend of Zelda under L).
		const int by = strcasecmp(a.name.c_str() + riftwii::sort_name_start(a.name),
			b.name.c_str() + riftwii::sort_name_start(b.name));
		return by != 0 ? by < 0 : strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	});
	const bool searching = !g_search.empty();
	if (!searching && g_filter == Filter::Recent) {
		// The games played from RiftWii, the latest first.
		const riftwii::PlayHistory& history = riftwii::wii::History();
		rows.erase(std::remove_if(rows.begin(), rows.end(),
			[&](const Row& r) { return history.find(r.game->id) == nullptr; }), rows.end());
		std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
			return history.find(a.game->id)->last > history.find(b.game->id)->last;
		});
	}
	for (const Row& r : rows) {
		const bool mods = g_packs.has_packs(r.game->id);
		if (searching && !MatchesSearch(r.name, r.game->id)) continue;
		if (!searching && g_filter == Filter::Mods && !mods) continue;
		if (!searching && g_filter == Filter::Favorites && riftwii::wii::Settings().favorites.count(r.game->id) == 0) continue;
		GridItem item;
		item.title = r.name;
		item.id = r.game->id;
		item.badge = r.entry.kind == HomeEntry::Kind::Usb ? "USB" : "SD";
		item.mods = mods;
		item.hue = skin::HueFor(r.game->id);
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
		const float x = 320 - boxW / 2.0f, y = -boxH - 12 + (boxH + 24) * k;
		skin::DrawNine(skin::card9, x - 8, y - 8, boxW + 16, boxH + 16, riftwii::kCardCorner);
		skin::Draw(skin::noticeIcon[isError ? 1 : 0], x + 14, y + boxH / 2.0f - 14);
		text.SetPosition(static_cast<int>(x) + 54, static_cast<int>(y) + 12);
		text.Draw();
	}
private:
	static constexpr int kMaxText = 470;
	GuiText text;
	int lines = 1, textW = 0;
	bool isError = false, on = false;
	u64 start = 0;
};
// Covers: the games looked at this session; off after a network failure.
static std::set<std::string> g_coversChecked;
static bool g_coversOff = false;
// The shelf's boxes (wii/boxart.cpp), asked for as the shelf comes near them.
static std::set<std::string> g_boxesChecked;
static bool g_boxesOff = false;

// The Channels view: each game's own icon from its banner (wii/banners.cpp),
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
	if (!player->Load(bytes, true, error)) {
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
	void Draw() override {
		const u64 start = gettime();
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

static ChannelChoice ShowChannel(int& index, int count, const std::string& firstId, const BannerLoader& load,
	const std::function<std::string(int)>& idOf)
{
	ChannelView view;
	std::string shownId;
	const auto logShown = [&] {
		if (view.frames == 0) return;
		logf("Banner of %s: %u frames; the CPU drew each in %.1f ms on average, %.1f ms at most; %u frame(s) late "
		     "(more than 20 ms after the one before)\n",
		     shownId.c_str(), view.frames, view.drawTotalUs / 1000.0 / view.frames, view.drawMaxUs / 1000.0, view.late);
		view.frames = view.late = 0;
		view.drawTotalUs = view.drawMaxUs = view.lastDraw = 0;
	};
	// Puts game `i`'s banner up (and its sound on). False when it has none.
	const auto open = [&](int i) {
		std::vector<std::uint8_t> bytes;
		if (!load(i, bytes)) return false;
		std::string error;
		riftwii::OpeningBanner parts;
		std::vector<std::uint8_t> sound;
		// Checked before the banner showing is let go: a broken one is
		// skipped and the one up stays.
		if (!riftwii::parse_opening_bnr(bytes.data(), bytes.size(), parts, error, false)) {
			logf("Banner of %s: %s\n", idOf(i).c_str(), error.c_str());
			return false;
		}
		sound.swap(parts.sound);
		parts = riftwii::OpeningBanner();
		if (!view.player.Load(bytes, false, error)) {
			logf("Banner of %s: %s\n", idOf(i).c_str(), error.c_str());
			return false;
		}
		logShown();
		shownId = idOf(i);
		index = i;
		std::vector<std::uint8_t>().swap(bytes);
		riftwii::wii::BannerSoundStart(sound);
		logf("Screen: channel %s\n", shownId.c_str());
		return true;
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
	SkinButton goBtn(skin::pillPrimary, skin::pillPrimaryOver, 4, 326, 384, tr("Start"),
		WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A | WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_A | PAD_BUTTON_START,
		WIIDRC_BUTTON_A | WIIDRC_BUTTON_PLUS);
	goBtn.text.SetColor(skin::kAccentInk);
	// A pill like Start's, where the Wii Menu has its "Wii Menu" one.
	SkinButton pageBtn(skin::pill, skin::pillOver, 4, 70, 384, tr("Settings"),
		WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_X, PAD_TRIGGER_R, WIIDRC_BUTTON_Y);
	// The arrows at the screen's sides, level with the banner's middle.
	f32 safeX, safeW;
	Menu_SafeArea(&safeX, &safeW);
	const int wide = safeX < 0 ? static_cast<int>(-safeX) : 0;
	SkinButton prevBtn(skin::arrowLeft, skin::arrowLeftOver, 2, 10 - wide, 155, nullptr,
		WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT, PAD_BUTTON_LEFT, WIIDRC_BUTTON_LEFT);
	SkinButton nextBtn(skin::arrowRight, skin::arrowRightOver, 2, 586 + wide, 155, nullptr,
		WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT, PAD_BUTTON_RIGHT, WIIDRC_BUTTON_RIGHT);
	GuiWindow w(screenwidth, screenheight);
	w.Append(&view);
	if (count > 1) {
		w.Append(&prevBtn.button);
		w.Append(&nextBtn.button);
	}
	w.Append(&backBtn.button);
	w.Append(&pageBtn.button);
	w.Append(&goBtn.button);
	mainWindow->SetState(STATE::DISABLED);
	mainWindow->Append(&w);
	w.SetState(STATE::DEFAULT);
	ResumeGui();
	int choice = -1;  // 0 Start, 1 back, 2 the game's page
	while (choice < 0) {
		usleep(20000);
		riftwii::wii::BannerSoundUpdate();
		HaltGui();
		ClearStaleButtons({&backBtn.button, &goBtn.button, &pageBtn.button, &prevBtn.button, &nextBtn.button});
		// The arrows first: A pointed at one also fires Start (its
		// trigger is A anywhere), which must not open the game then.
		if (prevBtn.Clicked() || nextBtn.Clicked()) {
			const int dir = nextBtn.Clicked() ? 1 : -1;
			prevBtn.button.ResetState();
			nextBtn.button.ResetState();
			goBtn.button.ResetState();
			// The next game round that has a banner; the banner slides to it
			// (Home's page turn, inside the banner only), the buttons stay.
			riftwii::wii::BannerSoundStop();
			f32 vx, vy, vw, vh;
			Menu_VisibleArea(&vx, &vy, &vw, &vh);
			transition::Begin(dir > 0 ? transition::Kind::PageForward : transition::Kind::PageBack,
				transition::Rect{vx, vy, vw, kChannelBarTop - vy});
			for (int step = 1; step < count; ++step) {
				if (open(((index + dir * step) % count + count) % count)) break;
			}
		} else if (backBtn.Clicked()) choice = 1;
		else if (pageBtn.Clicked()) {
			goBtn.button.ResetState();  // A on Settings fires Start too
			choice = 2;
		} else if (goBtn.Clicked()) {
			// While pointing, A starts the game only on Start itself, as
			// on the Wii Menu: A that misses an arrow does nothing.
			bool on = !AnyPointerLive();
			for (int i = 0; i < 4 && !on; ++i) {
				const WPADData* p = userInput[i].wpad;
				on = p->ir.valid && goBtn.button.IsInside(static_cast<int>(p->ir.x), static_cast<int>(p->ir.y));
			}
			if (on) choice = 0;
			else goBtn.button.ResetState();
		}
		if (choice < 0) ResumeGui();
	}
	riftwii::wii::BannerSoundStop();
	mainWindow->Remove(&w);
	mainWindow->SetState(STATE::DEFAULT);
	logShown();
	return choice == 0 ? ChannelChoice::Start : choice == 2 ? ChannelChoice::Page : ChannelChoice::Back;
}

void SetHomeNotice(const std::string& text) { g_homeNotice = text; }

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
// starts it (wii/modplan.hpp), but it does not work from RiftWii yet (a
// black screen from a disc, a green one from USB, on testers' consoles).
static bool HomebrewAppPackOn(const FrontendState& state)
{
	if (state.game_id.empty()) return false;
	std::string app, dropped;
	return riftwii::wii::homebrew_app_stand_in(state.model.selections(), state.game_id, app, dropped);
}

static const char* const kHomebrewAppWarning =
	"CTGP Revolution (a pack that starts a Homebrew Channel app) does not work from RiftWii yet: it stops on a black or green screen. Start CTGP from the Homebrew Channel instead.";

static std::string LaunchNote(const FrontendState& state)
{
	std::string note = state.launch_warning;
	if (HomebrewAppPackOn(state)) {
		if (!note.empty()) note += " ";
		note += tr(kHomebrewAppWarning);
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

static std::string HomeStatus(const FrontendState& state, const std::vector<GridItem>& items)
{
	if (!g_homeNotice.empty()) return g_homeNotice;
	// The games shown: the disc drive's tile, when it is there, is not one.
	std::size_t games = 0;
	for (const GridItem& item : items) games += item.badge != "DISC";
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
	// The Wii Menu's bar holds the date and nothing more.
	if (!skin::BarBump()) return "";
	const std::string count = games == 1 ? std::string(tr("1 game")) : tr("{1} games", {std::to_string(games)});
	return std::string(tr(FilterLabel(g_filter))) + ": " + count + "\n" + tr("1: view   2: settings   -/+: pages   B: A to Z");
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
	void Draw() override { Menu_FillWholeScreen((GXColor){0, 0, 0, 150}); }
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
	static constexpr int kW = 440, kH = 108, kTop = 150;
	EmptyHomeNote() : text(tr("No games found yet. Put your games (WBFS, ISO or RVZ) in a folder named wbfs or games at the top of the SD card or USB drive, then pick Look for games again in Settings."), 16, skin::kInk) {
		// Without a parent, x is where the centre goes on the screen.
		text.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		text.SetWrap(true, kW - 40, 4);
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
	GuiText text;
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

class PopupBox {
public:
	PopupBox(const std::string& title, const std::string& body, const std::string& ok = "",
		 const std::string& cancel = "")
		: extra(PopupExtra()), panel(skin::panelSettings, 34, 102, extra), titleTxt(title.c_str(), 24, skin::kInk),
		  bodyTxt(body.c_str(), 16, skin::kInkSoft),
		  okBtn(skin::pill, skin::pillOver, 4, cancel.empty() ? 198 : 70, 314, ok.c_str(),
			WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, WIIDRC_BUTTON_A),
		  cancelBtn(skin::pill, skin::pillOver, 4, 326, 314, cancel.c_str(),
			WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B | WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, PAD_BUTTON_B,
			WIIDRC_BUTTON_B | WIIDRC_BUTTON_HOME),
		  hasOk(!ok.empty()), hasCancel(!cancel.empty()), w(0.86f, 0, true)
	{
		Place(titleTxt, 56 - extra, 120);
		Place(bodyTxt, 56 - extra, 160);
		titleTxt.SetMaxWidth(528 + 2 * extra);
		bodyTxt.SetWrap(true, 528 + 2 * extra, 7);
		w.Append(&dim);
		w.Append(&panel);
		w.Append(&titleTxt);
		w.Append(&bodyTxt);
		if (hasOk) w.Append(&okBtn.button);
		if (hasCancel) w.Append(&cancelBtn.button);
		mainWindow->SetState(STATE::DISABLED);
		transition::Begin(transition::Kind::PopOpen);
		mainWindow->Append(&w);
		w.SetState(STATE::DEFAULT);
	}
	~PopupBox()
	{
		transition::Begin(transition::Kind::PopClose);
		mainWindow->Remove(&w);
		mainWindow->SetState(STATE::DEFAULT);
	}
	void SetBody(const std::string& body) { bodyTxt.SetText(body.c_str()); }
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
		bodyTxt.SetWrap(true, width + extra, 7);  // the box's right part stays where it was
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
			ClearStaleButtons({&okBtn.button, &cancelBtn.button});
			if (hasCancel && cancelBtn.Clicked()) choice = 1;
			else if (hasOk && okBtn.Clicked()) choice = 0;
			if (choice < 0) ResumeGui();
		}
		return choice;
	}
private:
	int extra;  // first: the panel and the texts use it
	Dim dim;
	Panel panel;
	GuiText titleTxt;
	GuiText bodyTxt;
	SkinButton okBtn;
	SkinButton cancelBtn;
	bool hasOk, hasCancel;
	ShiftWindow w;
};

static int ShowPopup(const std::string& title, const std::string& body, const std::string& ok,
	const std::string& cancel = "")
{
	PopupBox box(title, body, ok, cancel);
	return box.Wait();
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
			Menu_DrawRectangle(bx, by, 44, 16, line, 0);
			Menu_DrawRectangle(bx + 1, by + 1, 42, 14, line, 0);
			Menu_DrawRectangle(bx + 44, by + 4, 4, 8, line, 1);
			if (!on) continue;
			const int level = WPAD_BatteryLevel(i);
			const int bars = level >= 92 ? 4 : level >= 66 ? 3 : level >= 40 ? 2 : level >= 14 ? 1 : 0;
			for (int k = 0; k < bars; ++k)
				Menu_DrawRectangle(bx + 4 + k * 10, by + 4, 8, 8, bars == 1 ? (GXColor){222, 72, 72, 255} : skin::kAccent, 1);
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
		606 + wide - closeW, ios ? 18 : 22, tr("Close"),
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
		ResumeGui();
		ok = riftwii::wii::InstallUpdate(latest, where, error);
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
	g_homeNotice = tr("RiftWii {1} is installed. Start RiftWii again to use it.", {latest});
}

// Sends a problem report (wii/reportsend.hpp) and shows its link, with a
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
	g_homeNotice = tr("Report sent: {1}", {r.link});
}

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
	g_homeNotice = tr("RiftWii {1} is out. Settings > Check for a new version installs it.", {latest});
	return false;
}

// A drive that is there but cannot be read gets a box; one that is simply
// not inserted does not. Each problem is shown once until it changes.
static void WarnAboutDrives(const std::string& sdError, const std::string& usbError)
{
	static std::string shown;
	std::string text;
	const auto missing = [](const std::string& e) { return e.find("is inserted") != std::string::npos; };
	if (!sdError.empty() && !missing(sdError)) text += tr("SD card: {1}", {FlatCapped(sdError, 260)}) + "\n";
	if (!usbError.empty() && !missing(usbError)) text += tr("USB drive: {1}", {FlatCapped(usbError, 260)}) + "\n";
	if (text.empty() || text == shown) return;
	shown = text;
	ShowPopup(tr("Drive problem"),
		text + tr("Games on that drive are not listed. Check the drive on a computer; details are in sd:/riftwii/session.log."),
		tr("OK"));
}

// What the last game's card log noted (wii/boot.hpp TakeCardLog): into
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
static void ShowTutorialOnce()
{
	static const char* const kMarker = "sd:/riftwii/tutorial_done.txt";
	struct stat st;
	// Without a card nothing could remember it: it would show every start.
	if (stat("sd:/", &st) != 0 || stat(kMarker, &st) == 0) return;
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

// An older channel than this release's installer puts on (the old one
// starts RiftWii without full access to the Wii's hardware, which some
// features need): offered for reinstalling, once per channel version
// (sd:/riftwii/channel_update_offered.txt keeps the version asked about).
// True to open the installer.
static bool OfferChannelUpdateOnce()
{
	static const char* const kMarker = "sd:/riftwii/channel_update_offered.txt";
	if (riftwii::wii::CurrentRestartNote().kind == riftwii::wii::RestartKind::ChannelDone) return false;
	unsigned version = 0;
	std::string why;
	if (!riftwii::wii::ChannelInstalled(version) || version >= riftwii::wii::kChannelVersion) return false;
	unsigned asked = 0;
	if (FILE* f = std::fopen(kMarker, "r")) {
		if (std::fscanf(f, "%u", &asked) != 1) asked = 0;
		std::fclose(f);
	}
	if (asked >= riftwii::wii::kChannelVersion) return false;
	if (!riftwii::wii::ChannelCanInstall(why)) {
		logf("Channel: version %u installed, %u available, cannot offer it: %s\n", version, riftwii::wii::kChannelVersion,
			why.c_str());
		return false;
	}
	const bool update = ShowPopup(tr("Update the RiftWii channel?"),
		tr("The RiftWii channel on your Wii Menu is an older version ({1}). The new one ({2}) starts RiftWii with full access to the Wii's hardware, which some features need. Reinstall it with the channel installer: it opens, then brings you back here. Settings > RiftWii channel on the Wii Menu can open it later too.",
			{std::to_string(version), std::to_string(riftwii::wii::kChannelVersion)}),
		tr("Open installer"), tr("Not now")) == 0;
	mkdir("sd:/riftwii", 0777);
	FILE* f = std::fopen(kMarker, "w");
	if (f) {
		std::fprintf(f, "%u\n", riftwii::wii::kChannelVersion);
		std::fclose(f);
	}
	logf("Channel: version %u installed, update to %u offered, %s\n", version, riftwii::wii::kChannelVersion,
		update ? "accepted" : "declined");
	return update;
}

static void ScanDrives(FrontendState& state, GuiText& status)
{
	std::string error;
	status.SetText("Reading the SD card...");
	ResumeGui();
	const bool sd = scan_sd_games(state.sd_catalog, error);
	const std::string net = riftwii::wii::RefreshNetworkPacks([&](const char* line) { status.SetText(line); });
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
	WarnAboutDrives(sdError, usb ? std::string() : error.empty() ? std::string("scan failed") : error);
	if (sd) ReportCardLog();
	if (!usb) {
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
		g_homeNotice = tr("RiftWii {1} is installed. Start RiftWii again to use it.", {latest});
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
	LcdClock(int centreX, int top, int height) : cx(centreX), top(top), h(height), rest("", height / 2 + 2, skin::kClock) {
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
		if (restW > 0) width += gap + restW;
		float x = cx - width / 2;
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
			rest.SetPosition(static_cast<int>(x + gap), top + h - (h / 2 + 2) - 2);
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
	const time_t now = time(nullptr);
	struct tm local;
	localtime_r(&now, &local);
	static const char* const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	const int h = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
	char buf[32];
	// {1} the 12-hour hour, {2} the minutes, {3} the 24-hour hour.
	snprintf(buf, sizeof(buf), "%02d", local.tm_min);
	clock = tr(local.tm_hour < 12 ? "{1}:{2} AM" : "{1}:{2} PM",
		   {std::to_string(h), buf, std::to_string(local.tm_hour)});
	date = tr("{1} {2}/{3}", {tr(days[local.tm_wday % 7]), std::to_string(local.tm_mon + 1), std::to_string(local.tm_mday)});
}

// Asks for the words to look for on an on-screen keyboard. Returns false
// when cancelled; `text` holds the entry. Called with the GUI halted and
// returns with it halted, as ShowHomeMenu does: the caller rebuilds the
// grid's items next, which the GUI thread must not be drawing.
static bool AskSearch(std::string& text)
{
	GuiSearchKeys keys(text);
	mainWindow->SetState(STATE::DISABLED);
	mainWindow->Append(&keys);
	keys.SetState(STATE::DEFAULT);
	ResumeGui();
	while (keys.Result() == 0) usleep(20000);
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
	grid.SetShelf(riftwii::wii::Settings().home_tiles == "shelf");
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
	if (dip) {
		// Clear of the dip (58% of the bar, which is 856 wide on a widescreen menu).
		const int dipLeft = 320 - static_cast<int>(0.29f * (wide > 0 ? 856 : 640));
		Place(viewTxt, 16 - wide, 354);
		viewTxt.SetWrap(true, dipLeft - 10 - (16 - wide), 2);
		pageTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
		pageTxt.SetPosition(-(16 - wide), 354);
		pageTxt.SetMaxWidth(dipLeft - 10 - (16 - wide));
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
	filterHint.SetPosition(64 - 320 - wide, 354);
	settingsHint.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	settingsHint.SetPosition(576 - 320 + wide, 354);
	searchHint.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	searchHint.SetPosition(-72 + wide, 21);  // ends at x 568; its box at 576, clear of the button at 588
	// Each on a box of its own: the search's sits over the covers.
	HintChip filterChip(filterHint, 64 - wide, 354, true), settingsChip(settingsHint, 576 + wide, 354, true),
		searchChip(searchHint, 568 + wide, 21, false);
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
	GuiText statusTxt("", dip ? 15 : 17, skin::kInkSoft);
	Place(statusTxt, 0, dip ? 438 : 396, true);
	statusTxt.SetWrap(true, 400 + 2 * wide, dip ? 2 : 3);  // between the round buttons (x 106 and 534 on 4:3)
	EmptyHomeNote emptyNote;
	emptyNote.SetVisible(false);

	SkinButton filterBtn(skin::roundBtn, skin::roundBtnOver, 2, 26 - wide, 386, nullptr,
		WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y, WIIDRC_BUTTON_X, &skin::iconDrives);
	SkinButton settingsBtn(skin::roundBtn, skin::roundBtnOver, 2, 538 + wide, 386, nullptr,
		WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_X, PAD_TRIGGER_R, WIIDRC_BUTTON_Y, &skin::iconGear);
	// Search: Z on a GameCube controller, ZL on a Classic Controller (a
	// Wii Remote has no button left; point and press A). Small, in the top
	// right corner above the tiles' last column and the page arrow (they
	// end at x 626, 14 in from the edge), so the gear's name has room.
	SkinButton searchBtn(skin::roundBtn, skin::roundBtnOver, 2, 588 + wide, 12, nullptr,
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
	// News left for Home (a report sent, a new version): a notice as well.
	if (!g_homeNotice.empty()) toast.Show(g_homeNotice, false);

	const auto showView = [&] {
		// A search lists every game whatever the filter, so it is the view.
		// Over the view's own round button on a bar that dips: its name is enough.
		const std::string name = g_search.empty() ? std::string(tr(FilterLabel(g_filter))) : tr("Search \"{1}\"", {g_search});
		viewTxt.SetText((dip ? name : std::string(tr("View")) + ": " + name).c_str());
		// While a search is on, the round button's first press ends it.
		filterHint.SetText(g_search.empty() ? tr("View") : tr("Clear search"));
	};
	showView();
	const auto refresh = [&](bool keepFocus) {
		// Another view, search or order: crossfaded (unless the screen is
		// already changing some other way).
		transition::BeginAuto();
		showView();
		const int focus = keepFocus ? grid.FocusedIndex() : 0;
		BuildHome(state, items, entries);
		grid.SetItems(&items);
		grid.Focus(std::min(focus, std::max(0, static_cast<int>(items.size()) - 1)));
		statusTxt.SetText(HomeStatus(state, items).c_str());
		emptyNote.SetVisible(state.sd_catalog.games.empty() && state.usb_catalog.games.empty());
	};
	if (!g_scanned) {
		ScanDrives(state, statusTxt);
		riftwii::wii::GcAdapterMenuAllowStart();
		ShowTutorialOnce();
		SuggestDotClean();
		if (OfferChannelOnce() || OfferChannelUpdateOnce()) menu = MENU_CHANNEL;
		refresh(true);
		// The first time Home shows, it opens on the last game played.
		const std::vector<std::string> recent = riftwii::wii::History().recent(1);
		if (!g_focusRestored && !recent.empty()) {
			for (std::size_t i = 0; i < items.size(); ++i) {
				if (items[i].id != recent[0]) continue;
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

	int shownPage = -1, shownPages = -1;
	int bannerPageDone = -1;  // Channels: the page whose banners were all looked at
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
				const riftwii::wii::ImageGame& game = e.kind == HomeEntry::Kind::Usb ? state.usb_catalog.games[e.index]
					: state.sd_catalog.games[e.index];
				std::string why;
				if (riftwii::wii::StoreBanner(game, why)) g_bannerAbsent.erase(items[k].id);
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
		// The shelf's boxes once the covers are in: the ones near the focus first.
		if (coverQueue.empty() && grid.Shelf() && !g_boxesOff && !g_coversOff && riftwii::wii::Settings().online &&
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
		if (!arrived.empty()) grid.CoverArrived(arrived);
		if (!boxArrived.empty()) grid.BoxArrived(boxArrived);
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
		if (grid.Page() != shownPage || grid.Pages() != shownPages) {
			shownPage = grid.Page();
			shownPages = grid.Pages();
			// The shelf has no pages: Minus and Plus move it by 12 games.
			const std::string page = shownPages > 1 && !grid.Shelf() ? tr("Page {1} of {2}", {std::to_string(shownPage + 1), std::to_string(shownPages)}) : "";
			pageTxt.SetText(page.c_str());
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

		int clicked = grid.GetClicked();
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
				transition::Begin(transition::Kind::ZoomIn, tile);
				// Another game's banner, from the arrows: read from its image
				// first when it is not on the card yet.
				const BannerLoader loadBanner = [&](int i, std::vector<std::uint8_t>& bytes) {
					if (i < 0 || static_cast<std::size_t>(i) >= entries.size()) return false;
					const HomeEntry& e = entries[static_cast<std::size_t>(i)];
					if (e.kind == HomeEntry::Kind::Disc) return false;
					const std::string& id = items[static_cast<std::size_t>(i)].id;
					if (riftwii::wii::LoadBanner(id, bytes)) return true;
					if (!riftwii::wii::BannerWanted(id)) return false;
					const riftwii::wii::ImageGame& game = e.kind == HomeEntry::Kind::Usb ? state.usb_catalog.games[e.index]
						: state.sd_catalog.games[e.index];
					std::string why;
					g_bannersTried.insert(id);
					if (!riftwii::wii::StoreBanner(game, why)) return false;
					g_bannerAbsent.erase(id);
					return riftwii::wii::LoadBanner(id, bytes);
				};
				int shown = clicked;
				const ChannelChoice choice = ShowChannel(shown, static_cast<int>(entries.size()),
					items[static_cast<std::size_t>(clicked)].id, loadBanner,
					[&](int i) { return items[static_cast<std::size_t>(i)].id; });
				const bool go = choice != ChannelChoice::Back;
				g_startOnOpen = choice == ChannelChoice::Start;
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
					transition::Begin(transition::Kind::ZoomOut, tile);
					ResumeGui();
					continue;
				}
				// Start or Settings: the banner stays up while the game opens.
				transition::Hold(transition::Kind::Fade);
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
			} else {
				ok = SelectSdGame(state, entry.index, error);
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
				g_startOnOpen = false;
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
			menu = MENU_OPTIONS;
		} else if (searchBtn.Clicked()) {
			searchBtn.button.ResetState();
			std::string typed = g_search;
			if (AskSearch(typed)) {
				// Trim, so a lone space clears the search.
				const std::size_t from = typed.find_first_not_of(' ');
				g_search = from == std::string::npos ? "" : typed.substr(from, typed.find_last_not_of(' ') - from + 1);
				logf("Home: search \"%s\"\n", g_search.c_str());
				refresh(false);
			}
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
			riftwii::wii::SaveSettings();
			refresh(false);
		} else if (rescanBtn.GetState() == STATE::CLICKED) {
			rescanBtn.ResetState();
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

// The game's cover in the banner's corner, when it was downloaded.
class CoverArt : public GuiElement {
public:
	CoverArt(std::string id, int x, int y) : id(std::move(id)), x(x), y(y) {}
	void Draw() override {
		const u8* tex = riftwii::wii::CoverTexture(id);
		if (!tex) return;
		skin::Draw(skin::coverTile, x - 7, y - 7);
		skin::DrawRgb5a3(tex, riftwii::kCoverWidth, riftwii::kCoverHeight, x, y);
	}
private:
	std::string id;
	int x, y;
};

// The game's banner: its hue across the top with light stripes.
class GameBanner : public GuiElement {
public:
	static constexpr int kHeight = 120;
	explicit GameBanner(GXColor hue) : hue(hue) {}
	void Draw() override {
		Menu_FillScreen(-1000, 1000 + kHeight, hue);
		Menu_Scissor(-1000, -1000, 3000, 1000 + kHeight);
		// Across the whole screen: a widescreen menu is wider than the
		// stripes' 640 (drawn once, the band's sides were bare). Copies side
		// by side, a whole period apart, so they join.
		f32 safeX, safeW;
		Menu_SafeArea(&safeX, &safeW);
		const int w = skin::bannerStripes.w > 0 ? skin::bannerStripes.w : 640;
		for (int x = 0; x > safeX - w; x -= w) Stripes(x);
		for (int x = w; x < safeX + safeW; x += w) Stripes(x);
		GX_SetScissor(0, 0, Menu_XfbWidth(), Menu_EfbHeight());
		for (int i = 0; i < 4; ++i) Menu_FillScreen(kHeight - 19 + 4 * i, 4, (GXColor){0, 0, 0, static_cast<u8>(10 + 10 * i)});
		Menu_FillScreen(kHeight - 3, 3, skin::kAccent);
		Menu_FillScreen(kHeight, 2, (GXColor){0, 0, 0, 30});
		Menu_FillScreen(kHeight + 2, 3, (GXColor){0, 0, 0, 12});
	}
private:
	// One copy of the stripes at x, cut to the screen and the band first:
	// the copies at the sides of a widescreen menu ran some 530 units past
	// the screen, and a console draws a polygon that runs far past it
	// wrongly at times (7f1ad1d); a tester's 16:9 TV showed a bright bar
	// where two copies meet. Dolphin draws either way the same.
	void Stripes(int x) {
		const skin::Tex& t = skin::bannerStripes;
		if (!t.data) return;
		f32 vx, vy, vw, vh;
		Menu_VisibleArea(&vx, &vy, &vw, &vh);
		const float top = -8, x0 = std::max<float>(x, vx), x1 = std::min<float>(x + t.w, vx + vw);
		const float y0 = std::max(top, vy), y1 = std::min<float>(top + t.h, kHeight);
		if (x1 <= x0 || y1 <= y0) return;
		Menu_DrawImgPart(x0, y0, x1 - x0, y1 - y0, static_cast<u16>(t.w), static_cast<u16>(t.h), t.data,
			(x0 - x) / t.w, (y0 - top) / t.h, (x1 - x) / t.w, (y1 - top) / t.h, 255);
	}
private:
	GXColor hue;
};

// A white card at (x, y) drawn from a panel texture.
static std::string SaveLabel(const std::string& mode)
{
	if (mode == "separate") return "SD, from Wii save";
	if (mode == "fresh") return "SD, fresh start";
	return "On the Wii";
}
static std::string SaveNote(const riftwii::LaunchModel& model)
{
	const std::string owner = model.pack_save_owner();
	if (!owner.empty())
		return "This pack keeps its own saves. Turn it off to choose here.";
	const std::string& mode = model.save_mode;
	if (mode == "separate") return "Saves go to the SD card, starting from the Wii's save.";
	if (mode == "fresh") return "Saves go to the SD card, starting fresh.";
	return "Saves stay on the Wii, as usual.";
}

struct RowRef {
	enum class What { Mods, Saves, Cheats, Width, Deflicker, Borders, VideoMode, RegionVideo, Aspect, Rumble, Speaker,
		RegionStrings, Language, Cios, Server, Favorite, Pack, Option, Note,
		AddCodes, ForgetCodes, MakeImage, Cover } what = What::Note;
	std::size_t pkg = 0, opt = 0;
};

// The game's video settings: "global" follows Settings, the rest are
// riftwii/videopatch.hpp's names.
static const char* const kWidths[] = {"global", "game", "framebuffer", "704", "720"};
static const char* const kDeflickers[] = {"global", "game", "off", "low", "medium", "high"};
static const char* const kBorderModes[] = {"global", "keep", "remove", "remove_all"};
static const char* const kVideoModes[] = {"global", "game", "system", "ntsc", "pal60", "pal50", "480p"};
static const char* const kAspects[] = {"game", "4:3", "16:9"};
static const char* const kGameLanguages[] = {"global", "console", "ja", "en", "de", "fr", "es", "it", "nl", "zh-hans",
	"zh-hant", "ko"};
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

static void BuildGameRows(const FrontendState& state, std::vector<FlowRow>& rows, std::vector<RowRef>& refs)
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
	aspect.value = game.aspect == "4:3" || game.aspect == "16:9" ? game.aspect : tr("Game's own");
	aspect.on = game.aspect != "game";
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
	server.value = GameValue(game.server, global.wfc_server, ServerName);
	server.on = game.server != "global";
	add(server, {RowRef::What::Server});
	FlowRow favorite;
	favorite.kind = FlowRow::Kind::Toggle;
	favorite.label = tr("Favourite");
	favorite.on = global.favorites.count(state.game_id) != 0;
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
	show({W::Mods, W::Saves, W::Cheats, W::Favorite, W::Cover});
	heading(tr("Picture"));
	show({W::Width, W::Deflicker, W::Borders, W::VideoMode, W::RegionVideo, W::Aspect});
	heading(tr("Other"));
	show({W::Language, W::RegionStrings, W::Cios, W::Server, W::Rumble, W::Speaker});
}

// "1.2 GB", "640 MB".
static std::string SizeText(std::uint64_t bytes)
{
	const std::uint64_t mb = (bytes + (1 << 20) - 1) >> 20;
	if (mb < 1024) return std::to_string(mb) + " MB";
	const std::uint64_t tenths = mb * 10 / 1024;
	return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " GB";
}

// Makes an image of the code build `p` on the SD card (wii/vsdmake.hpp),
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
		ProgressBar bar(56, 280, 528);
		box.Add(&bar, 528);
		const u64 start = gettime();
		std::string shown;
		ResumeGui();
		ok = riftwii::wii::MakeVsdImage(plan, [&](std::uint64_t written, const std::string& path) {
			if (!path.empty()) shown = path;
			const u64 ms = diff_msec(start, gettime());
			std::string text = SizeText(written) + " / " + SizeText(plan.plan.image_bytes);
			// What is left at the rate so far, once there is a rate.
			if (ms > 10000 && written > (16u << 20)) {
				const std::uint64_t left = (plan.plan.image_bytes - std::min(written, plan.plan.image_bytes)) * ms / written / 1000;
				text += "  -  " + std::string(left >= 90 ? tr("{1} minutes left", {std::to_string((left + 59) / 60)})
						: tr("{1} seconds left", {std::to_string(left)}));
			}
			text += "\n" + FlatCapped(shown.empty() ? std::string(tr("Free space")) : shown, 60);
			HaltGui();
			bar.Set(static_cast<double>(written) / static_cast<double>(plan.plan.image_bytes));
			box.SetBody(text);
			const bool stop = box.Pressed();
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
		head.label = PackName(p.file);
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
	pick.label = "Add a code build...";
	add(pick, {RowRef::What::AddCodes});
}

static std::string SourceWhere(const FrontendState& state)
{
	if (state.use_usb) return "USB drive";
	if (state.use_sd) return "SD card";
	return "Disc drive";
}

static std::string GameTitle(const FrontendState& state)
{
	if (state.use_usb && state.usb_index < state.usb_catalog.games.size())
		return GameName(state.usb_catalog.games[state.usb_index]);
	if (state.use_sd && state.sd_index < state.sd_catalog.games.size())
		return GameName(state.sd_catalog.games[state.sd_index]);
	return riftwii::wii::GameDisplayName(state.game_id, state.disc_title.empty() ? state.game_id : state.disc_title);
}

// ---------------------------------------------------------------------------
// Cheats: the game's cheat file as a list to tick, opened from the game
// page. The file is plain text (riftwii/cheats.hpp); when the Wii is
// online a missing one is fetched from the GeckoCodes archive.

static void MenuCheats(FrontendState& state)
{
	riftwii::GameSettings& game = state.model.game;
	riftwii::CheatFile file;
	std::string status;
	bool loaded = false;
	const std::string fileNote = tr("The cheats are in {1}. Edit it on a computer to add your own.",
		{CheatFileShown(state.game_id)});

	enum class Act { Use, Download, Cheat, None };
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
				row.value = tr("Edit first");
				row.dim = true;
			} else {
				row.on = game.cheat_names.count(c.name) != 0;
				row.value = row.on ? tr("On") : tr("Off");
			}
			rows.push_back(row);
			refs.push_back({Act::Cheat, i});
		}
	};
	// Picks of cheats the file no longer has are dropped, so the game page
	// counts only what will be applied.
	const auto load = [&](bool download) {
		loaded = riftwii::wii::LoadGameCheats(state.game_id, download, file, status);
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
	Place(titleTxt, 40, 28);
	TitleBand titleBand;
	const std::string gameName = FlatCapped(GameTitle(state), 40);
	GuiText gameTxt(gameName.c_str(), 16, skin::kInkDim);
	gameTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	gameTxt.SetPosition(-40, 40);
	Panel panel(skin::panelGame, 34, 76);
	GuiFlowList list(46, 82, 548, 5);
	list.SetRows(&rows);
	list.Select(0);
	GuiText noteTxt(fileNote.c_str(), 16, skin::kInkSoft);
	Place(noteTxt, 52, 312);
	noteTxt.SetWrap(true, 536, 4);
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&titleTxt);
	w.Append(&gameTxt);
	w.Append(&panel);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
	mainWindow->Append(&w);

	const auto say = [&](const std::string& text) { noteTxt.SetText(text.c_str()); };
	// Fetching blocks for a few seconds: the message is drawn first.
	const auto fetch = [&]() {
		std::string text = tr("Downloading cheats...");
		say(text);
		ResumeGui();
		std::string error;
		const bool ok = riftwii::wii::DownloadCheats(state.game_id, error);
		HaltGui();
		if (ok) {
			load(false);
			say(loaded ? tr("{1} cheats. Turn on the ones you want.", {std::to_string(file.cheats.size())}) : status);
		} else {
			logf("Cheats: download failed: %s\n", error.c_str());
			say(tr("Could not download cheats: {1}", {FlatCapped(error, 90)}));
		}
		build();
		list.Refresh();
	};
	// A game opened for the first time gets its cheats straight away.
	if (!loaded && riftwii::wii::Settings().online && !state.game_id.empty()) {
		std::string error;
		if (riftwii::wii::DownloadCheats(state.game_id, error)) {
			load(false);
			build();
			list.Refresh();
		} else {
			status = tr("No cheats found online for this game.");
			build();
			list.Refresh();
			logf("Cheats: none downloaded for %s: %s\n", state.game_id.c_str(), error.c_str());
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
				if (c.needs_values) note = tr("This cheat has values to fill in (the X's). Edit the file first.") + (note.empty() ? "" : " " + note);
				say(FlatCapped(note.empty() ? c.name : c.name + ": " + note, 200));
			} else if (ref.act == Act::Use) {
				say(tr("Cheats are only applied when this is On."));
			} else if (ref.act == Act::Download) {
				say(riftwii::wii::Settings().online ? tr("Replaces the file with the latest cheats from the GeckoCodes archive.")
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
			} else if (ref.act == Act::Cheat) {
				const riftwii::Cheat& c = file.cheats[ref.cheat];
				if (c.needs_values) {
					say(tr("This cheat has values to fill in (the X's). Edit the file first."));
				} else {
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
		if (riftwii::show_package(p) && p.valid && p.enabled) names += (names.empty() ? "" : ", ") + PackName(p.file);
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

	GuiText titleTxt("Add a code build", 30, skin::kInk);
	Place(titleTxt, 40, 28);
	TitleBand titleBand;
	std::string whereText = folder + "/";
	GuiText whereTxt(whereText.c_str(), 16, skin::kInkDim);
	whereTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	whereTxt.SetPosition(-40, 40);
	Panel panel(skin::panelGame, 34, 76);
	GuiFlowList list(46, 82, 548, 5);
	list.SetRows(&rows);
	// In a folder, the first entry (the game's code file when it is there),
	// not the way up.
	const auto firstRow = [&]() { return folder != "sd:" && rows.size() > 1 ? 1 : 0; };
	list.Select(firstRow());
	GuiText noteTxt("", 16, skin::kInkSoft);
	Place(noteTxt, 52, 312);
	noteTxt.SetWrap(true, 536, 4);
	const std::string help = "Open the build's folder and pick its code file (" + state.game_id +
		".gct). Its gameconfig.txt is read from the same folder, a folder above it or the top of the card.";
	noteTxt.SetText(help.c_str());
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&titleTxt);
	w.Append(&whereTxt);
	w.Append(&panel);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
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
// PNG next to the mod (wii/modpicture.hpp), else the game's own cover,
// else the disc; with no picture of its own, the file name to add. Fades
// in and out like the hover names.
class ModPicturePopup : public GuiElement {
public:
	static constexpr int kInset = 4;
	static constexpr int kCaption = 38;
	static constexpr int kW = riftwii::wii::kModPictureW + 2 * kInset;
	static constexpr int kH = riftwii::wii::kModPictureH + 2 * kInset + kCaption;

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
		tex = picture;
		hinted = !picture && !hint.empty();
		name.SetText(hint.c_str());
		for (int size = 14; size >= 10; --size) {
			name.SetFontSize(size);
			if (name.GetTextWidth() <= kW - 8) break;
		}
		name.SetMaxWidth(kW - 8);
		const int h = hinted ? kH : kH - kCaption;
		x = 590 - kW;
		y = rowTop + GuiFlowList::kRowHeight + 6;
		if (y + h > 472) y = rowTop - 6 - h;
		if (y < 8) y = 8;
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
			Menu_DrawImg(px, py, riftwii::wii::kModPictureW, riftwii::wii::kModPictureH, const_cast<u8*>(tex), 0, 1, 1,
				static_cast<u8>(alpha));
		} else if (const u8* cover = riftwii::wii::CoverTexture(gameId)) {
			// 80x112 at 1.5 fills the 120x168 box; it scales about its centre.
			skin::DrawRgb5a3(cover, riftwii::kCoverWidth, riftwii::kCoverHeight,
				px + (riftwii::wii::kModPictureW - riftwii::kCoverWidth) / 2.0f,
				py + (riftwii::wii::kModPictureH - riftwii::kCoverHeight) / 2.0f, alpha, 1.5f);
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
	Place(titleTxt, 40, 28);
	TitleBand titleBand;
	const std::string gameName = FlatCapped(GameTitle(state), 40);
	GuiText gameTxt(gameName.c_str(), 16, skin::kInkDim);
	gameTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	gameTxt.SetPosition(-40, 40);
	Panel panel(skin::panelGame, 34, 76);
	GuiFlowList list(46, 82, 548, 5);
	list.SetRows(&rows);
	list.Select(0);
	GuiText noteTxt("", 16, skin::kInkSoft);
	Place(noteTxt, 52, 312);
	noteTxt.SetWrap(true, 536, 4);
	int noteWidth = 536;  // narrower while the picture is beside it
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	ModPicturePopup picture(state.game_id);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&titleTxt);
	w.Append(&gameTxt);
	w.Append(&panel);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
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
			if (row != pictureRow) {
				pictureRow = row;
				pictureRest = 0;
				picture.Hide();
			} else if (modRow && ++pictureRest == 20) {
				const riftwii::LaunchPackage& p = state.model.packages[refs[static_cast<std::size_t>(row)].pkg];
				picture.Show(riftwii::wii::ModPicture(p), riftwii::wii::ModPictureName(p), list.RowTop(row));
			}
			picture.Step();
			// The note's four lines (y 312 down) wrap short of the picture.
			const int width = picture.Covers(312, 312 + 4 * 22) ? picture.Left() - 8 - 52 : 536;
			if (width != noteWidth) {
				noteWidth = width;
				noteTxt.SetWrap(true, width, 4);
			}
		}
		if (row != shownRow && row >= 0 && static_cast<std::size_t>(row) < refs.size()) {
			shownRow = row;
			const RowRef& ref = refs[static_cast<std::size_t>(row)];
			if (ref.what == RowRef::What::Pack) say(PackSummary(state.model.packages[ref.pkg]));
			else if (ref.what == RowRef::What::Option) {
				const riftwii::Option& o = state.model.packages[ref.pkg].package.options[ref.opt];
				say(o.section.empty() ? PackName(state.model.packages[ref.pkg].file) : o.section);
			} else if (ref.what == RowRef::What::AddCodes) {
				say("For mods made of Gecko codes, like Project+ builds: pick the build's code file on the SD card.");
			} else if (ref.what == RowRef::What::ForgetCodes) {
				say("Takes this build off the list. Its files stay on the SD card.");
			} else if (ref.what == RowRef::What::MakeImage) {
				say("Copies this build into an image in sd:/riftwii, a card of its own for the game, so the game can be on the SD card too.");
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
		if (backBtn.Clicked()) done = true;
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	riftwii::wii::ForgetModPictures();
}

static int MenuHome(FrontendState& state)
{
	int menu = MENU_NONE;

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
	std::vector<FlowRow> rows;
	std::vector<RowRef> refs;
	BuildGameRows(state, rows, refs);

	GameBanner banner(skin::HueFor(state.game_id));
	const std::string where = SourceWhere(state);
	GuiText whereTxt(where.c_str(), 16, skin::WithAlpha(skin::kWhite, 200));
	Place(whereTxt, 40, 10);
	const std::string title = GameTitle(state);
	GuiText titleTxt(title.c_str(), 28, skin::kWhite);
	Place(titleTxt, 40, 30);
	const bool hasCover = riftwii::wii::CoverStored(state.game_id);
	titleTxt.SetWrap(true, hasCover ? 460 : 560, 2);
	CoverArt cover(state.game_id, 528, 4);
	// The ID, and how often the game was played from RiftWii.
	const std::string played = riftwii::wii::PlayNote(state.game_id);
	const std::string idLine = played.empty() ? state.game_id : state.game_id + "   " + played;
	GuiText idTxt(idLine.c_str(), 16, skin::WithAlpha(skin::kWhite, 200));
	Place(idTxt, 40, 96);

	Panel panel(skin::panelGame, 34, 124);
	GuiFlowList list(46, 130, 548, 5);
	list.SetRows(&rows);
	list.Select(0);

	GuiText statusTxt(ModsNote(state, scanStatus).c_str(), 15, skin::kInkSoft);
	// Two lines between the card and the buttons: a cIOS remedy or a
	// compile error must stay readable in full.
	Place(statusTxt, 0, 362, true);
	statusTxt.SetWrap(true, 572, 2);

	SkinButton backBtn(skin::pill, skin::pillOver, 4, 50, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	SkinButton startBtn(skin::pillPrimary, skin::pillPrimaryOver, 4, 346, 406, "Start",
		WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_START | PAD_BUTTON_X, WIIDRC_BUTTON_PLUS);
	startBtn.text.SetColor(skin::kAccentInk);
	// Dump (a development aid) has no button: the Wii Remote's 2 or a
	// GameCube pad's Z.
	GuiTrigger trigDump;
	trigDump.SetButtonOnlyTrigger(-1, WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_X, PAD_TRIGGER_Z);
	GuiButton dumpBtn(0, 0);
	dumpBtn.SetTrigger(&trigDump);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&banner);
	w.Append(&cover);
	w.Append(&whereTxt);
	w.Append(&titleTxt);
	w.Append(&idTxt);
	w.Append(&panel);
	w.Append(&list);
	w.Append(&statusTxt);
	w.Append(&backBtn.button);
	w.Append(&startBtn.button);
	w.Append(&dumpBtn);
	mainWindow->Append(&w);
	// Start on the banner screen: this page's Start, pressed for the player,
	// so its checks and warnings still stand (the page stays when one does).
	bool fromBanner = false;
	if (g_startOnOpen) {
		g_startOnOpen = false;
		fromBanner = true;
		logf("Game page: Start from the banner screen\n");
		startBtn.button.SetState(STATE::CLICKED);
	}
	ResumeGui();

	const auto say = [&](const std::string& text) { statusTxt.SetText(text.c_str()); };
	const auto saveOrSay = [&]() {
		std::string error;
		if (SaveChoices(state, error)) return true;
		say(error);
		return false;
	};
	int shownRow = -1;
	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button, &startBtn.button});

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
						titleTxt.SetWrap(true, 460, 2);
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
					const int selected = list.Selected();
					BuildGameRows(state, rows, refs);
					list.Refresh();
					list.Select(selected);
				}
			}
		}

		const int row = list.Selected();
		if (row != shownRow && row >= 0 && static_cast<std::size_t>(row) < refs.size()) {
			shownRow = row;
			const RowRef& ref = refs[static_cast<std::size_t>(row)];
			if (ref.what == RowRef::What::Mods) say(ModsNote(state, scanStatus));
			else if (ref.what == RowRef::What::Saves) say(SaveNote(state.model));
			else if (ref.what == RowRef::What::Cheats)
				say(tr("Cheat codes for this game. Press A to choose them."));
			else if (ref.what == RowRef::What::Width)
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
			else if (ref.what == RowRef::What::Favorite)
				say(tr("Favourites have their own view on Home: press 1 there until it shows."));
			else if (ref.what == RowRef::What::Cover)
				say(tr("Downloads this game's box art from GameTDB now."));
			else if (ref.what == RowRef::What::Cios)
				say(tr("The d2x cIOS the game runs under. Automatic uses the menu's, else the first of 249, 250 and 251 that works."));
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
			bool changed = false;
			if (ref.what == RowRef::What::Saves && !state.model.pack_save_owner().empty()) {
				say(SaveNote(state.model));
			} else if (ref.what == RowRef::What::Saves) {
				static const char* const modes[] = {"nand", "separate", "fresh"};
				int at = 0;
				while (at < 3 && state.model.save_mode != modes[at]) ++at;
				state.model.save_mode = modes[((at % 3) + 3 + direction) % 3];
				changed = true;
			} else if (ref.what == RowRef::What::Mods || ref.what == RowRef::What::Cheats) {
				transition::Hold(transition::Kind::SlideForward);
				mainWindow->Remove(&w);
				if (ref.what == RowRef::What::Mods) MenuMods(state, scanStatus);
				else MenuCheats(state);
				transition::Hold(transition::Kind::SlideBack);
				mainWindow->Append(&w);
				BuildGameRows(state, rows, refs);
				list.Refresh();
				list.Select(acted);
				shownRow = -1;
			} else if (ref.what == RowRef::What::Width) {
				state.model.game.video_width = StepValue(kWidths, state.model.game.video_width, direction);
				changed = true;
			} else if (ref.what == RowRef::What::Deflicker) {
				state.model.game.deflicker = StepValue(kDeflickers, state.model.game.deflicker, direction);
				changed = true;
			} else if (ref.what == RowRef::What::Borders) {
				state.model.game.borders = StepValue(kBorderModes, state.model.game.borders, direction);
				changed = true;
			} else if (ref.what == RowRef::What::VideoMode) {
				state.model.game.video_mode = StepValue(kVideoModes, state.model.game.video_mode, direction);
				changed = true;
			} else if (ref.what == RowRef::What::RegionVideo) {
				state.model.game.region_video = state.model.game.region_video == "on" ? "off" : "on";
				changed = true;
			} else if (ref.what == RowRef::What::Aspect) {
				state.model.game.aspect = StepValue(kAspects, state.model.game.aspect, direction);
				changed = true;
			} else if (ref.what == RowRef::What::Rumble) {
				state.model.game.rumble = state.model.game.rumble == "off" ? "on" : "off";
				changed = true;
			} else if (ref.what == RowRef::What::Speaker) {
				state.model.game.speaker = state.model.game.speaker == "off" ? "on" : "off";
				changed = true;
			} else if (ref.what == RowRef::What::RegionStrings) {
				state.model.game.region_strings = state.model.game.region_strings == "on" ? "off" : "on";
				changed = true;
			} else if (ref.what == RowRef::What::Language) {
				state.model.game.language = StepValue(kGameLanguages, state.model.game.language, direction);
				changed = true;
			} else if (ref.what == RowRef::What::Cios) {
				state.model.game.cios = StepValue(kCiosChoices, state.model.game.cios, direction);
				changed = true;
			} else if (ref.what == RowRef::What::Server) {
				state.model.game.server = StepValue(kServers, state.model.game.server, direction);
				changed = true;
			} else if (ref.what == RowRef::What::Cover && !state.game_id.empty()) {
				// Started at the top of the loop, or as soon as the network's
				// thread is free; the answer is said when it comes.
				say(tr("Downloading the cover..."));
				coverStart = true;
				coverAsked = true;
			} else if (ref.what == RowRef::What::Favorite && !state.game_id.empty()) {
				std::set<std::string>& favorites = riftwii::wii::Settings().favorites;
				if (favorites.count(state.game_id) != 0) favorites.erase(state.game_id);
				else favorites.insert(state.game_id);
				if (!riftwii::wii::SaveSettings()) say(tr("Could not save the settings to the SD card."));
				BuildGameRows(state, rows, refs);
				list.Refresh();
				list.Select(acted);
			}
			if (changed) {
				saveOrSay();
				BuildGameRows(state, rows, refs);
				list.Refresh();
				list.Select(acted);
				shownRow = -1;
			}
		}

		if (backBtn.Clicked()) {
			if (saveOrSay()) menu = MENU_SOURCE;
			else backBtn.button.ResetState();
		} else if (startBtn.Clicked()) {
			startBtn.button.ResetState();
			if (state.game_id.empty()) {
				say("No game is selected; go back and pick one.");
			} else if (state.use_usb && !state.usb_catalog.cios_note.empty()) {
				// No cIOS in any candidate slot: refuse while the remedy is
				// still readable on screen.
				say(FlatCapped(state.usb_catalog.cios_note, 150));
			} else if (state.use_sd && !state.sd_catalog.cios_note.empty()) {
				say(FlatCapped(state.sd_catalog.cios_note, 150));
			} else if (const std::string problem = riftwii::wii::ModPlaceProblem(state); !problem.empty()) {
				// Mods where they cannot work: not even tried. In a popup: the
				// remedy takes more than the status line's two rows.
				ShowPopup(tr("This code mod can't start"), problem, tr("OK"));
			} else if (std::string codes_error; !riftwii::wii::CheckCodeBuilds(state, codes_error)) {
				// In a popup: what to do takes more than the status line's two rows.
				ShowPopup(tr("This code mod can't start"), codes_error, tr("OK"));
			} else if (!LaunchNote(state).empty() && !state.warning_shown) {
				state.warning_shown = true;
				// In a popup: the status line holds two rows, which cut
				// the CTGP warning off before it said what to do.
				if (fromBanner) {
					// Start on the banner screen: Play goes on with it.
					if (ShowPopup(tr("Before you play"), LaunchNote(state), tr("Play"), tr("Not now")) == 0)
						startBtn.button.SetState(STATE::CLICKED);
				} else {
					ShowPopup(tr("Before you play"), LaunchNote(state) + " " + tr("Press Start again to play."), tr("OK"));
				}
			} else if (!saveOrSay()) {
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
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	// Cheats and video settings go with every launch, mods or not.
	if (menu == MENU_LAUNCH || menu == MENU_BOOT) {
		riftwii::wii::PrepareLaunchExtras(state);
		riftwii::wii::RecordPlay(state.game_id);
		riftwii::wii::TagGame(state.game_id, true);
	}
	return menu;
}

// ---------------------------------------------------------------------------
// Settings

static const char* const kLanguages[] = {"auto", "en", "es", "ja", "pt", "it"};

// Each language by its own name, as players look for it.
static std::string LanguageName(const std::string& lang)
{
	if (lang == "en") return "English";
	if (lang == "es") return "Español";
	if (lang == "ja") return "日本語";
	if (lang == "pt") return "Português";
	if (lang == "it") return "Italiano";
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

static std::string ChannelNote(const riftwii::LoaderSettings& settings)
{
	if (riftwii::effective_update_channel(settings.update_channel, RIFTWII_VERSION) == "beta")
		return tr("Beta: every new version, including test builds that may have new bugs. For testers.");
	return tr("Stable: only versions marked stable, which testers have checked.");
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
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);

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
		ClearStaleButtons({&backBtn.button});
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
		if (backBtn.Clicked()) done = true;
		ResumeGui();
	}
	HaltGui();
	if (started && !was_running) riftwii::wii::GcAdapterStop();
	mainWindow->Remove(&w);
	ResumeGui();
}

// Settings > Credits and license: RiftWii's license notice, who its
// parts come from and the GNU GPL in full (wii/credits.hpp).
static void CreditsPage()
{
	GuiText titleTxt(tr("Credits and license"), 30, skin::kInk);
	Place(titleTxt, 40, 28);
	TitleBand titleBand;
	GuiText versionTxt("GPL-3.0-or-later", 15, skin::kInkDim);
	versionTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	versionTxt.SetPosition(-40, 40);
	// About 840 lines: moved into the rows, not copied (the MEM1 heap is small).
	std::vector<FlowRow> rows;
	{
		std::vector<std::string> lines = riftwii::wii::CreditsLines(64);
		rows.resize(lines.size());
		for (std::size_t i = 0; i < lines.size(); ++i) rows[i].label = std::move(lines[i]);
	}
	Panel panel(skin::panelSettings, 34, 76);
	GuiFlowList list(46, 82, 548, 6);
	list.SetRows(&rows);
	list.Select(0);
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&titleTxt);
	w.Append(&versionTxt);
	w.Append(&panel);
	w.Append(&list);
	w.Append(&backBtn.button);
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

static int MenuSettings(FrontendState& state)
{
	int menu = MENU_NONE;
	riftwii::LoaderSettings& settings = riftwii::wii::Settings();

	const std::vector<int> iosChoices = riftwii::wii::MenuIosChoices();
	int iosSlot = riftwii::wii::LoadMenuIos();
	const bool iosChoosable = iosChoices.size() > 1 || iosSlot != 0;

	bool netOn = riftwii::wii::NetworkPacksEnabled();
	// The themes on the card (wii/menutheme.hpp), read once per visit.
	const std::vector<riftwii::wii::ThemeEntry> themes = riftwii::wii::ListMenuThemes();
	const auto themeName = [&](const std::string& folder) -> std::string {
		if (folder == "default") return tr("Default");
		for (const riftwii::wii::ThemeEntry& t : themes)
			if (t.folder == folder) return t.name;
		return folder;
	};
	enum RowAction { kLanguage, kWidth, kDeflicker, kBorders, kVideoMode, kGameLanguage, kGameCios, kServer, kHomeTiles, kDiscTile, kWidescreen, kScreenSize, kTheme, kFont, kSounds, kMusic, kReturnTo, kShots, kOnline, kNames, kGcAdapter, kGcTest, kIos, kNet, kResync,
		kRescan, kChannel, kUpdate, kReport, kWiiChannel, kTutorial, kCredits, kExit, kNone, kHeading };
	// The RiftWii channel on the Wii Menu (wii/channel.hpp).
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
		option(tr("Game language"), GameLanguageName(settings.game_language), settings.game_language != "console",
			kGameLanguage);
		option(tr("Game cIOS"), CiosName(settings.game_cios), settings.game_cios != "auto", kGameCios);
		option(tr("Online server"), ServerName(settings.wfc_server), settings.wfc_server != "off", kServer);
		option(tr("Wii Menu button"), settings.return_to == "menu" ? tr("Wii Menu") : tr("Back to RiftWii"),
			settings.return_to != "menu", kReturnTo);
		option(tr("In-game screenshots"), settings.screenshots == "demo" ? std::string("Demo")
			: settings.screenshots == "on" ? tr("On") : tr("Off"), settings.screenshots != "off", kShots,
			FlowRow::Kind::Toggle);
		option(tr("GameCube adapter"), settings.gc_adapter == "demo" ? std::string("Demo")
			: settings.gc_adapter == "on" ? tr("On") : settings.gc_adapter == "off" ? tr("Off") : tr("Automatic"),
			settings.gc_adapter != "off", kGcAdapter);
		FlowRow gcTest;
		gcTest.kind = FlowRow::Kind::Action;
		gcTest.label = tr("Check the GameCube adapter");
		gcTest.value = tr("Test");
		rows.push_back(gcTest);
		actions.push_back(kGcTest);
		heading(tr("Menu"));
		option(tr("Language"), LanguageName(settings.language), settings.language != "auto", kLanguage);
		option(tr("Home tiles"), settings.home_tiles == "names" ? tr("Names") : settings.home_tiles == "shelf" ? tr("Shelf")
			: settings.home_tiles == "channels" ? tr("Channels") : tr("Covers"),
			settings.home_tiles != "names", kHomeTiles);
		option(tr("Disc Channel"), settings.home_disc == "off" ? tr("Off") : tr("On"), settings.home_disc != "off",
			kDiscTile, FlowRow::Kind::Toggle);
		option(tr("Widescreen menu"), settings.menu_widescreen == "on" ? std::string("16:9")
			: settings.menu_widescreen == "off" ? std::string("4:3")
			: std::string(tr("Automatic")) + (riftwii::wii::MenuWidescreen() ? " (16:9)" : " (4:3)"),
			settings.menu_widescreen != "off", kWidescreen);
		option(tr("Screen size"), std::to_string(settings.screen_size) + "%", settings.screen_size != 100, kScreenSize);
		option(tr("Theme"), themeName(settings.theme), settings.theme != "default", kTheme);
		option(tr("Menu font"), settings.menu_font == "wii" ? tr("Wii Menu") : "RiftWii", settings.menu_font == "wii", kFont);
		option(tr("Menu sounds"), MenuSoundsName(settings.menu_sounds), settings.menu_sounds != "off", kSounds);
		option(tr("Menu music"), settings.menu_music == "off" ? tr("Off") : tr("On"), settings.menu_music != "off",
			kMusic, FlowRow::Kind::Toggle);
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
		{
			const bool beta = riftwii::effective_update_channel(settings.update_channel, RIFTWII_VERSION) == "beta";
			option(tr("Updates"), beta ? tr("Beta") : tr("Stable"), beta, kChannel);
		}
		FlowRow update;
		update.kind = FlowRow::Kind::Action;
		update.label = tr("Check for a new version");
		update.value = tr("Check");
		update.dim = !settings.online;
		rows.push_back(update);
		actions.push_back(kUpdate);
		heading(tr("More"));
		FlowRow report;
		report.kind = FlowRow::Kind::Action;
		report.label = tr("Send a problem report");
		report.value = tr("Send");
		rows.push_back(report);
		actions.push_back(kReport);
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
	};
	build();

	GuiText titleTxt("Settings", 30, skin::kInk);
	Place(titleTxt, 40, 28);
	TitleBand titleBand;
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
	Panel panel(skin::panelGame, 34, 76);
	GuiFlowList list(46, 82, 548, 5);
	list.SetRows(&rows);
	list.Select(1);  // row 0 is the first group's name
	GuiText noteTxt(tr("These apply to every game. A game's own page can change them for that game."), 16, skin::kInkSoft);
	Place(noteTxt, 52, 312);
	noteTxt.SetWrap(true, 536, 4);

	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleBand);
	w.Append(&titleTxt);
	w.Append(&versionTxt);
	w.Append(&panel);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
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
			case kGameLanguage: return tr("The language the game is told the console uses. Pick one the game has: some games stop without it.");
			case kGameCios: return tr("The d2x cIOS the game runs under. Automatic uses the menu's, else the first of 249, 250 and 251 that works.");
			case kHomeTiles: return tr("Covers: each game's box art from GameTDB (fetched when downloads are on). Shelf: the boxes on a shelf. Channels: each game's animated icon, and its banner when you pick it, as on the Wii Menu. Names: the names only.");
			case kWidescreen: return tr("On a 16:9 TV the menu is drawn narrower, so covers and pictures keep their shape. Automatic follows the Wii's own TV setting.");
			case kScreenSize: return tr("Makes the menu smaller on screen, so nothing is cut off at the TV's edges. Lower it until the whole menu shows.");
			case kTheme: return tr("The menu's colours and pictures. Themes are folders in sd:/riftwii/themes (docs/THEMES.md on GitHub).");
			case kFont: return tr("The letters the menu is written in: RiftWii's own, or the Wii Menu's, read from this Wii.");
			case kSounds: return tr("How loud the menu's clicks are. Quiet softens the tick the pointer makes moving onto something.");
			case kReturnTo: return ReturnToNote();
			case kShots: return tr("Experimental. In a game, hold 1 and press HOME (GameCube controller: hold L and R, press Down). Pictures go to sd:/riftwii/screenshots when RiftWii next starts. Some games and mods don't work with it.");
			case kDiscTile: return DiscTileNote();
			case kMusic: return riftwii::wii::MenuMusicFound() ? tr("Music while the menu is open: music.ogg from sd:/riftwii, or the one in RiftWii's own folder.") : tr("No music.ogg found in sd:/riftwii or in RiftWii's own folder.");
			case kServer: return tr("The online server the game uses in place of Nintendo's, which closed. Custom uses wfc_domain in settings.txt.");
			case kOnline:
				return settings.online ? tr("Game names and cheats are downloaded when the Wii is online.")
					: tr("Nothing is downloaded. Names and cheats already on the card are still used.");
			case kNames: return tr("Downloads the newest game names from GameTDB.");
			case kGcAdapter: return AdapterNote(settings.gc_adapter);
			case kGcTest: return tr("Shows live what the controllers in the adapter are pressing.");
			case kTutorial: return tr("The short tour of RiftWii's basics that a new SD card starts with.");
			case kCredits: return tr("Who RiftWii's parts come from, its license (the GNU GPL, version 3 or later) and where its source is.");
			case kIos: return MenuIosNote(iosSlot);
			case kNet:
				return netOn ? "Looks for a PC running a RiiFS server when the games are read. Rescan to look now."
					: "Only servers named by <network> in an XML on the card are used.";
			case kResync: return "The next launch copies every file of its network packs again.";
			case kRescan: return tr("Reads the SD card and the USB drive again.");
			case kChannel: return ChannelNote(settings);
			case kUpdate: return tr("This is RiftWii {1}. Looks on GitHub for a newer release.", {RIFTWII_VERSION});
			case kReport: return tr("Something went wrong? Sends what it takes to find out to a paste site, and shows a link to pass on.");
			case kWiiChannel:
				if (!channelCan) return std::string(tr(channelWhy.c_str())) + ".";
				return tr("A Wii Menu channel that starts RiftWii from the SD card. It holds no copy of RiftWii, so updates keep working. Opens the channel installer, to add, update or remove it.");
			case kExit: return tr("Opens the HOME Menu, as HOME does: the Homebrew Channel, the Wii Menu, Priiloader or power off.");
			case kHeading: return "";
			case kNone:  // the Menu IOS row when there is nothing to choose
				return tr("No d2x cIOS was found in slots 248 to 252, so the menu runs under IOS 58. Install d2x to play games from SD or USB.");
			default: return "";
		}
	};
	int shownRow = list.Selected();
	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button});
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
					riftwii::wii::SetMenuLanguage(riftwii::wii::MenuLanguage());
					namesStale = true;  // fetched once, on the way out
					titleTxt.SetText("Settings");
					backBtn.text.SetText("Back");
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
					saveAndNote(riftwii::wii::MenuMusicFound() ? tr("Music while the menu is open: music.ogg from sd:/riftwii, or the one in RiftWii's own folder.") : tr("No music.ogg found in sd:/riftwii or in RiftWii's own folder."));
					rebuild();
					break;
				case kHomeTiles:
					settings.home_tiles = settings.home_tiles == "covers" ? "shelf" : settings.home_tiles == "shelf" ? "channels"
						: settings.home_tiles == "channels" ? "names" : "covers";
					saveAndNote(tr("Covers: each game's box art from GameTDB (fetched when downloads are on). Shelf: the boxes on a shelf. Channels: each game's animated icon, and its banner when you pick it, as on the Wii Menu. Names: the names only."));
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
					if (themes.empty()) {
						note(tr("No themes in sd:/riftwii/themes. The zip's themes folder has one to copy there."));
						break;
					}
					std::vector<std::string> folders{"default"};
					for (const riftwii::wii::ThemeEntry& t : themes) folders.push_back(t.folder);
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
					note(tr("The menu restarts to show a theme."));
					if (ShowPopup(tr("Restart the menu?"),
						    tr("RiftWii's menu restarts to show {1}. Your games and settings stay as they are.", {themeName(settings.theme)}),
						    tr("Restart"), tr("Later")) == 0) {
						logf("Settings: theme %s; restarting the menu\n", settings.theme.c_str());
						riftwii::wii::WarmRestart(riftwii::wii::RestartKind::Theme, tr("Theme: {1}", {themeName(settings.theme)}));
						note(tr("RiftWii could not restart. Start it again from the Homebrew Channel."));
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
					const std::string fontName = settings.menu_font == "wii" ? tr("the Wii Menu's font") : tr("RiftWii's font");
					note(tr("The menu restarts to change its font."));
					if (ShowPopup(tr("Restart the menu?"),
						    tr("RiftWii's menu restarts to show {1}. Your games and settings stay as they are.", {fontName}),
						    tr("Restart"), tr("Later")) == 0) {
						logf("Settings: menu font %s; restarting the menu\n", settings.menu_font.c_str());
						riftwii::wii::WarmRestart(riftwii::wii::RestartKind::MenuFont,
							tr("Menu font: {1}", {settings.menu_font == "wii" ? tr("Wii Menu") : std::string("RiftWii")}));
						note(tr("RiftWii could not restart. Start it again from the Homebrew Channel."));
					}
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
				case kChannel:
					// Stable and Beta; the setting stays explicit once changed.
					settings.update_channel =
						riftwii::effective_update_channel(settings.update_channel, RIFTWII_VERSION) == "beta" ? "stable" : "beta";
					saveAndNote(ChannelNote(settings));
					rebuild();
					break;
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
						note(tr("No stable version is out yet. This is RiftWii {1}.", {RIFTWII_VERSION}));
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
		if (menu == MENU_NONE && backBtn.Clicked())
			menu = MENU_SOURCE;
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

// ---------------------------------------------------------------------------
// No SD card: RiftWii keeps its settings, logs and saves there, so it
// does not run without one (USB mode is not supported yet). A QR code
// leads to SD cards to buy.

static bool g_noSdFromUsb = false;

void SetNoSdCard(bool fromUsb) { g_noSdFromUsb = fromUsb; }

static int MenuNeedsSd()
{
	GuiText titleTxt(tr("RiftWii needs an SD card"), 28, skin::kInk);
	Place(titleTxt, 40, 36);
	GuiText subTxt(g_noSdFromUsb ? tr("USB mode is not supported yet") : tr("No SD card was found"), 18,
		skin::kAccentInk);
	Place(subTxt, 40, 74);
	Panel card(skin::panelSettings, 34, 120);
	GuiText bodyTxt(g_noSdFromUsb
			? tr("RiftWii was started from a USB drive. It keeps its settings, logs and saves on the SD card, so for now it needs one to run. Copy the sd-card folder from the RiftWii zip to a FAT32 SD card, put the card in the Wii and start RiftWii from it.")
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
	SkinButton exitBtn(skin::pill, skin::pillOver, 4, 198, 412, tr("Exit"),
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B | WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, PAD_BUTTON_B | PAD_BUTTON_START,
		WIIDRC_BUTTON_B | WIIDRC_BUTTON_HOME);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&subTxt);
	w.Append(&card);
	w.Append(&bodyTxt);
	w.Append(&qr);
	w.Append(&scanTxt);
	w.Append(&exitBtn.button);
	mainWindow->Append(&w);
	ResumeGui();
	for (;;) {
		usleep(THREAD_SLEEP);
		HaltGui();
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
	const char* doing = action == MENU_DUMP ? "Dumping files from"
		: action == MENU_CHANNEL ? "Opening the installer for" : "Starting";
	// The game page's band in the game's colour, as the page it starts from.
	GameBanner banner(skin::HueFor(action == MENU_CHANNEL ? std::string("RIFTWII") : state.game_id));
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
	GuiText footTxt(riftwii::wii::NetBackgroundBusy() && !riftwii::wii::CoverFetchGame().empty() ? "Waiting for a cover download to finish..."
		: riftwii::wii::NetBackgroundBusy() ? "Waiting for the update check to finish (up to 20 seconds)..."
		: action == MENU_CHANNEL ? "RiftWii starts again when it is done."
		: "The game takes over the screen when it is ready.", 15, skin::kInkDim);
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
		currentMenu != MENU_CHANNEL)
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
	if (currentMenu == MENU_LAUNCH || currentMenu == MENU_BOOT || currentMenu == MENU_DUMP || currentMenu == MENU_CHANNEL) {
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
