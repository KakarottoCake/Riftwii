// SPDX-License-Identifier: GPL-3.0-or-later
/****************************************************************************
 * Riftwii
 *
 * rift_menu.cpp
 * The frontend: a source screen (SD, USB or DISC), an image games picker, a mods
 * screen listing the packages made for the picked game (broken XML stays
 * visible with its error), an options screen setting each option's
 * choice, and Launch handing the selection to the boot. A per-game Saves
 * toggle keeps modded saves in an SD folder. The state itself
 * (riftwii/launch.hpp) is host-tested; this file only lists the directory,
 * draws and reads the pads. Built on the vendored libwiigui template.
 ***************************************************************************/

#include <gccore.h>
#include <ogcsys.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <array>
#include <initializer_list>
#include <wiiuse/wpad.h>

#include "libwiigui/gui.h"
#include "wiidrc.h"
#include "menu.h"
#include "autorun.hpp"
#include "demo.h"
#include "input.h"
#include "riftwii/patch.hpp"

#define THREAD_SLEEP 100

using riftwii::wii::ScanPackages;
using riftwii::wii::SaveChoices;
using riftwii::wii::CompileSelection;
using riftwii::wii::SelectDisc;
using riftwii::wii::SelectSdGame;
using riftwii::wii::SelectUsbGame;
using riftwii::wii::scan_sd_games;
using riftwii::wii::scan_usb_games;

static const char* PackageValue(const riftwii::LaunchPackage& p)
{
	if (!p.valid) return "Invalid";
	if (!p.for_disc) return "Other disc";
	return p.enabled ? "On" : "Off";
}

// A 32-character hard chunk stays within the 560px text width even for a
// run of wide glyphs. Home has room for one line only; Mod Options has room
// for three lines because it has just one row of bottom buttons.
constexpr std::size_t kDetailCharsPerLine = 32;
constexpr std::size_t kDetailLinesPerPage = 3;

struct DetailPage {
	std::array<std::string, kDetailLinesPerPage> rows;
};

struct DetailPages {
	std::vector<DetailPage> pages;
};

// Hard-wrap instead of relying on GuiText word wrapping: an XML attribute
// name can be thousands of characters with no whitespace. Parser details are
// otherwise copied byte-for-byte.
static std::vector<std::string> MakeDetailLines(const std::string& detail)
{
	std::vector<std::string> lines;
	std::string line;
	const auto finish_line = [&]() {
		lines.push_back(line);
		line.clear();
	};
	for (char c : detail) {
		if (c == '\r') continue;
		if (c == '\n') {
			finish_line();
			continue;
		}
		line.push_back(c);
		if (line.size() == kDetailCharsPerLine) finish_line();
	}
	if (!line.empty() || lines.empty()) finish_line();
	return lines;
}

// Each page is one header and two diagnostic lines, so it cannot grow into
// the button rows.
static DetailPages MakeDetailPages(const std::string& detail)
{
	const std::vector<std::string> lines = MakeDetailLines(detail);
	constexpr std::size_t kBodyLines = kDetailLinesPerPage - 1;
	const std::size_t count = (lines.size() + kBodyLines - 1) / kBodyLines;
	DetailPages pages;
	for (std::size_t page = 0; page < count; ++page) {
		DetailPage detail;
		detail.rows[0] = "Details " + std::to_string(page + 1) + "/" + std::to_string(count);
		const std::size_t first = page * kBodyLines;
		for (std::size_t row = 0; row < kBodyLines; ++row) {
			if (first + row < lines.size()) detail.rows[row + 1] = lines[first + row];
		}
		pages.pages.push_back(std::move(detail));
	}
	return pages;
}

// Home status does not have a page control. Keep it to one text row below
// the browser; the selected package's complete diagnostics are in Mod
// Options. The explicit marker explains where a truncated status continues.
static std::string BoundedHomeDetail(const std::string& detail)
{
	const std::vector<std::string> lines = MakeDetailLines(detail);
	if (lines.size() == 1) return lines.front();
	static const std::string marker = " +: details";
	return lines.front().substr(0, kDetailCharsPerLine - marker.size()) + marker;
}

static void SetDetailRows(const std::array<GuiText*, kDetailLinesPerPage>& rows, const DetailPage& detail)
{
	for (std::size_t row = 0; row < kDetailLinesPerPage; ++row)
		rows[row]->SetText(detail.rows[row].c_str());
}

static void SetBoundedHomeDetail(GuiText& text, const std::string& detail)
{
	text.SetText(BoundedHomeDetail(detail).c_str());
}

static bool AllOptionsOff(const riftwii::LaunchPackage& p)
{
	if (p.package.options.empty()) return false;
	for (const riftwii::Option& option : p.package.options) {
		if (option.selected != 0 && option.selected <= option.choices.size()) return false;
	}
	return true;
}

static std::string ToggleDetail(const riftwii::LaunchPackage& p, bool enabled, bool simple_auto_selected)
{
	if (!enabled)
		return "Disabled. A enables; + details";
	if (simple_auto_selected) {
		const riftwii::Option& option = p.package.options.front();
		const std::string choice = option.choices.front().name;
		return "Enabled: '" + choice.substr(0, 10) + "' selected";
	}
	if (AllOptionsOff(p))
		return "Enabled; no choice. + options";
	if (p.package.options.empty())
		return "Enabled; no options. + details";
	return "Enabled. + options/details";
}

static GuiImageData * pointer[4];
static GuiImage * bgImg = nullptr;
static GuiWindow * mainWindow = nullptr;
static lwp_t guithread = LWP_THREAD_NULL;
static std::atomic<bool> guiHalt{true};
static int selectedPackage = 0;  // the home screen's highlighted row, kept across screens
static riftwii::wii::ImageDevice pickerDevice = riftwii::wii::ImageDevice::Usb;
static int selectedGame = 0;  // the image picker row, reset whenever its source changes
constexpr int kOptionCapacity = 150;

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
			mainWindow->Draw();

			for(i = 3; i >= 0; i--)
			{
				if(userInput[i].wpad->ir.valid)
					Menu_DrawImg(userInput[i].wpad->ir.x-48, userInput[i].wpad->ir.y-48,
						96, 96, pointer[i]->GetImage(), userInput[i].wpad->ir.angle, 1, 1, 255);
				DoRumble(i);
			}

			Menu_Render();

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

// A plain button with the template's look, triggered by A and by `extra`.
// The Wii U GamePad mirrors the Wii names (A/B/Plus/Minus/Home/D-pad) and
// stands in for 1 with X; every bottom button carries a GamePad hotkey so
// the menus are fully drivable without a pointer.
struct MenuButton {
	GuiText text;
	GuiImage image;
	GuiImage imageOver;
	GuiTrigger trigA;
	GuiTrigger trigExtra;
	GuiButton button;
	MenuButton(const char* label, GuiImageData& outline, GuiImageData& outlineOver, GuiSound& sound,
		   u32 wpadExtra, u16 padExtra, u16 wiidrcExtra = 0)
		: text(label, 22, (GXColor){0, 0, 0, 255}), image(&outline), imageOver(&outlineOver),
		  button(outline.GetWidth(), outline.GetHeight())
	{
		trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, WIIDRC_BUTTON_A);
		trigExtra.SetButtonOnlyTrigger(-1, wpadExtra, padExtra, wiidrcExtra);
		button.SetLabel(&text);
		button.SetImage(&image);
		button.SetImageOver(&imageOver);
		button.SetSoundOver(&sound);
		button.SetTrigger(&trigA);
		if (wpadExtra || padExtra || wiidrcExtra) button.SetTrigger(&trigExtra);
		button.SetEffectGrow();
	}
	void Place(ALIGN_H h, ALIGN_V v, int x, int y)
	{
		button.SetAlignment(h, v);
		button.SetPosition(x, y);
	}
	bool Clicked() { return button.GetState() == STATE::CLICKED; }
};

// libwiigui only clears a hover-selected button while the pointer is live:
// with an invalid pointer a stale SELECTED survives, and the next A press
// fires both it and the browser row. Drop stale bottom-button selection
// when no pointer is live; hotkeys (BUTTON_ONLY) and fresh hovers are
// unaffected, and browser rows manage their own focus.
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

// Short image-catalog status for the source screen (counts only): the
// full sentences never fit between the title and the buttons.
static std::string CountLine(const char* tag, const riftwii::wii::ImageCatalog& catalog)
{
	const std::string prefix = std::string(tag) + ": ";
	if (!catalog.games.empty()) {
		return prefix + std::to_string(catalog.games.size()) +
			(catalog.games.size() == 1 ? " game" : " games");
	}
	if (catalog.status.empty() || catalog.status == prefix + "select to scan") return prefix + "select";
	if (catalog.status.compare(0, 8, "No valid") == 0) return prefix + "no games";
	return prefix + "unavailable";
}

static void AppendCatalogDetail(std::string& detail, const char* tag,
							const riftwii::wii::ImageCatalog& catalog)
{
	const std::string initial = std::string(tag) + ": select to scan";
	if (catalog.games.empty() && !catalog.status.empty() && catalog.status != initial) {
		if (!detail.empty()) detail += "\n";
		detail += catalog.status;
	}
	if (!catalog.cios_note.empty()) {
		if (!detail.empty()) detail += "\n";
		detail += catalog.cios_note;
	}
}

static std::string SourceDetail(const riftwii::wii::FrontendState& state)
{
	std::string detail;
	AppendCatalogDetail(detail, "SD", state.sd_catalog);
	AppendCatalogDetail(detail, "USB", state.usb_catalog);
	if (!detail.empty()) detail += "\n";
	detail += "1/Y: SD (X on GamePad)    +/X: USB    -/Z: DISC    (or point and press A)";
	return detail;
}

// Two lines, so the buttons below stay clear: the saves mode plus either
// the hotkeys (clean scan) or the scan problem (which matters more).
static std::string HomeDetail(const FrontendState& state, const std::string& scanStatus)
{
	const std::string save = state.model.save_mode == "separate" ? "Save:Sep" :
		state.model.save_mode == "fresh" ? "Save:Fresh" : "Save:NAND";
	if (scanStatus == riftwii::wii::kScanReady)
		return save + " A:toggle +:options";
	return scanStatus;
}

// Start screen. Image selection happens while IOS58 owns the storage; the
// selected source is activated only after the GUI has been torn down.
static int MenuSource(FrontendState& state)
{
	int menu = MENU_NONE;

	GuiText titleTxt("Riftwii", 34, (GXColor){255, 255, 255, 255});
	titleTxt.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	titleTxt.SetPosition(0, 30);

	GuiText subTxt("Choose game source", 18, (GXColor){200, 200, 200, 255});
	subTxt.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	subTxt.SetPosition(0, 76);

	GuiText discTxt(state.disc_status.c_str(), 18, (GXColor){200, 200, 200, 255});
	discTxt.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	discTxt.SetPosition(0, 102);

	std::string sourceLine = CountLine("SD", state.sd_catalog) + "      " + CountLine("USB", state.usb_catalog);
	GuiText sdusbTxt(sourceLine.c_str(),
			 18, (GXColor){200, 200, 200, 255});
	sdusbTxt.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	sdusbTxt.SetPosition(0, 126);

	std::string sourceDetail = SourceDetail(state);
	GuiText detailTxt(sourceDetail.c_str(), 16, (GXColor){255, 255, 255, 255});
	detailTxt.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::BOTTOM);
	detailTxt.SetPosition(0, -100);
	detailTxt.SetWrap(true, screenwidth - 80);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);

	MenuButton sdBtn("SD", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y, WIIDRC_BUTTON_X);
	sdBtn.Place(ALIGN_H::CENTRE, ALIGN_V::MIDDLE, 0, -44);
	MenuButton usbBtn("USB", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_X, WIIDRC_BUTTON_PLUS);
	usbBtn.Place(ALIGN_H::CENTRE, ALIGN_V::MIDDLE, 0, 24);
	MenuButton discBtn("DISC", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, PAD_TRIGGER_Z, WIIDRC_BUTTON_MINUS);
	discBtn.Place(ALIGN_H::CENTRE, ALIGN_V::MIDDLE, 0, 92);
	MenuButton exitBtn("Exit", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, 0, WIIDRC_BUTTON_HOME);
	exitBtn.Place(ALIGN_H::LEFT, ALIGN_V::BOTTOM, 40, -35);
	for (MenuButton* b : {&sdBtn, &usbBtn, &discBtn, &exitBtn}) b->button.SetScale(1.0f);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&subTxt);
	w.Append(&discTxt);
	w.Append(&sdusbTxt);
	w.Append(&detailTxt);
	w.Append(&sdBtn.button);
	w.Append(&usbBtn.button);
	w.Append(&discBtn.button);
	w.Append(&exitBtn.button);
	mainWindow->Append(&w);
	ResumeGui();

	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&exitBtn.button, &sdBtn.button, &usbBtn.button, &discBtn.button});
		if(exitBtn.Clicked())
			menu = MENU_EXIT;
		else if(sdBtn.Clicked()) {
			sdBtn.button.ResetState();
			detailTxt.SetText("Scanning SD...");
			ResumeGui();
			std::string error;
			const bool scanned = scan_sd_games(state.sd_catalog, error);
			HaltGui();
			if (!scanned) {
				state.sd_catalog.status = "SD: " + (error.empty() ? "scan failed" : error);
				sourceLine = CountLine("SD", state.sd_catalog) + "      " + CountLine("USB", state.usb_catalog);
				sdusbTxt.SetText(sourceLine.c_str());
				detailTxt.SetText(error.empty() ? "SD scan failed" : error.c_str());
			} else {
				pickerDevice = riftwii::wii::ImageDevice::Sd;
				selectedGame = 0;
				menu = MENU_GAMES;
			}
		}
		else if(usbBtn.Clicked()) {
			usbBtn.button.ResetState();
			detailTxt.SetText("Scanning USB...");
			ResumeGui();
			std::string error;
			const bool scanned = scan_usb_games(state.usb_catalog, error);
			HaltGui();
			if (!scanned) {
				state.usb_catalog.status = "USB: " + (error.empty() ? "scan failed" : error);
				sourceLine = CountLine("SD", state.sd_catalog) + "      " + CountLine("USB", state.usb_catalog);
				sdusbTxt.SetText(sourceLine.c_str());
				detailTxt.SetText(error.empty() ? "USB scan failed" : error.c_str());
			} else {
				pickerDevice = riftwii::wii::ImageDevice::Usb;
				selectedGame = 0;
				menu = MENU_GAMES;
			}
		}
		else if(discBtn.Clicked()) {
			discBtn.button.ResetState();
			detailTxt.SetText("Probing disc...");
			ResumeGui();
			std::string error;
			const bool selected = SelectDisc(state, error);
			HaltGui();
			if (!selected) {
				detailTxt.SetText(error.empty() ? "No disc in drive" : error.c_str());
			} else {
				discTxt.SetText(state.disc_status.c_str());
				menu = MENU_HOME;
			}
		}
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

static int MenuGames(FrontendState& state)
{
	int menu = MENU_NONE;

	static OptionList options;
	memset(&options, 0, sizeof(options));
	const bool sd = pickerDevice == riftwii::wii::ImageDevice::Sd;
	const auto& catalog = sd ? state.sd_catalog : state.usb_catalog;
	const auto fill = [&]() {
		options.length = 0;
		for (const auto& g : catalog.games) {
			if (options.length >= kOptionCapacity) break;
			const int i = options.length++;
			snprintf(options.name[i], sizeof(options.name[i]), "%.6s  %.40s", g.id.c_str(), g.title.c_str());
			snprintf(options.value[i], sizeof(options.value[i]), "%s",
				 g.format == riftwii::UsbImageFormat::Wbfs ? "WBFS" : "ISO");
		}
		if (options.length == 0) {
			snprintf(options.name[0], sizeof(options.name[0]), "No %s games found", sd ? "SD" : "USB");
			options.length = 1;
		}
		if (selectedGame >= options.length) selectedGame = 0;
	};
	fill();

	GuiText titleTxt(sd ? "SD games" : "USB games", 28, (GXColor){255, 255, 255, 255});
	titleTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.SetPosition(40,20);

	GuiText statusTxt(catalog.status.c_str(), 18, (GXColor){200, 200, 200, 255});
	statusTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	statusTxt.SetPosition(40, 58);

	GuiText detailTxt("A: choose   B: back   +/X: rescan", 16, (GXColor){255, 255, 255, 255});
	detailTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	detailTxt.SetPosition(40, 340);
	detailTxt.SetWrap(true, screenwidth - 80);

	GuiOptionBrowser browser(552, 248, &options);
	browser.SetPosition(0, 84);
	browser.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	browser.SetFocus(1);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);

	MenuButton backBtn("Back", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	backBtn.Place(ALIGN_H::LEFT, ALIGN_V::BOTTOM, 40, -35);
	MenuButton rescanBtn("Rescan", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_X, WIIDRC_BUTTON_PLUS);
	rescanBtn.Place(ALIGN_H::CENTRE, ALIGN_V::BOTTOM, 0, -35);
	for (MenuButton* b : {&backBtn, &rescanBtn}) b->button.SetScale(0.85f);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&statusTxt);
	w.Append(&browser);
	w.Append(&detailTxt);
	w.Append(&backBtn.button);
	w.Append(&rescanBtn.button);
	mainWindow->Append(&w);
	ResumeGui();

	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button, &rescanBtn.button});
		const int clicked = browser.GetClickedOption();
		if (clicked >= 0 && static_cast<std::size_t>(clicked) < catalog.games.size()) {
			selectedGame = clicked;
			std::string error;
			if ((sd ? SelectSdGame(state, static_cast<std::size_t>(clicked), error)
			        : SelectUsbGame(state, static_cast<std::size_t>(clicked), error))) {
				menu = MENU_HOME;
			} else {
				detailTxt.SetText(error.c_str());
			}
		}
		if(backBtn.Clicked())
			menu = MENU_SOURCE;
		else if(rescanBtn.Clicked()) {
			rescanBtn.button.ResetState();
			detailTxt.SetText(sd ? "Scanning SD..." : "Scanning USB...");
			ResumeGui();
			std::string error;
			const bool scanned = sd ? scan_sd_games(state.sd_catalog, error) : scan_usb_games(state.usb_catalog, error);
			HaltGui();
			if (!scanned) {
				riftwii::wii::ImageCatalog& updated = sd ? state.sd_catalog : state.usb_catalog;
				updated.status = std::string(sd ? "SD: " : "USB: ") + (error.empty() ? "scan failed" : error);
				statusTxt.SetText(updated.status.c_str());
				detailTxt.SetText(error.empty() ? "Image scan failed" : error.c_str());
			} else {
				statusTxt.SetText(catalog.status.c_str());
				detailTxt.SetText(catalog.games.empty() ? (sd ? "No SD games found (sd:/wbfs, sd:/games)" : "No USB games found (usb:/wbfs, usb:/games)")
										  : "A: choose   B: back   +/X: rescan");
			}
			fill();
			browser.TriggerUpdate();
		}
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

static int MenuHome(FrontendState& state)
{
	int menu = MENU_NONE;

	static OptionList options;
	memset(&options, 0, sizeof(options));
	std::string scanStatus;
	try {
		scanStatus = ScanPackages(state);
	} catch (...) {
		scanStatus = "Package scan failed; rescan to retry";
	}
	// Rows show packs made for this game plus broken XML (its error is
	// the point); other-disc packs are hidden. `visible` maps each row
	// back to its package: the browser only knows row numbers.
	std::vector<std::size_t> visible;
	const auto fill = [&]() {
		visible.clear();
		options.length = 0;
		for (std::size_t i = 0; i < state.model.packages.size(); ++i) {
			const auto& p = state.model.packages[i];
			if (!riftwii::show_package(p)) continue;
			if (options.length >= kOptionCapacity) break;
			visible.push_back(i);
			const int r = options.length++;
			snprintf(options.name[r], sizeof(options.name[r]), "%.49s", p.file.c_str());
			snprintf(options.value[r], sizeof(options.value[r]), "%s", PackageValue(p));
		}
		if (options.length == 0) {
			if (!state.model.packages.empty())
				snprintf(options.name[0], sizeof(options.name[0]), "No mods for this game (%u for other games)",
					 static_cast<unsigned>(state.model.packages.size()));
			else
				snprintf(options.name[0], sizeof(options.name[0]), "No packages available");
			options.length = 1;
		}
	};
	fill();
	if (state.model.packages.empty()) {
		selectedPackage = 0;
	} else if (selectedPackage < 0 || static_cast<std::size_t>(selectedPackage) >= state.model.packages.size() ||
	           !riftwii::show_package(state.model.packages[static_cast<std::size_t>(selectedPackage)])) {
		selectedPackage = 0;
		for (std::size_t i = 0; i < state.model.packages.size(); ++i) {
			if (riftwii::show_package(state.model.packages[i])) {
				selectedPackage = static_cast<int>(i);
				break;
			}
		}
	}

	GuiText titleTxt("Riftwii", 28, (GXColor){255, 255, 255, 255});
	titleTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.SetPosition(40,20);

	GuiText discTxt(state.disc_status.c_str(), 18, (GXColor){200, 200, 200, 255});
	discTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	discTxt.SetPosition(40, 58);

	GuiText detailTxt(BoundedHomeDetail(HomeDetail(state, scanStatus)).c_str(), 16, (GXColor){255, 255, 255, 255});
	detailTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	detailTxt.SetPosition(40, 328);
	detailTxt.SetWrap(false);

	GuiOptionBrowser browser(552, 238, &options);
	browser.SetPosition(0, 84);
	browser.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	browser.SetFocus(1);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);

	// Five buttons across a 640-wide screen: the template's 196-wide
	// button scaled to 167, stacked in pairs. Dump (a development aid)
	// has no button: the Wii Remote's 2 or a GameCube pad's B.
	MenuButton exitBtn("Exit", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, 0, WIIDRC_BUTTON_HOME);
	exitBtn.Place(ALIGN_H::LEFT, ALIGN_V::BOTTOM, 40, -35);
	MenuButton backBtn("Back", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	backBtn.Place(ALIGN_H::CENTRE, ALIGN_V::BOTTOM, 0, -82);
	MenuButton savesBtn("Save Mode", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, PAD_BUTTON_START, WIIDRC_BUTTON_MINUS);
	savesBtn.Place(ALIGN_H::CENTRE, ALIGN_V::BOTTOM, 0, -35);
	MenuButton optionsBtn("Mod Options", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_X, WIIDRC_BUTTON_PLUS);
	optionsBtn.Place(ALIGN_H::RIGHT, ALIGN_V::BOTTOM, -40, -82);
	MenuButton launchBtn("Launch", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y, WIIDRC_BUTTON_X);
	launchBtn.Place(ALIGN_H::RIGHT, ALIGN_V::BOTTOM, -40, -35);
	for (MenuButton* b : {&exitBtn, &backBtn, &savesBtn, &optionsBtn, &launchBtn}) b->button.SetScale(0.8f);
	GuiTrigger trigDump;
	trigDump.SetButtonOnlyTrigger(-1, WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_X, PAD_TRIGGER_Z);
	GuiButton dumpBtn(0, 0);  // invisible: a trigger only
	dumpBtn.SetTrigger(&trigDump);
	const auto save_before_leave = [&](int next_menu) {
		std::string error;
		if (!SaveChoices(state, error)) {
			launchBtn.button.ResetState();
			SetBoundedHomeDetail(detailTxt, error);
			return false;
		}
		menu = next_menu;
		return true;
	};

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&discTxt);
	w.Append(&browser);
	w.Append(&detailTxt);
	w.Append(&exitBtn.button);
	w.Append(&backBtn.button);
	w.Append(&savesBtn.button);
	w.Append(&optionsBtn.button);
	w.Append(&launchBtn.button);
	w.Append(&dumpBtn);
	mainWindow->Append(&w);
	ResumeGui();

	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&exitBtn.button, &backBtn.button, &savesBtn.button, &optionsBtn.button, &launchBtn.button});
		const int clicked = browser.GetClickedOption();
		if (clicked >= 0 && static_cast<std::size_t>(clicked) < visible.size()) {
			// A on a row: enable or disable it, saved at once so a later
			// Back navigation (which rescans and restores) cannot wipe it.
			const std::size_t pkg = visible[static_cast<std::size_t>(clicked)];
			selectedPackage = static_cast<int>(pkg);
			riftwii::LaunchPackage& p = state.model.packages[pkg];
			const bool enabling = !p.enabled;
			const bool simple_was_off = enabling && p.valid && p.package.options.size() == 1 &&
				p.package.options.front().choices.size() == 1 && p.package.options.front().selected == 0;
			if (!state.model.set_enabled(pkg, !p.enabled)) {
				if (!p.valid) SetBoundedHomeDetail(detailTxt, "Invalid XML; + details");
				else if (!p.for_disc) SetBoundedHomeDetail(detailTxt, "Other disc; cannot enable");
				else SetBoundedHomeDetail(detailTxt, "Cannot enable this package");
			} else {
				std::string save_error;
				if (!SaveChoices(state, save_error)) SetBoundedHomeDetail(detailTxt, save_error);
				else {
					const bool simple_auto_selected = simple_was_off && p.enabled && p.package.options.front().selected == 1;
					SetBoundedHomeDetail(detailTxt, ToggleDetail(p, p.enabled, simple_auto_selected));
				}
			}
			fill();
			browser.TriggerUpdate();
		}
		if(exitBtn.Clicked()) {
			std::string ignored;
			SaveChoices(state, ignored);  // best effort: quitting keeps the session's choices
			menu = MENU_EXIT;
		}
		else if(optionsBtn.Clicked()) {
			if (selectedPackage >= 0 && static_cast<std::size_t>(selectedPackage) < state.model.packages.size() &&
			    riftwii::show_package(state.model.packages[selectedPackage])) {
				menu = MENU_OPTIONS;
			} else {
				optionsBtn.button.ResetState();
				SetBoundedHomeDetail(detailTxt, "Select a mod; + options");
			}
		}
		else if(backBtn.Clicked()) {
			// Save first: MenuHome rescans and restores on entry, so an
			// unsaved Back would wipe row and choice changes. Stay put on
			// failure rather than silently losing them.
			std::string save_error;
			if (!SaveChoices(state, save_error)) {
				backBtn.button.ResetState();
				SetBoundedHomeDetail(detailTxt, save_error);
			} else if (state.use_usb || state.use_sd) {
				menu = MENU_GAMES;
			} else {
				menu = MENU_SOURCE;
			}
		}
		else if(savesBtn.Clicked()) {
			savesBtn.button.ResetState();
			if (state.model.save_mode == "separate") state.model.save_mode = "fresh";
			else if (state.model.save_mode == "fresh") state.model.save_mode = "nand";
			else state.model.save_mode = "separate";
			std::string error;
			if (!SaveChoices(state, error)) SetBoundedHomeDetail(detailTxt, error);
			else SetBoundedHomeDetail(detailTxt, HomeDetail(state, scanStatus));
		}
		else if(launchBtn.Clicked()) {
			if (state.game_id.empty()) {
				launchBtn.button.ResetState();
				SetBoundedHomeDetail(detailTxt, "Insert disc; rescan first");
			} else if (state.use_usb && !state.usb_catalog.cios_note.empty()) {
				// No cIOS in any candidate slot: refuse before the GUI tears
				// down, while the remedy is still readable on screen. The
				// reload path errors the same way headless (autorun) runs.
				launchBtn.button.ResetState();
				SetBoundedHomeDetail(detailTxt, state.usb_catalog.cios_note);
			} else if (state.use_sd && !state.sd_catalog.cios_note.empty()) {
				launchBtn.button.ResetState();
				SetBoundedHomeDetail(detailTxt, state.sd_catalog.cios_note);
			} else if (!riftwii::needs_launch_pipeline(!state.model.selections().empty(), state.model.save_mode)) {
				save_before_leave(MENU_BOOT);  // no resident work: boot the selected source as it is
			} else if (state.use_usb || state.use_sd) {
				// cIOS reload and F9 must happen after the GUI exits; compilation
				// follows the virtual DI probe in RunLaunch. This also retains a
				// selected Separate/Fresh save redirect with no enabled package.
				save_before_leave(MENU_LAUNCH);
			} else {
				// Preflight builds the resident plan even with no packages when
				// Separate/Fresh needs the save redirect for a physical disc.
				save_before_leave(MENU_PREFLIGHT);
			}
		}
		else if(dumpBtn.GetState() == STATE::CLICKED)
			menu = MENU_DUMP;
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

// Compiles the enabled packages and shows what they will do to the disc
// (the compiler's notes, one per row) or why they cannot; Boot goes on,
// Back returns with nothing changed.
static int MenuPreflight(FrontendState& state)
{
	int menu = MENU_NONE;

	GuiText titleTxt("Launch", 28, (GXColor){255, 255, 255, 255});
	titleTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.SetPosition(40,20);
	GuiText statusTxt("Compiling the enabled packages...", 18, (GXColor){200, 200, 200, 255});
	statusTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	statusTxt.SetPosition(40, 58);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&statusTxt);
	mainWindow->Append(&w);
	ResumeGui();

	// The compile reads the card and the disc; the GUI thread keeps drawing.
	std::string error;
	state.has_compiled = false;
	const bool ok = CompileSelection(state.model.selections(), state.compiled, error);
	state.has_compiled = ok;

	static OptionList options;
	memset(&options, 0, sizeof(options));
	std::vector<std::string> rows;
	if (ok) {
		const riftwii::SaveOverride saves =
		    riftwii::resolve_save_override(state.model.save_mode, state.compiled.savegame_dir, state.game_id);
		if (!state.compiled.savegame_dir.empty())
			rows.push_back("Saves: " + state.compiled.savegame_dir + " (from the mod)");
		else if (!saves.dir.empty())
			rows.push_back("Saves: " + saves.dir + (saves.clone ? " (progress cloned in once)" : " (fresh)"));
		else
			rows.push_back("Saves: NAND (as usual)");
		const std::size_t after_saves = rows.size();
		for (const std::string& warning : state.compiled.warnings) rows.push_back("warning: " + warning);
		for (const std::string& note : state.compiled.notes) rows.push_back(note);
		if (rows.size() == after_saves) rows.push_back("Nothing to apply");
	} else {
		rows.push_back(error);
	}
	for (const std::string& row : rows) {
		if (options.length == MAX_OPTIONS) break;
		snprintf(options.name[options.length], sizeof(options.name[0]), "%.49s", row.c_str());
		options.value[options.length][0] = 0;
		++options.length;
	}
	char summary[128];
	if (ok) {
		snprintf(summary, sizeof(summary), "%u file range(s), %u relocated or created file(s), %u memory patch(es)",
			 static_cast<unsigned>(state.compiled.entries.size()), static_cast<unsigned>(state.compiled.relocations.size()),
			 static_cast<unsigned>(state.compiled.memory.size()));
	} else {
		snprintf(summary, sizeof(summary), "Cannot launch: fix the package or the card and try again");
	}

	HaltGui();
	statusTxt.SetText(summary);
	GuiOptionBrowser browser(552, 248, &options);
	browser.SetPosition(0, 84);
	browser.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	browser.SetCol2Position(540);
	browser.SetFocus(1);

	GuiText detailTxt(ok ? "Boot (1/Y, X on GamePad) starts the game with these patches; Back (B) changes nothing" : error.c_str(), 16,
			  (GXColor){255, 255, 255, 255});
	detailTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	detailTxt.SetPosition(40, 340);
	detailTxt.SetWrap(true, screenwidth - 80);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);
	MenuButton backBtn("Back", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	backBtn.Place(ALIGN_H::LEFT, ALIGN_V::BOTTOM, 40, -35);
	MenuButton bootBtn("Boot", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y, WIIDRC_BUTTON_X);
	bootBtn.Place(ALIGN_H::RIGHT, ALIGN_V::BOTTOM, -40, -35);
	bootBtn.button.SetScale(0.85f);

	w.Append(&browser);
	w.Append(&detailTxt);
	w.Append(&backBtn.button);
	if (ok) w.Append(&bootBtn.button);
	ResumeGui();

	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		ClearStaleButtons({&backBtn.button, &bootBtn.button});
		browser.GetClickedOption();  // rows are information only
		if (backBtn.Clicked())
			menu = MENU_HOME;
		else if (ok && bootBtn.Clicked())
			menu = MENU_LAUNCH;
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

static int MenuOptions(FrontendState& state)
{
	int menu = MENU_NONE;
	riftwii::LaunchPackage& p = state.model.packages[selectedPackage];
	const bool readOnly = !p.valid;
	DetailPages detailPages = MakeDetailPages(p.detail);
	std::size_t detailPage = 0;

	static OptionList options;
	memset(&options, 0, sizeof(options));
	const auto fill = [&]() {
		options.length = 0;
		for (std::size_t i = 0; i < p.package.options.size() && options.length < MAX_OPTIONS; ++i) {
			const riftwii::Option& o = p.package.options[i];
			const int row = options.length++;
			if (o.section.empty()) {
				snprintf(options.name[row], sizeof(options.name[row]), "%.49s", o.name.c_str());
			} else {
				snprintf(options.name[row], sizeof(options.name[row]), "%.20s: %.27s", o.section.c_str(), o.name.c_str());
			}
			snprintf(options.value[row], sizeof(options.value[row]), "%.49s",
				 state.model.choice_name(selectedPackage, i).c_str());
		}
		if (options.length == 0) {
			snprintf(options.name[0], sizeof(options.name[0]), "This package has no options");
			options.length = 1;
		}
	};
	fill();

	GuiText titleTxt(p.file.c_str(), 26, (GXColor){255, 255, 255, 255});
	titleTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.SetPosition(40,20);
	titleTxt.SetMaxWidth(screenwidth - 80);

	GuiText hintTxt(readOnly ? "+: next detail   B: back" : "A: next choice   -: previous   +: next detail   B: back", 18,
		(GXColor){200, 200, 200, 255});
	hintTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	hintTxt.SetPosition(40, 58);

	// Paged diagnostics preserve the complete parser detail without allowing
	// a long no-whitespace attribute name to reach the button row.
	GuiText detailTxt0(detailPages.pages[detailPage].rows[0].c_str(), 16, (GXColor){255, 255, 255, 255});
	GuiText detailTxt1(detailPages.pages[detailPage].rows[1].c_str(), 16, (GXColor){255, 255, 255, 255});
	GuiText detailTxt2(detailPages.pages[detailPage].rows[2].c_str(), 16, (GXColor){255, 255, 255, 255});
	for (GuiText* text : {&detailTxt0, &detailTxt1, &detailTxt2}) {
		text->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
		text->SetWrap(false);
	}
	detailTxt0.SetPosition(40, 340);
	detailTxt1.SetPosition(40, 362);
	detailTxt2.SetPosition(40, 384);
	const std::array<GuiText*, kDetailLinesPerPage> detailRows = {&detailTxt0, &detailTxt1, &detailTxt2};

	GuiOptionBrowser browser(552, 248, &options);
	browser.SetPosition(0, 84);
	browser.SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	browser.SetFocus(1);

	GuiSound btnSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
	GuiImageData btnOutline(button_png);
	GuiImageData btnOutlineOver(button_over_png);

	MenuButton backBtn("Back", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B);
	backBtn.Place(ALIGN_H::LEFT, ALIGN_V::BOTTOM, 40, -35);
	MenuButton prevBtn("Previous", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, PAD_BUTTON_Y, WIIDRC_BUTTON_MINUS);
	prevBtn.Place(ALIGN_H::RIGHT, ALIGN_V::BOTTOM, -40, -35);
	MenuButton detailBtn("Next Detail", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_X, WIIDRC_BUTTON_PLUS);
	detailBtn.Place(ALIGN_H::CENTRE, ALIGN_V::BOTTOM, 0, -35);
	backBtn.button.SetScale(0.85f);
	prevBtn.button.SetScale(0.85f);
	detailBtn.button.SetScale(0.85f);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&hintTxt);
	if (!readOnly) w.Append(&browser);
	w.Append(&detailTxt0);
	w.Append(&detailTxt1);
	w.Append(&detailTxt2);
	w.Append(&backBtn.button);
	if (!readOnly) w.Append(&prevBtn.button);
	w.Append(&detailBtn.button);
	mainWindow->Append(&w);
	ResumeGui();

	int lastRow = 0;
	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		if (readOnly) ClearStaleButtons({&backBtn.button, &detailBtn.button});
		else ClearStaleButtons({&backBtn.button, &prevBtn.button, &detailBtn.button});
		const int clicked = readOnly ? -1 : browser.GetClickedOption();
		if (clicked >= 0 && static_cast<std::size_t>(clicked) < p.package.options.size()) {
			lastRow = clicked;
			state.model.cycle(selectedPackage, clicked, +1);
			fill();
			browser.TriggerUpdate();
		}
		if (!readOnly && prevBtn.Clicked()) {
			prevBtn.button.ResetState();
			if (static_cast<std::size_t>(lastRow) < p.package.options.size()) {
				state.model.cycle(selectedPackage, lastRow, -1);
				fill();
				browser.TriggerUpdate();
			}
		}
		if (detailBtn.Clicked()) {
			detailBtn.button.ResetState();
			detailPage = (detailPage + 1) % detailPages.pages.size();
			SetDetailRows(detailRows, detailPages.pages[detailPage]);
		}
		if(backBtn.Clicked()) {
			if (readOnly) {
				menu = MENU_HOME;
				ResumeGui();
				continue;
			}
			// Cycled choices live in memory until saved; MenuHome would
			// restore the file over them on entry.
			std::string save_error;
			if (!SaveChoices(state, save_error)) {
				backBtn.button.ResetState();
				SetDetailRows(detailRows, MakeDetailPages(save_error).pages.front());
			} else {
				menu = MENU_HOME;
			}
		}
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

int MainMenu(int menu, FrontendState& state)
{
	int currentMenu = menu;

	pointer[0] = new GuiImageData(player1_point_png);
	pointer[1] = new GuiImageData(player2_point_png);
	pointer[2] = new GuiImageData(player3_point_png);
	pointer[3] = new GuiImageData(player4_point_png);

	mainWindow = new GuiWindow(screenwidth, screenheight);

	bgImg = new GuiImage(screenwidth, screenheight, (GXColor){40, 40, 48, 255});
	bgImg->ColorStripe(30);
	mainWindow->Append(bgImg);

	ResumeGui();

	while(currentMenu != MENU_EXIT && currentMenu != MENU_LAUNCH && currentMenu != MENU_BOOT && currentMenu != MENU_DUMP)
	{
		switch (currentMenu)
		{
			case MENU_SOURCE:
				currentMenu = MenuSource(state);
				break;
			case MENU_GAMES:
				currentMenu = MenuGames(state);
				break;
			case MENU_OPTIONS:
				currentMenu = MenuOptions(state);
				break;
			case MENU_PREFLIGHT:
				currentMenu = MenuPreflight(state);
				break;
			case MENU_HOME:
			default:
				currentMenu = MenuHome(state);
				break;
		}
	}

	if (currentMenu == MENU_LAUNCH || currentMenu == MENU_BOOT || currentMenu == MENU_DUMP) {
		// The GUI thread is halted (the screens halt it before returning);
		// hand the screen back to main for the console phase.
		mainWindow->Remove(bgImg);
		return currentMenu;
	}

	ExitRequested = 1;
	ResumeGui();
	while(1) usleep(THREAD_SLEEP);
	return MENU_EXIT;
}
