// SPDX-License-Identifier: GPL-3.0-or-later
/****************************************************************************
 * Riftwii
 *
 * rift_menu.cpp
 * The frontend: the home screen lists the packages on the card and which
 * are enabled for the inserted disc, an options screen sets each option's
 * choice, and Launch hands the selection to the boot. The state itself
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
#include <wiiuse/wpad.h>

#include "libwiigui/gui.h"
#include "menu.h"
#include "demo.h"
#include "input.h"
#include "riftwii/patch.hpp"

#define THREAD_SLEEP 100

using riftwii::wii::ScanPackages;
using riftwii::wii::SaveChoices;

static const char* PackageValue(const riftwii::LaunchPackage& p)
{
	if (!p.valid) return "Invalid";
	if (!p.for_disc) return "Other disc";
	return p.enabled ? "On" : "Off";
}

static GuiImageData * pointer[4];
static GuiImage * bgImg = nullptr;
static GuiWindow * mainWindow = nullptr;
static lwp_t guithread = LWP_THREAD_NULL;
static std::atomic<bool> guiHalt{true};
static int selectedPackage = 0;  // the home screen's highlighted row, kept across screens

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
struct MenuButton {
	GuiText text;
	GuiImage image;
	GuiImage imageOver;
	GuiTrigger trigA;
	GuiTrigger trigExtra;
	GuiButton button;
	MenuButton(const char* label, GuiImageData& outline, GuiImageData& outlineOver, GuiSound& sound,
		   u32 wpadExtra, u16 padExtra)
		: text(label, 22, (GXColor){0, 0, 0, 255}), image(&outline), imageOver(&outlineOver),
		  button(outline.GetWidth(), outline.GetHeight())
	{
		trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);
		trigExtra.SetButtonOnlyTrigger(-1, wpadExtra, padExtra);
		button.SetLabel(&text);
		button.SetImage(&image);
		button.SetImageOver(&imageOver);
		button.SetSoundOver(&sound);
		button.SetTrigger(&trigA);
		if (wpadExtra || padExtra) button.SetTrigger(&trigExtra);
		button.SetEffectGrow();
	}
	void Place(ALIGN_H h, ALIGN_V v, int x, int y)
	{
		button.SetAlignment(h, v);
		button.SetPosition(x, y);
	}
	bool Clicked() { return button.GetState() == STATE::CLICKED; }
};

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
	const auto fill = [&]() {
		options.length = 0;
		for (const auto& p : state.model.packages) {
			const int i = options.length++;
			snprintf(options.name[i], sizeof(options.name[i]), "%.49s", p.file.c_str());
			snprintf(options.value[i], sizeof(options.value[i]), "%s", PackageValue(p));
		}
		if (options.length == 0) {
			snprintf(options.name[0], sizeof(options.name[0]), "No packages available");
			options.length = 1;
		}
	};
	fill();

	GuiText titleTxt("Riftwii", 28, (GXColor){255, 255, 255, 255});
	titleTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.SetPosition(40,20);

	GuiText discTxt(state.disc_status.c_str(), 18, (GXColor){200, 200, 200, 255});
	discTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	discTxt.SetPosition(40, 58);

	GuiText detailTxt(scanStatus.c_str(), 16, (GXColor){255, 255, 255, 255});
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

	// Three buttons across a 640-wide screen: the template's 196-wide
	// button scaled to 167. Dump (a development aid) has no button: the
	// Wii Remote's 2 or a GameCube pad's B.
	MenuButton exitBtn("Exit", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, 0);
	exitBtn.Place(ALIGN_H::LEFT, ALIGN_V::BOTTOM, 40, -35);
	MenuButton optionsBtn("Options", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_X);
	optionsBtn.Place(ALIGN_H::CENTRE, ALIGN_V::BOTTOM, 0, -35);
	MenuButton launchBtn("Launch", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y);
	launchBtn.Place(ALIGN_H::RIGHT, ALIGN_V::BOTTOM, -40, -35);
	for (MenuButton* b : {&exitBtn, &optionsBtn, &launchBtn}) b->button.SetScale(0.85f);
	GuiTrigger trigDump;
	trigDump.SetButtonOnlyTrigger(-1, WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_X, PAD_BUTTON_B);
	GuiButton dumpBtn(0, 0);  // invisible: a trigger only
	dumpBtn.SetTrigger(&trigDump);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&discTxt);
	w.Append(&browser);
	w.Append(&detailTxt);
	w.Append(&exitBtn.button);
	w.Append(&optionsBtn.button);
	w.Append(&launchBtn.button);
	w.Append(&dumpBtn);
	mainWindow->Append(&w);
	ResumeGui();

	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		const int clicked = browser.GetClickedOption();
		if (clicked >= 0 && static_cast<std::size_t>(clicked) < state.model.packages.size()) {
			// A on a row: enable or disable it.
			selectedPackage = clicked;
			const riftwii::LaunchPackage& p = state.model.packages[clicked];
			if (!state.model.set_enabled(clicked, !p.enabled)) {
				detailTxt.SetText(p.valid ? "This package is for another disc" : p.detail.substr(0, 160).c_str());
			} else {
				detailTxt.SetText(p.detail.substr(0, 160).c_str());
			}
			fill();
			browser.TriggerUpdate();
		}
		if(exitBtn.Clicked())
			menu = MENU_EXIT;
		else if(optionsBtn.Clicked()) {
			if (selectedPackage >= 0 && static_cast<std::size_t>(selectedPackage) < state.model.packages.size() &&
			    state.model.packages[selectedPackage].valid) {
				menu = MENU_OPTIONS;
			} else {
				optionsBtn.button.ResetState();
				detailTxt.SetText("Enable or disable a package with A first; + opens its options");
			}
		}
		else if(launchBtn.Clicked()) {
			if (state.game_id.empty()) {
				launchBtn.button.ResetState();
				detailTxt.SetText("Insert a disc and rescan (Dump) before launching");
			} else if (state.model.selections().empty()) {
				menu = MENU_BOOT;  // nothing enabled: the disc as it is
			} else {
				menu = MENU_LAUNCH;
			}
		}
		else if(dumpBtn.GetState() == STATE::CLICKED)
			menu = MENU_DUMP;
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	if (menu == MENU_LAUNCH || menu == MENU_BOOT) SaveChoices(state);
	return menu;
}

static int MenuOptions(FrontendState& state)
{
	int menu = MENU_NONE;
	riftwii::LaunchPackage& p = state.model.packages[selectedPackage];

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

	GuiText hintTxt("A: next choice   -: previous choice   B: back", 18, (GXColor){200, 200, 200, 255});
	hintTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	hintTxt.SetPosition(40, 58);

	GuiText detailTxt(p.detail.substr(0, 160).c_str(), 16, (GXColor){255, 255, 255, 255});
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

	MenuButton backBtn("Back", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B);
	backBtn.Place(ALIGN_H::LEFT, ALIGN_V::BOTTOM, 40, -35);
	MenuButton prevBtn("Previous", btnOutline, btnOutlineOver, btnSoundOver, WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, PAD_TRIGGER_L);
	prevBtn.Place(ALIGN_H::RIGHT, ALIGN_V::BOTTOM, -40, -35);
	backBtn.button.SetScale(0.85f);
	prevBtn.button.SetScale(0.85f);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&hintTxt);
	w.Append(&browser);
	w.Append(&detailTxt);
	w.Append(&backBtn.button);
	w.Append(&prevBtn.button);
	mainWindow->Append(&w);
	ResumeGui();

	int lastRow = 0;
	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		const int clicked = browser.GetClickedOption();
		if (clicked >= 0 && static_cast<std::size_t>(clicked) < p.package.options.size()) {
			lastRow = clicked;
			state.model.cycle(selectedPackage, clicked, +1);
			fill();
			browser.TriggerUpdate();
		}
		if (prevBtn.Clicked()) {
			prevBtn.button.ResetState();
			if (static_cast<std::size_t>(lastRow) < p.package.options.size()) {
				state.model.cycle(selectedPackage, lastRow, -1);
				fill();
				browser.TriggerUpdate();
			}
		}
		if(backBtn.Clicked())
			menu = MENU_HOME;
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
			case MENU_OPTIONS:
				currentMenu = MenuOptions(state);
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
