// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Scripted input for checking the menu without hands on a controller (in
// Dolphin, for instance). When sd:/riftwii/guiscript.txt exists the menu
// reads it at start and plays it on channel 0, one command per line:
//
//   wait <frames>        let the menu run
//   point <x> <y>        aim the pointer there (stays until nopoint)
//   nopoint              take the pointer away (the D-pad focus shows)
//   press <button>       one press: A B PLUS MINUS 1 2 HOME UP DOWN LEFT RIGHT
//   shot <path>          save the screen as a 24-bit BMP (sd:/...)
//   finalshot <path>     save the launch screen once the menu has left
//                        (on a dump, which press 2 on the game page starts)
//
// Nothing happens when the file is absent.
namespace riftwii::wii {

bool GuiScriptLoad(const char* path);
bool GuiScriptActive();
// After the pads are read: overrides channel 0 with the script's pointer
// and presses.
void GuiScriptApply();
// After each rendered frame: advances waits, takes screenshots of `xfb`
// (the YUYV frame just shown, `width` x `height`).
void GuiScriptAfterFrame(const void* xfb, int width, int height);
// Takes the finalshot, if the script asked for one.
void GuiScriptFinalShot(const void* xfb, int width, int height);

}  // namespace riftwii::wii
