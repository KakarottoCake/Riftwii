// SPDX-License-Identifier: GPL-3.0-or-later
/****************************************************************************
 * RiftWii
 *
 * rift_menu.cpp
 * The frontend, in a light Wii-Menu-like look (skin.hpp):
 *   Home      the games on the SD card and the USB drive as a page of
 *             tiles (plus the disc drive), a filter (games with mods, the
 *             default, or all games), the clock, and Settings.
 *   Game      the picked game's banner and its mod packs: A turns a pack
 *             on or off and steps its options, Saves picks where the saves
 *             go, Start leaves for the boot.
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
#include <atomic>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <wiiuse/wpad.h>

#include "libwiigui/gui.h"
#include "gui_flowlist.hpp"
#include "gui_gamegrid.hpp"
#include "guiscript.hpp"
#include "skin.hpp"
#include "wiidrc.h"
#include "menu.h"
#include "autorun.hpp"
#include "demo.h"
#include "input.h"
#include "riftwii/patch.hpp"
#include "log.hpp"
#include "menuios.hpp"
#include "netpacks.hpp"
#include "video.h"

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
		default: return "?";
	}
}

static GuiWindow * mainWindow = nullptr;
static skin::GuiBackdrop * backdrop = nullptr;
static GuiSound * soundOver = nullptr;
static lwp_t guithread = LWP_THREAD_NULL;
static std::atomic<bool> guiHalt{true};
static std::atomic<bool> hidePointers{false};

static void ResumeGui()
{
	guiHalt = false;
	LWP_ResumeThread(guithread);
}

static void HaltGui()
{
	guiHalt = true;
	while(!LWP_ThreadIsSuspended(guithread))
		usleep(THREAD_SLEEP);
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
			mainWindow->Draw();

			for(i = 3; i >= 0; i--)
			{
				if(!hidePointers && userInput[i].wpad->ir.valid)
					Menu_DrawImg(userInput[i].wpad->ir.x-48, userInput[i].wpad->ir.y-48,
						96, 96, skin::hand[i].data, userInput[i].wpad->ir.angle, 1, 1, 255);
				DoRumble(i);
			}

			Menu_Render();
			riftwii::wii::GuiScriptAfterFrame(Menu_CurrentXfb(), Menu_XfbWidth(), Menu_XfbHeight());

			for(i = 0; i < 4; i++)
				mainWindow->Update(&userInput[i]);

			if(ExitRequested)
			{
				for(i = 0; i <= 255; i += 15)
				{
					mainWindow->Draw();
					Menu_DrawRectangle(0,0,screenwidth,screenheight,(GXColor){0, 0, 0, (u8)i},1);
					Menu_Render();
				}
				ExitApp();
			}
		}
	}
	return nullptr;
}

void InitGUIThreads()
{
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

// A painted button (skin textures) triggered by A and by a hotkey. The
// Wii U GamePad mirrors the Wii names; every button carries a GamePad and
// a GameCube hotkey so the menus are drivable without a pointer.
struct SkinButton {
	GuiImage image;
	GuiImage imageOver;
	GuiImage icon;
	GuiText text;
	GuiTrigger trigA;
	GuiTrigger trigHot;
	GuiButton button;
	// `x`, `y`: where the visible shape starts; `margin`: the texture's
	// transparent border around it.
	SkinButton(const skin::Tex& face, const skin::Tex& faceOver, int margin, int x, int y, const char* label,
		   u32 wpadHot, u16 padHot, u16 drcHot, const skin::Tex* iconTex = nullptr)
		: image(face.data, face.w, face.h), imageOver(faceOver.data, faceOver.w, faceOver.h),
		  icon(iconTex ? iconTex->data : nullptr, iconTex ? iconTex->w : 0, iconTex ? iconTex->h : 0),
		  text(label, 22, skin::kInk), button(face.w, face.h)
	{
		trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, WIIDRC_BUTTON_A);
		trigHot.SetButtonOnlyTrigger(-1, wpadHot, padHot, drcHot);
		button.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		button.SetPosition(x - margin, y - margin);
		button.SetImage(&image);
		button.SetImageOver(&imageOver);
		if (label) button.SetLabel(&text);
		if (iconTex) {
			icon.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
			button.SetIcon(&icon);
		}
		if (soundOver) button.SetSoundOver(soundOver);
		button.SetTrigger(&trigA);
		if (wpadHot || padHot || drcHot) button.SetTrigger(&trigHot);
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
	if (flat.size() > max) flat = flat.substr(0, max) + "...";
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
	if (reason.compare(0, 8, "No valid") == 0) return std::string(tag) + ": no games in /wbfs or /games";
	if (reason == "no USB mass-storage device is inserted") return "";  // no drive is not a problem
	if (reason == "no SD card is inserted") return "SD: no card";
	return std::string(tag) + ": " + FlatCapped(reason, 110);
}

// The Menu IOS setting's label and what choosing a slot means.
static std::string MenuIosLabel(int slot)
{
	return slot == 0 ? "IOS 58" : "IOS " + std::to_string(slot);
}
static std::string MenuIosNote(int slot)
{
	const int running = riftwii::wii::MenuCiosSlot();
	std::string note = slot == 0
		? "The menu runs under the Homebrew Channel's IOS (the default)."
		: "The menu and every game run under cIOS " + std::to_string(slot) +
		  ", so a cIOS with fakemote makes USB DS3/DS4 pads work as Wii Remotes. USB drives in the menu need a base-58 cIOS.";
	if (slot != running) note += " Takes effect the next time RiftWii starts.";
	return note;
}

// The pack's name without ".xml", for display.
static std::string PackName(const std::string& file)
{
	if (file.size() > 4) {
		const std::string ext = file.substr(file.size() - 4);
		if (strcasecmp(ext.c_str(), ".xml") == 0) return file.substr(0, file.size() - 4);
	}
	return file;
}

// What the status line says about a focused pack.
static std::string PackSummary(const riftwii::LaunchPackage& p)
{
	if (!p.valid) return "This XML cannot be read; the error is listed under it.";
	const std::size_t n = p.package.options.size();
	if (!p.enabled) return n == 0 ? "Off. A turns it on." : "Off. A turns it on and shows its " + std::to_string(n) + (n == 1 ? " setting." : " settings.");
	std::size_t on = 0;
	for (const riftwii::Option& o : p.package.options)
		if (o.selected != 0 && o.selected <= o.choices.size()) ++on;
	if (n == 0) return "On. It applies as a whole.";
	if (on == 0) return "On, but nothing chosen yet: pick its settings below.";
	return "On, " + std::to_string(on) + " of " + std::to_string(n) + " settings chosen.";
}

// ---------------------------------------------------------------------------
// Home

enum class Filter { Mods, All };
static Filter g_filter = Filter::Mods;
static bool g_scanned = false;   // the drives were read this session
static int g_homeFocus = 0;      // the focused tile, kept across screens
static riftwii::PackIndex g_packs;

static const char* FilterLabel(Filter f)
{
	switch (f) {
		case Filter::Mods: return "Games with mods";
		default: return "All games";
	}
}

// Which games have packs: every XML in sd:/riivolution and in the
// network packs' cache, by game ID only.
static void LoadPackIndex()
{
	g_packs = riftwii::PackIndex();
	bool limited = false;
	for (const riftwii::wii::PackFile& pack : riftwii::wii::ListPackFiles(256, limited)) {
		std::ifstream in(pack.path, std::ios::binary);
		if (!in) continue;
		std::stringstream text;
		text << in.rdbuf();
		g_packs.add(text.str());
	}
	logf("Home: %u pack(s) indexed\n", static_cast<unsigned>(g_packs.size()));
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
		disc.title = "Disc drive";
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
		return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	});
	for (const Row& r : rows) {
		const bool mods = g_packs.has_packs(r.game->id);
		if (g_filter == Filter::Mods && !mods) continue;
		GridItem item;
		item.title = r.name;
		item.id = r.game->id;
		item.badge = r.entry.kind == HomeEntry::Kind::Usb ? "USB" : "SD";
		item.mods = mods;
		item.hue = skin::HueFor(r.game->id);
		items.push_back(std::move(item));
		entries.push_back(r.entry);
	}
}

// The bottom bar with the clock's bump.
class HomeBar : public GuiElement {
public:
	void Draw() override { skin::Draw(skin::bar, 0, 356); }
};

static std::string HomeStatus(const FrontendState& state, std::size_t shown)
{
	std::string status;
	for (const std::string& p : {ShortSourceProblem("SD", state.sd_catalog), ShortSourceProblem("USB", state.usb_catalog)}) {
		if (p.empty()) continue;
		status += (status.empty() ? "" : "   ") + p;
	}
	if (!state.usb_catalog.cios_note.empty() || !state.sd_catalog.cios_note.empty())
		status += std::string(status.empty() ? "" : "   ") + "No d2x cIOS in 249-251: games cannot boot yet";
	if (!status.empty()) return status;
	if (g_filter == Filter::Mods && shown <= 1) return "No game here has packs in sd:/riivolution yet. Press 1 for all games.";
	if (shown <= 1) return "No games found (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)";
	return std::string(FilterLabel(g_filter)) + "   1: filter   2: settings   +: rescan";
}

static void ScanDrives(FrontendState& state, GuiText& status)
{
	std::string error;
	status.SetText("Reading the SD card...");
	ResumeGui();
	const bool sd = scan_sd_games(state.sd_catalog, error);
	const std::string net = riftwii::wii::RefreshNetworkPacks([&](const char* line) { status.SetText(line); });
	if (!net.empty()) logf("%s\n", net.c_str());
	LoadPackIndex();
	HaltGui();
	if (!sd) {
		logf("SD scan failed: %s\n", error.c_str());
		state.sd_catalog.status = "SD: " + (error.empty() ? "scan failed" : error);
	}
	error.clear();
	status.SetText("Reading the USB drive... (a big drive takes a moment)");
	ResumeGui();
	const bool usb = scan_usb_games(state.usb_catalog, error);
	HaltGui();
	if (!usb) {
		logf("USB scan failed: %s\n", error.c_str());
		state.usb_catalog.status = "USB: " + (error.empty() ? "scan failed" : error);
		if (riftwii::wii::MenuCiosSlot() != 0 && error.find("more than one") == std::string::npos) {
			state.usb_catalog.status += " (the menu runs under IOS" + std::to_string(riftwii::wii::MenuCiosSlot()) +
				"; USB drives need a base-58 cIOS for that, or set the menu IOS back to 58)";
		}
	}
	g_scanned = true;
}

static void ClockText(std::string& clock, std::string& date)
{
	const time_t now = time(nullptr);
	struct tm local;
	localtime_r(&now, &local);
	static const char* const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
	const int h = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
	char buf[32];
	snprintf(buf, sizeof(buf), "%d:%02d %s", h, local.tm_min, local.tm_hour < 12 ? "AM" : "PM");
	clock = buf;
	snprintf(buf, sizeof(buf), "%s %d/%d", days[local.tm_wday % 7], local.tm_mon + 1, local.tm_mday);
	date = buf;
}

static int MenuSource(FrontendState& state)
{
	int menu = MENU_NONE;
	std::vector<GridItem> items;
	std::vector<HomeEntry> entries;
	BuildHome(state, items, entries);

	GuiGameGrid grid;
	grid.SetItems(&items);
	grid.Focus(g_homeFocus);
	HomeBar bar;

	GuiText pageTxt("", 15, skin::kInkDim);
	Place(pageTxt, 40, 300);
	std::string clock, date;
	ClockText(clock, date);
	GuiText clockTxt(clock.c_str(), 34, skin::kClock);
	Place(clockTxt, 0, 310, true);
	GuiText dateTxt(date.c_str(), 16, skin::kInkSoft);
	Place(dateTxt, 0, 378, true);
	GuiText statusTxt("", 15, skin::kInkSoft);
	Place(statusTxt, 0, 408, true);
	statusTxt.SetWrap(true, 400);

	SkinButton filterBtn(skin::roundBtn, skin::roundBtnOver, 2, 26, 386, nullptr,
		WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y, WIIDRC_BUTTON_X, &skin::iconDrives);
	SkinButton settingsBtn(skin::roundBtn, skin::roundBtnOver, 2, 538, 386, nullptr,
		WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_X, PAD_TRIGGER_R, WIIDRC_BUTTON_Y, &skin::iconGear);
	GuiTrigger trigRescan, trigExit;
	trigRescan.SetButtonOnlyTrigger(-1, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_X, WIIDRC_BUTTON_PLUS);
	trigExit.SetButtonOnlyTrigger(-1, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, PAD_BUTTON_START, WIIDRC_BUTTON_HOME);
	GuiButton rescanBtn(0, 0), exitBtn(0, 0);  // hotkeys only
	rescanBtn.SetTrigger(&trigRescan);
	exitBtn.SetTrigger(&trigExit);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&grid);
	w.Append(&bar);
	w.Append(&pageTxt);
	w.Append(&clockTxt);
	w.Append(&dateTxt);
	w.Append(&statusTxt);
	w.Append(&filterBtn.button);
	w.Append(&settingsBtn.button);
	w.Append(&rescanBtn);
	w.Append(&exitBtn);
	mainWindow->Append(&w);

	const auto refresh = [&](bool keepFocus) {
		const int focus = keepFocus ? grid.FocusedIndex() : 0;
		BuildHome(state, items, entries);
		grid.SetItems(&items);
		grid.Focus(std::min(focus, std::max(0, static_cast<int>(items.size()) - 1)));
		statusTxt.SetText(HomeStatus(state, items.size()).c_str());
	};
	if (!g_scanned) {
		ScanDrives(state, statusTxt);
		refresh(true);
	} else {
		statusTxt.SetText(HomeStatus(state, items.size()).c_str());
	}
	ResumeGui();

	int shownPage = -1, shownPages = -1;
	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&filterBtn.button, &settingsBtn.button});
		if (grid.Page() != shownPage || grid.Pages() != shownPages) {
			shownPage = grid.Page();
			shownPages = grid.Pages();
			const std::string page = shownPages > 1 ? "Page " + std::to_string(shownPage + 1) + " of " + std::to_string(shownPages) : "";
			pageTxt.SetText(page.c_str());
		}
		std::string nowClock, nowDate;
		ClockText(nowClock, nowDate);
		if (nowClock != clock) {
			clock = nowClock;
			date = nowDate;
			clockTxt.SetText(clock.c_str());
			dateTxt.SetText(date.c_str());
		}

		const int clicked = grid.GetClicked();
		if (clicked >= 0 && static_cast<std::size_t>(clicked) < entries.size()) {
			g_homeFocus = clicked;
			const HomeEntry entry = entries[static_cast<std::size_t>(clicked)];
			std::string error;
			statusTxt.SetText(entry.kind == HomeEntry::Kind::Disc ? "Reading the disc..." : "Opening the game...");
			ResumeGui();
			bool ok = false;
			if (entry.kind == HomeEntry::Kind::Disc) {
				logf("DISC: probing\n");
				ok = SelectDisc(state, error);
				if (!ok && error.empty()) error = "No disc in the drive";
			} else if (entry.kind == HomeEntry::Kind::Usb) {
				ok = SelectUsbGame(state, entry.index, error);
			} else {
				ok = SelectSdGame(state, entry.index, error);
			}
			HaltGui();
			if (ok) {
				menu = MENU_HOME;
			} else {
				logf("Home: %s\n", error.c_str());
				statusTxt.SetText(FlatCapped(error, 150).c_str());
			}
		}
		if (menu != MENU_NONE) {
			// picked
		} else if (exitBtn.GetState() == STATE::CLICKED) {
			menu = MENU_EXIT;
		} else if (settingsBtn.Clicked()) {
			g_homeFocus = grid.FocusedIndex();
			menu = MENU_OPTIONS;
		} else if (filterBtn.Clicked()) {
			filterBtn.button.ResetState();
			g_filter = g_filter == Filter::Mods ? Filter::All : Filter::Mods;
			logf("Home: filter %s\n", FilterLabel(g_filter));
			refresh(false);
		} else if (rescanBtn.GetState() == STATE::CLICKED) {
			rescanBtn.ResetState();
			ScanDrives(state, statusTxt);
			refresh(true);
		}
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

// ---------------------------------------------------------------------------
// Game page

// The game's banner: its hue across the top with light stripes.
class GameBanner : public GuiElement {
public:
	explicit GameBanner(GXColor hue) : hue(hue) {}
	void Draw() override {
		Menu_DrawRectangle(0, 0, screenwidth, 184, hue, 1);
		skin::Draw(skin::bannerStripes, 0, -8);
	}
private:
	GXColor hue;
};

// A white card at (x, y) drawn from a panel texture.
class Panel : public GuiElement {
public:
	Panel(const skin::Tex& tex, int x, int y) : tex(tex), x(x), y(y) {}
	void Draw() override { skin::Draw(tex, x - 4.0f, y - 4.0f); }
private:
	const skin::Tex& tex;
	int x, y;
};

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
	enum class What { Saves, Pack, Option, Note } what = What::Note;
	std::size_t pkg = 0, opt = 0;
};

static void BuildGameRows(const FrontendState& state, const std::string& scanStatus,
			  std::vector<FlowRow>& rows, std::vector<RowRef>& refs)
{
	rows.clear();
	refs.clear();
	const auto add = [&](FlowRow row, RowRef ref) {
		rows.push_back(std::move(row));
		refs.push_back(ref);
	};
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

	std::size_t shown = 0;
	for (std::size_t i = 0; i < state.model.packages.size(); ++i) {
		const riftwii::LaunchPackage& p = state.model.packages[i];
		if (!riftwii::show_package(p)) continue;
		++shown;
		FlowRow head;
		head.kind = FlowRow::Kind::Header;
		head.label = PackName(p.file);
		head.value = !p.valid ? "Broken" : p.enabled ? "On" : "Off";
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
		if (!p.enabled) continue;
		for (std::size_t o = 0; o < p.package.options.size(); ++o) {
			// An option merged across packs shows once, under the
			// first enabled pack that has it.
			if (!state.model.option_shown(i, o)) continue;
			const riftwii::Option& option = p.package.options[o];
			FlowRow row;
			row.kind = FlowRow::Kind::Option;
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
			: std::to_string(state.model.packages.size()) + " XML file(s) are for other games";
		add(hint, {RowRef::What::Note});
	}
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
	return state.disc_title.empty() ? state.game_id : state.disc_title;
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
	std::vector<FlowRow> rows;
	std::vector<RowRef> refs;
	BuildGameRows(state, scanStatus, rows, refs);

	GameBanner banner(skin::HueFor(state.game_id));
	const std::string where = SourceWhere(state);
	GuiText whereTxt(where.c_str(), 16, skin::WithAlpha(skin::kWhite, 200));
	Place(whereTxt, 40, 30);
	const std::string title = GameTitle(state);
	GuiText titleTxt(title.c_str(), 30, skin::kWhite);
	Place(titleTxt, 40, 52);
	titleTxt.SetWrap(true, 560);
	GuiText idTxt(state.game_id.c_str(), 16, skin::WithAlpha(skin::kWhite, 200));
	Place(idTxt, 40, 150);

	Panel panel(skin::panelGame, 34, 196);
	GuiFlowList list(46, 203, 548, 5);
	list.SetRows(&rows);
	list.Select(0);

	GuiText statusTxt(SaveNote(state.model).c_str(), 15, skin::kInkSoft);
	Place(statusTxt, 0, 380, true);
	statusTxt.SetMaxWidth(572);

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

		const int row = list.Selected();
		if (row != shownRow && row >= 0 && static_cast<std::size_t>(row) < refs.size()) {
			shownRow = row;
			const RowRef& ref = refs[static_cast<std::size_t>(row)];
			if (ref.what == RowRef::What::Saves) say(SaveNote(state.model));
			else if (ref.what == RowRef::What::Pack) say(PackSummary(state.model.packages[ref.pkg]));
			else if (ref.what == RowRef::What::Option) {
				const riftwii::Option& o = state.model.packages[ref.pkg].package.options[ref.opt];
				say(o.section.empty() ? PackName(state.model.packages[ref.pkg].file) : o.section);
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
			if (ref.what == RowRef::What::Saves && !state.model.pack_save_owner().empty()) {
				say(SaveNote(state.model));
			} else if (ref.what == RowRef::What::Saves) {
				static const char* const modes[] = {"nand", "separate", "fresh"};
				int at = 0;
				while (at < 3 && state.model.save_mode != modes[at]) ++at;
				state.model.save_mode = modes[((at % 3) + 3 + direction) % 3];
				changed = true;
			} else if (ref.what == RowRef::What::Pack) {
				riftwii::LaunchPackage& p = state.model.packages[ref.pkg];
				if (!p.valid) say("This XML cannot be read; fix it on the card and come back.");
				else changed = state.model.set_enabled(ref.pkg, !p.enabled);
				if (!changed && p.valid) say("This pack cannot be turned on.");
			} else if (ref.what == RowRef::What::Option) {
				changed = state.model.cycle(ref.pkg, ref.opt, direction);
			}
			if (changed) {
				saveOrSay();
				BuildGameRows(state, scanStatus, rows, refs);
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
			} else if (!saveOrSay()) {
				// the status shows why
			} else if (!riftwii::needs_launch_pipeline(!state.model.selections().empty(), state.model.save_mode)) {
				menu = MENU_BOOT;  // no resident work: boot the game as it is
			} else if (state.use_usb || state.use_sd) {
				// cIOS reload and F9 happen after the GUI exits; compilation
				// follows the virtual DI probe in RunLaunch.
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
					say(FlatCapped(error, 150));
				}
			}
		} else if (dumpBtn.GetState() == STATE::CLICKED) {
			menu = MENU_DUMP;
		}
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

// ---------------------------------------------------------------------------
// Settings

static int MenuSettings(FrontendState& state)
{
	(void)state;
	int menu = MENU_NONE;

	const std::vector<int> iosChoices = riftwii::wii::MenuIosChoices();
	int iosSlot = riftwii::wii::LoadMenuIos();
	const bool iosChoosable = iosChoices.size() > 1 || iosSlot != 0;

	bool netOn = riftwii::wii::NetworkPacksEnabled();
	enum RowAction { kIos, kNet, kResync, kRescan, kExit, kNone };
	std::vector<FlowRow> rows;
	std::vector<RowAction> actions;
	const auto build = [&]() {
		rows.clear();
		actions.clear();
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
		FlowRow net;
		net.kind = FlowRow::Kind::Option;
		net.label = "Find network packs (RiiFS)";
		net.value = netOn ? "On" : "Off";
		net.on = netOn;
		rows.push_back(net);
		actions.push_back(kNet);
		FlowRow resync;
		resync.kind = FlowRow::Kind::Action;
		resync.label = "Copy network packs again";
		resync.value = "Resync";
		rows.push_back(resync);
		actions.push_back(kResync);
		FlowRow rescan;
		rescan.kind = FlowRow::Kind::Action;
		rescan.label = "Look for games again";
		rescan.value = "Rescan";
		rows.push_back(rescan);
		actions.push_back(kRescan);
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
	GuiText versionTxt("RiftWii " RIFTWII_VERSION, 15, skin::kInkDim);
	versionTxt.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	versionTxt.SetPosition(-40, 40);
	Panel panel(skin::panelSettings, 34, 84);
	GuiFlowList list(46, 94, 548, 5);
	list.SetRows(&rows);
	list.Select(0);
	GuiText noteTxt(MenuIosNote(iosSlot).c_str(), 16, skin::kInkSoft);
	Place(noteTxt, 52, 280);
	noteTxt.SetWrap(true, 536);

	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&versionTxt);
	w.Append(&panel);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
	mainWindow->Append(&w);
	ResumeGui();

	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button});
		int acted = list.GetClicked();
		int direction = +1;
		if (acted < 0) {
			acted = list.GetClickedBack();
			direction = -1;
		}
		if (acted >= 0 && static_cast<std::size_t>(acted) < actions.size()) {
			switch (actions[static_cast<std::size_t>(acted)]) {
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
				case kExit:
					menu = MENU_EXIT;
					break;
				default:
					break;
			}
		}
		if (menu == MENU_NONE && backBtn.Clicked())
			menu = MENU_SOURCE;
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

// ---------------------------------------------------------------------------
// Launch frame: stays on screen while the boot log prints into its card.

static void ShowLaunchFrame(const FrontendState& state, int action)
{
	const std::string title = GameTitle(state);
	const char* doing = action == MENU_DUMP ? "Dumping files from" : "Starting";
	GuiText doingTxt(doing, 16, skin::kInkDim);
	Place(doingTxt, 40, 40);
	GuiText titleTxt(title.c_str(), 28, skin::kInk);
	Place(titleTxt, 40, 62);
	titleTxt.SetWrap(true, 560);
	Panel card(skin::panelSettings, 34, 160);
	GuiText footTxt("The game takes over the screen when it is ready.", 15, skin::kInkDim);
	Place(footTxt, 0, 428, true);

	HaltGui();
	hidePointers = true;
	GuiWindow w(screenwidth, screenheight);
	w.Append(&doingTxt);
	w.Append(&titleTxt);
	w.Append(&card);
	w.Append(&footTxt);
	mainWindow->Append(&w);
	ResumeGui();
	usleep(100000);  // a few frames, so both framebuffers show it
	HaltGui();
	mainWindow->Remove(&w);
}

int MainMenu(int menu, FrontendState& state)
{
	int currentMenu = menu;

	skin::Init();
	soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	mainWindow = new GuiWindow(screenwidth, screenheight);
	backdrop = new skin::GuiBackdrop();
	mainWindow->Append(backdrop);
	riftwii::wii::GuiScriptLoad("sd:/riftwii/guiscript.txt");

	ResumeGui();

	while(currentMenu != MENU_EXIT && currentMenu != MENU_LAUNCH && currentMenu != MENU_BOOT && currentMenu != MENU_DUMP)
	{
		logf("Screen: %s\n", ScreenName(currentMenu));
		switch (currentMenu)
		{
			case MENU_OPTIONS:
				currentMenu = MenuSettings(state);
				break;
			case MENU_HOME:
				currentMenu = MenuHome(state);
				break;
			case MENU_SOURCE:
			default:
				currentMenu = MenuSource(state);
				break;
		}
	}

	logf("Screen: %s\n", ScreenName(currentMenu));
	if (currentMenu == MENU_LAUNCH || currentMenu == MENU_BOOT || currentMenu == MENU_DUMP) {
		// The GUI thread is halted after this; main prints the boot log
		// into the card of the frame left on screen.
		ShowLaunchFrame(state, currentMenu);
		return currentMenu;
	}

	ExitRequested = 1;
	ResumeGui();
	while(1) usleep(THREAD_SLEEP);
	return MENU_EXIT;
}
