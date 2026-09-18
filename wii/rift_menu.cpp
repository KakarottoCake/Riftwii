/****************************************************************************
 * Riftwii
 *
 * menu.cpp
 * Original Riftwii menu flow: home screen lists detected patch packages
 * and reports which runtime backend features are unavailable. Built on the
 * vendored libwiigui template structure.
 ***************************************************************************/

#include <gccore.h>
#include <ogcsys.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <memory>
#include <strings.h>
#include <sys/stat.h>
#include <wiiuse/wpad.h>

#include "libwiigui/gui.h"
#include "menu.h"
#include "demo.h"
#include "input.h"
#include "riftwii/patch.hpp"

#define THREAD_SLEEP 100
#define PACKAGE_DIR "sd:/riivolution"

struct PackageEntry {
	std::string name;
	std::string detail;
	bool valid = false;
};

static std::vector<PackageEntry> ScanPackages(std::string& status)
{
	std::vector<PackageEntry> entries;
	std::unique_ptr<DIR, int(*)(DIR*)> dir(opendir(PACKAGE_DIR), closedir);
	if (!dir) {
		status = errno == ENOENT ? "Create sd:/riivolution for XML packages" : "Cannot read SD package directory";
		return entries;
	}
	bool limited = false;
	while (true) {
		errno = 0;
		const auto* ent = readdir(dir.get());
		if (!ent) {
			if (errno != 0) {
				status = "SD directory read failed; rescan to retry";
				return entries;
			}
			break;
		}
		const char* dot = strrchr(ent->d_name, '.');
		if (!dot || strcasecmp(dot, ".xml") != 0) continue;
		if (entries.size() == MAX_OPTIONS) {
			limited = true;
			break;
		}
		const std::string path = std::string(PACKAGE_DIR) + "/" + ent->d_name;
		struct stat info;
		if (stat(path.c_str(), &info) == 0 && !S_ISREG(info.st_mode)) continue;
		PackageEntry entry;
		entry.name = ent->d_name;
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			entry.detail = "Cannot open package";
		} else {
			riftwii::Package package;
			entry.valid = riftwii::read_package(input, package, entry.detail);
			if (entry.valid) {
				entry.detail = "Valid XML: " + std::to_string(package.options.size()) +
					" options, " + std::to_string(package.patches.size()) + " patch definitions";
			}
		}
		entries.push_back(std::move(entry));
	}
	std::sort(entries.begin(), entries.end(), [](const PackageEntry& a, const PackageEntry& b) {
		return a.name < b.name;
	});
	status = limited ? "First 150 packages shown; directory limit reached" :
		(entries.empty() ? "No XML packages in sd:/riivolution" : "Select a package to see validation details");
	return entries;
}

static GuiImageData * pointer[4];
static GuiImage * bgImg = nullptr;
static GuiWindow * mainWindow = nullptr;
static lwp_t guithread = LWP_THREAD_NULL;
static std::atomic<bool> guiHalt{true};

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

static int MenuHome()
{
	int menu = MENU_NONE;

	static OptionList options;
	memset(&options, 0, sizeof(options));
	std::string scanStatus;
	std::vector<PackageEntry> entries;
	try {
		entries = ScanPackages(scanStatus);
	} catch (...) {
		scanStatus = "Package scan failed; rescan to retry";
	}
	for (const auto& entry : entries) {
		const int i = options.length++;
		snprintf(options.name[i], sizeof(options.name[i]), "%.49s", entry.name.c_str());
		snprintf(options.value[i], sizeof(options.value[i]), "%s", entry.valid ? "Valid XML" : "Invalid / unreadable");
	}
	if (options.length == 0) {
		snprintf(options.name[0], sizeof(options.name[0]), "No packages available");
		options.length = 1;
	}

	GuiText titleTxt("Riftwii", 28, (GXColor){255, 255, 255, 255});
	titleTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.SetPosition(40,20);

	GuiText statusTxt("XML validation only - game launching unavailable", 18, (GXColor){200, 200, 200, 255});
	statusTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	statusTxt.SetPosition(40, 58);

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

	GuiTrigger trigA, trigHome;
	trigA.SetSimpleTrigger(-1, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A);
	trigHome.SetButtonOnlyTrigger(-1, WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME, 0);

	GuiText exitBtnTxt("Exit", 22, (GXColor){0, 0, 0, 255});
	GuiImage exitBtnImg(&btnOutline);
	GuiImage exitBtnImgOver(&btnOutlineOver);
	GuiButton exitBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	exitBtn.SetAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
	exitBtn.SetPosition(100, -35);
	exitBtn.SetLabel(&exitBtnTxt);
	exitBtn.SetImage(&exitBtnImg);
	exitBtn.SetImageOver(&exitBtnImgOver);
	exitBtn.SetSoundOver(&btnSoundOver);
	exitBtn.SetTrigger(&trigA);
	exitBtn.SetTrigger(&trigHome);
	exitBtn.SetEffectGrow();

	GuiText rescanTxt("Rescan", 22, (GXColor){0, 0, 0, 255});
	GuiImage rescanImg(&btnOutline);
	GuiImage rescanOver(&btnOutlineOver);
	GuiTrigger trigRescan;
	trigRescan.SetButtonOnlyTrigger(-1, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_X);
	GuiButton rescanBtn(btnOutline.GetWidth(), btnOutline.GetHeight());
	rescanBtn.SetAlignment(ALIGN_H::RIGHT, ALIGN_V::BOTTOM);
	rescanBtn.SetPosition(-100, -35);
	rescanBtn.SetLabel(&rescanTxt);
	rescanBtn.SetImage(&rescanImg);
	rescanBtn.SetImageOver(&rescanOver);
	rescanBtn.SetTrigger(&trigA);
	rescanBtn.SetTrigger(&trigRescan);

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&statusTxt);
	w.Append(&browser);
	w.Append(&detailTxt);
	w.Append(&exitBtn);
	w.Append(&rescanBtn);
	mainWindow->Append(&w);
	ResumeGui();

	while(menu == MENU_NONE)
	{
		usleep(10000);
		HaltGui();
		const int selected = browser.GetClickedOption();
		if (selected >= 0 && static_cast<std::size_t>(selected) < entries.size()) {
			detailTxt.SetText(entries[selected].detail.substr(0, 160).c_str());
		}
		if(exitBtn.GetState() == STATE::CLICKED)
			menu = MENU_EXIT;
		else if(rescanBtn.GetState() == STATE::CLICKED)
			menu = MENU_HOME;
		ResumeGui();
	}

	HaltGui();
	mainWindow->Remove(&w);
	return menu;
}

void MainMenu(int menu)
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

	while(currentMenu != MENU_EXIT)
	{
		switch (currentMenu)
		{
			case MENU_HOME:
				currentMenu = MenuHome();
				break;
			default:
				currentMenu = MenuHome();
				break;
		}
	}

	ExitRequested = 1;
	ResumeGui();
	while(1) usleep(THREAD_SLEEP);
}
