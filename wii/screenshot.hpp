// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Screenshots of the menu, and the import of the ones taken in games.
//
// In the menu: hold 1 on a Wii Remote and press HOME, or hold L and R on a
// GameCube controller and press Down on the D-pad. The screen flashes and
// its PNG goes to sd:/riftwii/screenshots/riftwii-NNNN.png, written by a
// thread of its own while the menu goes on. The held button (1, L or R)
// then does nothing; alone, it does its usual job when it is let go
// instead of when it is pressed, and the button that completes the combo
// never reaches the menu.
//
// Games: the in-game blob (runtime/shot, wii/shothook.hpp) leaves each
// capture as a raw file in the NAND's /shared2/riftwii; at the menu's
// start the same thread turns them into sd:/riftwii/screenshots/<game
// ID>-NNNN.png and deletes them from the NAND.
namespace riftwii::wii {

// With the menu: starts the thread, whose first job is the import.
void ScreenshotsStart();
// Before a launch or exit: waits for the file being written, then stops.
void ScreenshotsStop();
// In the GUI thread, after the pads are read: watches for the combo.
void ScreenshotPoll();
// After a frame is shown: copies it when the combo asked for it.
void ScreenshotAfterFrame(const void* xfb, int width, int height);
// 0-255: the white flash to draw over the menu (fades frame by frame).
int ScreenshotFlash();

// The NAND folder the in-game blob writes (runtime/shot/shot_hook.h).
constexpr const char* kGameShotDir = "/shared2/riftwii";

}  // namespace riftwii::wii
