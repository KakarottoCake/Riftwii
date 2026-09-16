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
#include <wiiuse/wpad.h>

#include "libwiigui/gui.h"
#include "menu.h"
#include "demo.h"
#include "input.h"

#define THREAD_SLEEP 100

static GuiImageData * pointer[4];
static GuiImage * bgImg = nullptr;
static GuiWindow * mainWindow = nullptr;
static lwp_t guithread = LWP_THREAD_NULL;
static bool guiHalt = true;

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
			LWP_SuspendThread(guithread);
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
	LWP_CreateThread(&guithread, UpdateGUI, nullptr, nullptr, 24576, 70);
}

static int MenuHome()
{
	int menu = MENU_NONE;

	GuiText titleTxt("Riftwii", 28, (GXColor){255, 255, 255, 255});
	titleTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.SetPosition(50,50);

	GuiText statusTxt("Runtime backend: not implemented", 20, (GXColor){200, 200, 200, 255});
	statusTxt.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	statusTxt.SetPosition(50, 90);

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

	HaltGui();
	GuiWindow w(screenwidth, screenheight);
	w.Append(&titleTxt);
	w.Append(&statusTxt);
	w.Append(&exitBtn);
	mainWindow->Append(&w);
	ResumeGui();

	while(menu == MENU_NONE)
	{
		usleep(THREAD_SLEEP);

		if(exitBtn.GetState() == STATE::CLICKED)
			menu = MENU_EXIT;
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

	ResumeGui();
	ExitRequested = 1;
	while(1) usleep(THREAD_SLEEP);
}
