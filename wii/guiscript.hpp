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
//   hold <button>        press and keep holding it (until release)
//   release              let go of held buttons
//   glide <x> <y> <n>    move the pointer there over n frames (a drag, with hold)
//   shot <path>          save the screen as a 24-bit BMP (sd:/...)
//   finalshot <path>     save the launch screen once the menu has left
//                        (on a dump, which press 2 on the game page starts)
//   failnext             the next launch fails at once (tests the way back)
//   crash                a trap instruction (tests wii/crash.cpp)
//   launchshots          save the launch screen at each progress stage
//                        while the card is up (sd:/riftwii/launch_<n>.bmp)
//
// After a restart (wii/restart.hpp) the menu reads guiscript-restart.txt
// instead, so a script that crashes on purpose does not run again.
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
// Whether the script asked the next launch to fail (failnext).
bool GuiScriptFailLaunch();
// The crash screen, saved as sd:/riftwii/crashscreen.bmp when a script
// was loaded (Dolphin's frame dump misses screens drawn without GX).
void GuiScriptCrashShot(const void* xfb, int width, int height);
// A launch stage at `percent` (wii/progress.cpp), when launchshots is on.
void GuiScriptLaunchShot(int percent, const void* xfb, int width, int height);

}  // namespace riftwii::wii
