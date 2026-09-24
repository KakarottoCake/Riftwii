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
#include <atomic>
#include <ctime>
#include <fstream>
#include <initializer_list>
#include <set>
#include <sstream>
#include <wiiuse/wpad.h>

#include "libwiigui/gui.h"
#include "gui_flowlist.hpp"
#include "gui_gamegrid.hpp"
#include "guiscript.hpp"
#include "gcadapter.hpp"
#include "skin.hpp"
#include "wiidrc.h"
#include "menu.h"
#include "autorun.hpp"
#include "demo.h"
#include "input.h"
#include "riftwii/patch.hpp"
#include "gameextras.hpp"
#include "i18n.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "menuios.hpp"
#include "online.hpp"
#include "restart.hpp"
#include "riftwii/settingsfile.hpp"
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
		return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
	});
	if (g_filter == Filter::Recent) {
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
		if (g_filter == Filter::Mods && !mods) continue;
		if (g_filter == Filter::Favorites && riftwii::wii::Settings().favorites.count(r.game->id) == 0) continue;
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

static std::string g_homeNotice;

void SetHomeNotice(const std::string& text) { g_homeNotice = text; }

static std::string HomeStatus(const FrontendState& state, std::size_t shown)
{
	if (!g_homeNotice.empty()) return g_homeNotice;
	std::string status;
	for (const std::string& p : {ShortSourceProblem("SD", state.sd_catalog), ShortSourceProblem("USB", state.usb_catalog)}) {
		if (p.empty()) continue;
		status += (status.empty() ? "" : "   ") + p;
	}
	if (!state.usb_catalog.cios_note.empty() || !state.sd_catalog.cios_note.empty())
		status += std::string(status.empty() ? "" : "   ") + "No d2x cIOS in 249-251: games cannot boot yet";
	if (!status.empty()) return status;
	if (g_filter == Filter::Mods && shown <= 1) return "No game here has packs in sd:/riivolution yet. Press 1 for all games.";
	if (g_filter == Filter::Recent && shown <= 1) return "No game on these drives was played from RiftWii yet. Press 1 for all games.";
	if (g_filter == Filter::Favorites && shown <= 1)
		return tr("No favourite is on these drives. Mark games on their page. Press 1 for all games.");
	if (shown <= 1) return "No games found (usb:/wbfs, usb:/games, sd:/wbfs, sd:/games)";
	return std::string(tr(FilterLabel(g_filter))) + "   " + tr("1: view   2: settings   -: A to Z   +: rescan");
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
		state.sd_catalog.status = "SD: " + (error.empty() ? std::string(tr("scan failed")) : error);
	}
	error.clear();
	status.SetText("Reading the USB drive... (a big drive takes a moment)");
	ResumeGui();
	const bool usb = scan_usb_games(state.usb_catalog, error);
	HaltGui();
	if (!usb) {
		logf("USB scan failed: %s\n", error.c_str());
		state.usb_catalog.status = "USB: " + (error.empty() ? std::string(tr("scan failed")) : error);
		if (riftwii::wii::MenuCiosSlot() != 0 && error.find("more than one") == std::string::npos) {
			state.usb_catalog.status += " " + tr("(the menu runs under IOS {1}; USB drives need a base-58 cIOS for that, or set the menu IOS back to 58)",
				{std::to_string(riftwii::wii::MenuCiosSlot())});
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
	// {1} the 12-hour hour, {2} the minutes, {3} the 24-hour hour.
	snprintf(buf, sizeof(buf), "%02d", local.tm_min);
	clock = tr(local.tm_hour < 12 ? "{1}:{2} AM" : "{1}:{2} PM",
		   {std::to_string(h), buf, std::to_string(local.tm_hour)});
	date = tr("{1} {2}/{3}", {tr(days[local.tm_wday % 7]), std::to_string(local.tm_mon + 1), std::to_string(local.tm_mday)});
}

// A to Z: the first game (in the view's order) whose name starts with
// the next letter after the focused one's, wrapping round. Names that
// start with anything else come before A.
static int NextLetter(const std::vector<GridItem>& items, int focused)
{
	const auto letter = [&](std::size_t i) -> int {
		const char c = items[i].title.empty() ? 0 : items[i].title[0];
		if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
		return c >= 'A' && c <= 'Z' ? c : '#';
	};
	// The disc tile (0) has no letter: from it, the first letter comes next.
	const int from = focused > 0 && static_cast<std::size_t>(focused) < items.size()
		? letter(static_cast<std::size_t>(focused)) : 0;
	int best = -1, first = -1;
	for (std::size_t i = 1; i < items.size(); ++i) {
		const int l = letter(i);
		if (l > from && (best < 0 || l < letter(static_cast<std::size_t>(best)))) best = static_cast<int>(i);
		if (first < 0 || l < letter(static_cast<std::size_t>(first))) first = static_cast<int>(i);
	}
	return best >= 0 ? best : first;
}

static int MenuSource(FrontendState& state)
{
	int menu = MENU_NONE;
	LoadFilter();
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
	statusTxt.SetWrap(true, 400, 3);

	SkinButton filterBtn(skin::roundBtn, skin::roundBtnOver, 2, 26, 386, nullptr,
		WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y, WIIDRC_BUTTON_X, &skin::iconDrives);
	SkinButton settingsBtn(skin::roundBtn, skin::roundBtnOver, 2, 538, 386, nullptr,
		WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_X, PAD_TRIGGER_R, WIIDRC_BUTTON_Y, &skin::iconGear);
	GuiTrigger trigRescan, trigExit, trigJump;
	trigRescan.SetButtonOnlyTrigger(-1, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_X, WIIDRC_BUTTON_PLUS);
	trigExit.SetButtonOnlyTrigger(-1, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, PAD_BUTTON_START, WIIDRC_BUTTON_HOME);
	trigJump.SetButtonOnlyTrigger(-1, WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, PAD_TRIGGER_L, WIIDRC_BUTTON_MINUS);
	GuiButton rescanBtn(0, 0), exitBtn(0, 0), jumpBtn(0, 0);  // hotkeys only
	rescanBtn.SetTrigger(&trigRescan);
	exitBtn.SetTrigger(&trigExit);
	jumpBtn.SetTrigger(&trigJump);

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
	w.Append(&jumpBtn);
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
			const std::string page = shownPages > 1 ? tr("Page {1} of {2}", {std::to_string(shownPage + 1), std::to_string(shownPages)}) : "";
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
		} else if (jumpBtn.GetState() == STATE::CLICKED) {
			jumpBtn.ResetState();
			const int to = NextLetter(items, grid.FocusedIndex());
			if (to >= 0) grid.Focus(to);
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
	static constexpr int kHeight = 136;
	explicit GameBanner(GXColor hue) : hue(hue) {}
	void Draw() override {
		Menu_DrawRectangle(0, 0, screenwidth, kHeight, hue, 1);
		GX_SetScissor(0, 0, Menu_XfbWidth(), kHeight * Menu_EfbHeight() / 480);
		skin::Draw(skin::bannerStripes, 0, -8);
		GX_SetScissor(0, 0, Menu_XfbWidth(), Menu_EfbHeight());
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
	enum class What { Mods, Saves, Cheats, Width, Deflicker, Borders, VideoMode, Language, Cios, Favorite, Pack, Option, Note } what = What::Note;
	std::size_t pkg = 0, opt = 0;
};

// The game's video settings: "global" follows Settings, the rest are
// riftwii/videopatch.hpp's names.
static const char* const kWidths[] = {"global", "game", "framebuffer", "704", "720"};
static const char* const kDeflickers[] = {"global", "game", "off", "low", "medium", "high"};
static const char* const kBorderModes[] = {"global", "keep", "remove"};
static const char* const kVideoModes[] = {"global", "game", "system", "ntsc", "pal60", "pal50", "480p"};
static const char* const kGameLanguages[] = {"global", "console", "ja", "en", "de", "fr", "es", "it", "nl", "zh-hans",
	"zh-hant", "ko"};
static const char* const kCiosChoices[] = {"global", "auto", "248", "249", "250", "251", "252"};

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
static std::string BordersName(const std::string& v)
{
	return v == "remove" ? tr("Remove") : tr("Keep");
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
	const auto add = [&](FlowRow row, RowRef ref) {
		rows.push_back(std::move(row));
		refs.push_back(ref);
	};
	// Mods first: the reason most games are opened here.
	std::size_t enabled = 0;
	const std::size_t shown = ShownPacks(state, &enabled);
	FlowRow mods;
	mods.kind = FlowRow::Kind::Action;
	mods.label = tr("Mods");
	mods.value = shown == 0 ? tr("None") : enabled == 0 ? tr("Off") : tr("{1} on", {std::to_string(enabled)});
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
	borders.value = GameValue(game.borders, global.borders, BordersName);
	borders.on = game.borders != "global";
	add(borders, {RowRef::What::Borders});
	FlowRow videoMode;
	videoMode.kind = FlowRow::Kind::Option;
	videoMode.label = tr("Video mode");
	videoMode.value = GameValue(game.video_mode, global.video_mode, VideoModeName);
	videoMode.on = game.video_mode != "global";
	add(videoMode, {RowRef::What::VideoMode});
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
	FlowRow favorite;
	favorite.kind = FlowRow::Kind::Toggle;
	favorite.label = tr("Favourite");
	favorite.on = global.favorites.count(state.game_id) != 0;
	favorite.value = favorite.on ? tr("On") : tr("Off");
	add(favorite, {RowRef::What::Favorite});
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
			: tr("{1} XML file(s) are for other games", {std::to_string(state.model.packages.size())});
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
				say(note.empty() ? c.name : FlatCapped(note, 200));
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
	if (names.empty()) return tr("{1} mod pack(s) for this game. Press A to turn them on.", {std::to_string(shown)});
	return FlatCapped(tr("On: {1}", {names}), 90);
}

// ---------------------------------------------------------------------------
// Mods: the packs made for the game, opened from the game page.

static void MenuMods(FrontendState& state, const std::string& scanStatus)
{
	std::vector<FlowRow> rows;
	std::vector<RowRef> refs;
	BuildModRows(state, scanStatus, rows, refs);

	GuiText titleTxt(tr("Mods"), 30, skin::kInk);
	Place(titleTxt, 40, 28);
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
	SkinButton backBtn(skin::pill, skin::pillOver, 4, 198, 406, "Back",
		WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&gameTxt);
	w.Append(&panel);
	w.Append(&list);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
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
			if (ref.what == RowRef::What::Pack) say(PackSummary(state.model.packages[ref.pkg]));
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
			if (ref.what == RowRef::What::Pack) {
				riftwii::LaunchPackage& p = state.model.packages[ref.pkg];
				if (!p.valid) say("This XML cannot be read; fix it on the card and come back.");
				else changed = state.model.set_enabled(ref.pkg, !p.enabled);
				if (!changed && p.valid) say("This pack cannot be turned on.");
			} else if (ref.what == RowRef::What::Option) {
				changed = state.model.cycle(ref.pkg, ref.opt, direction);
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
	BuildGameRows(state, rows, refs);

	GameBanner banner(skin::HueFor(state.game_id));
	const std::string where = SourceWhere(state);
	GuiText whereTxt(where.c_str(), 16, skin::WithAlpha(skin::kWhite, 200));
	Place(whereTxt, 40, 16);
	const std::string title = GameTitle(state);
	GuiText titleTxt(title.c_str(), 28, skin::kWhite);
	Place(titleTxt, 40, 38);
	titleTxt.SetWrap(true, 560, 2);
	// The ID, and how often the game was played from RiftWii.
	const std::string played = riftwii::wii::PlayNote(state.game_id);
	const std::string idLine = played.empty() ? state.game_id : state.game_id + "   " + played;
	GuiText idTxt(idLine.c_str(), 16, skin::WithAlpha(skin::kWhite, 200));
	Place(idTxt, 40, 108);

	Panel panel(skin::panelGame, 34, 144);
	GuiFlowList list(46, 150, 548, 5);
	list.SetRows(&rows);
	list.Select(0);

	GuiText statusTxt(ModsNote(state, scanStatus).c_str(), 15, skin::kInkSoft);
	// Two lines between the card and the buttons: a cIOS remedy or a
	// compile error must stay readable in full.
	Place(statusTxt, 0, 368, true);
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
				const std::string how = tr("Remove stretches the picture to fill the screen.");
				say(seen.empty() ? how : seen + " " + how);
			}
			else if (ref.what == RowRef::What::VideoMode)
				say(tr("The TV signal the game sends. PAL 50 Hz needs a TV that takes it, 480p a component cable."));
			else if (ref.what == RowRef::What::Language)
				say(tr("The language the game is told the console uses. Pick one the game has: some games stop without it."));
			else if (ref.what == RowRef::What::Favorite)
				say(tr("Favourites have their own view on Home: press 1 there until it shows."));
			else if (ref.what == RowRef::What::Cios)
				say(tr("The d2x cIOS the game runs under. Automatic uses the menu's, else the first of 249, 250 and 251 that works."));
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
				mainWindow->Remove(&w);
				if (ref.what == RowRef::What::Mods) MenuMods(state, scanStatus);
				else MenuCheats(state);
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
			} else if (ref.what == RowRef::What::Language) {
				state.model.game.language = StepValue(kGameLanguages, state.model.game.language, direction);
				changed = true;
			} else if (ref.what == RowRef::What::Cios) {
				state.model.game.cios = StepValue(kCiosChoices, state.model.game.cios, direction);
				changed = true;
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
	// Cheats and video settings go with every launch, mods or not.
	if (menu == MENU_LAUNCH || menu == MENU_BOOT) {
		riftwii::wii::PrepareLaunchExtras(state);
		riftwii::wii::RecordPlay(state.game_id);
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

static std::string AdapterStatus(const riftwii::wii::GcAdapterView& v)
{
	switch (v.link) {
		case GCAD_LINK_POLL: return tr("Adapter: working");
		case GCAD_LINK_SETUP:
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
	w.Append(&titleTxt);
	w.Append(&panel);
	w.Append(&statusTxt);
	w.Append(&hidTxt);
	for (auto& t : portTxt) w.Append(&t);
	w.Append(&noteTxt);
	w.Append(&backBtn.button);
	mainWindow->Append(&w);
	std::string why;
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
			std::snprintf(hid, sizeof(hid), "USB HID v%u on IOS%d, %u USB device(s), %u reports", v.version,
				IOS_GetVersion(), v.listed, v.reports);
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
	if (started) riftwii::wii::GcAdapterStop();
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
	enum RowAction { kLanguage, kWidth, kDeflicker, kBorders, kVideoMode, kGameLanguage, kGameCios, kOnline, kNames, kGcAdapter, kGcTest, kIos, kNet, kResync,
		kRescan, kExit, kNone };
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
		option(tr("Language"), LanguageName(settings.language), settings.language != "auto", kLanguage);
		option(tr("Picture width"), WidthName(settings.video_width), settings.video_width != "game", kWidth);
		option(tr("Deflicker"), DeflickerName(settings.deflicker), settings.deflicker != "game", kDeflicker);
		option(tr("Black borders"), BordersName(settings.borders), settings.borders == "remove", kBorders);
		option(tr("Video mode"), VideoModeName(settings.video_mode), settings.video_mode != "game", kVideoMode);
		option(tr("Game language"), GameLanguageName(settings.game_language), settings.game_language != "console",
			kGameLanguage);
		option("Game cIOS", CiosName(settings.game_cios), settings.game_cios != "auto", kGameCios);
		option(tr("Download names and cheats"), settings.online ? tr("On") : tr("Off"), settings.online, kOnline,
			FlowRow::Kind::Toggle);
		FlowRow names;
		names.kind = FlowRow::Kind::Action;
		names.label = tr("Get the latest game names");
		names.value = tr("Update");
		names.dim = !settings.online;
		rows.push_back(names);
		actions.push_back(kNames);
		option(tr("GameCube adapter"), settings.gc_adapter == "demo" ? std::string("Demo")
			: settings.gc_adapter == "on" ? tr("On") : tr("Off"), settings.gc_adapter != "off", kGcAdapter,
			FlowRow::Kind::Toggle);
		FlowRow gcTest;
		gcTest.kind = FlowRow::Kind::Action;
		gcTest.label = tr("Check the GameCube adapter");
		gcTest.value = tr("Test");
		rows.push_back(gcTest);
		actions.push_back(kGcTest);
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
	list.Select(0);
	GuiText noteTxt(tr("These apply to every game. A game's own page can change them for that game."), 16, skin::kInkSoft);
	Place(noteTxt, 52, 312);
	noteTxt.SetWrap(true, 536, 4);

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

	// What each row does, shown when it takes the focus (the first row
	// keeps the page's own note until the focus moves).
	const auto help = [&](RowAction action) -> std::string {
		switch (action) {
			case kLanguage: return tr("The menu's language. Wii follows the console's own setting.");
			case kWidth: return tr("How wide the picture is drawn. 720 fills the screen from side to side.");
			case kDeflicker: return tr("A filter that softens the picture to hide flicker. Off gives the sharpest picture.");
			case kBorders: return tr("Remove stretches the picture to fill the screen.");
			case kVideoMode: return tr("The TV signal the game sends. PAL 50 Hz needs a TV that takes it, 480p a component cable.");
			case kGameLanguage: return tr("The language the game is told the console uses. Pick one the game has: some games stop without it.");
			case kGameCios: return tr("The d2x cIOS the game runs under. Automatic uses the menu's, else the first of 249, 250 and 251 that works.");
			case kOnline:
				return settings.online ? tr("Game names and cheats are downloaded when the Wii is online.")
					: tr("Nothing is downloaded. Names and cheats already on the card are still used.");
			case kNames: return tr("Downloads the newest game names from GameTDB.");
			case kGcAdapter:
				return tr("In games that support the GameCube controller, the adapter's controllers fill the ports that have none plugged in. It needs IOS 58 or a d2x cIOS.");
			case kGcTest: return tr("Shows live what the controllers in the adapter are pressing.");
			case kIos: return MenuIosNote(iosSlot);
			case kNet:
				return netOn ? "Looks for a PC running a RiiFS server when the games are read. Rescan to look now."
					: "Only servers named by <network> in an XML on the card are used.";
			case kResync: return "The next launch copies every file of its network packs again.";
			case kRescan: return tr("Reads the SD card and the USB drive again.");
			case kExit: return tr("Back to the Homebrew Channel.");
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
					saveAndNote(tr("Remove stretches the picture to fill the screen."));
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
				case kGcAdapter:
					settings.gc_adapter = settings.gc_adapter == "off" ? "on" : "off";
					saveAndNote(settings.gc_adapter == "on"
						? tr("In games that support the GameCube controller, the adapter's controllers fill the ports that have none plugged in. It needs IOS 58 or a d2x cIOS.")
						: tr("The adapter is left alone."));
					rebuild();
					break;
				case kGcTest:
					mainWindow->Remove(&w);
					ResumeGui();
					GcAdapterTestPage();
					HaltGui();
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
				case kExit:
					menu = MENU_EXIT;
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
// Launch frame: stays on screen while the boot log prints into its card.

static void ShowLaunchFrame(const FrontendState& state, int action)
{
	const std::string title = GameTitle(state);
	const char* doing = action == MENU_DUMP ? "Dumping files from" : "Starting";
	GuiText doingTxt(doing, 16, skin::kInkDim);
	Place(doingTxt, 40, 40);
	GuiText titleTxt(title.c_str(), 28, skin::kInk);
	Place(titleTxt, 40, 62);
	titleTxt.SetWrap(true, 560, 2);
	Panel card(skin::panelSettings, 34, 160);
	GuiText footTxt("The game takes over the screen when it is ready.", 15, skin::kInkDim);
	Place(footTxt, 0, 444, true);

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
	riftwii::wii::GuiScriptLoad(riftwii::wii::CurrentRestartNote().kind == riftwii::wii::RestartKind::None
	                                ? "sd:/riftwii/guiscript.txt"
	                                : "sd:/riftwii/guiscript-restart.txt");

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
				g_homeNotice.clear();
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
