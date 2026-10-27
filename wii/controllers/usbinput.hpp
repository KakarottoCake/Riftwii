// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2026 quatric (libwiiinput) <https://github.com/quatric/libwiiinput>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "usbkeyboard.hpp"

// USB keyboards, USB mice and pen tablets in the menu (the drivers come from
// libwiiinput), and the uDraw GameTablet on a Wii Remote. They work the menu
// through player 1's controls, the way the GameCube adapter works it
// through the GameCube ports (vendor-libgui/source/input.cpp, UpdatePads):
//
//  - The keyboard is a pad: the arrows are the D-pad, Enter is A, Esc and
//    Backspace are B, Home and F1 are Start, 1 is the Wii Remote's 1.
//    Page Up and Page Down turn Home's pages. While the search or a name is
//    being typed (GuiSearchKeys) its keys type instead.
//  - The mouse and a pen tablet point, as a Wii Remote does on channel 1:
//    the left button (the pen's tip) is A, the right one (the pen's lower
//    switch) is B, and the mouse wheel turns Home's pages. A Remote's
//    button, the D-pad or the Remote's pointer moving takes the pointer
//    back, as between a Remote and a GameCube controller.
//  - A uDraw points on its own Wii Remote's channel while its pen is over
//    the drawing area, the tip as A and the lower rocker as B.
namespace riftwii::wii {

// The "USB keyboard, mouse and tablet" setting (usb_input). Off, the
// drivers stop at the next frame and don't start, and the libogc wrappers
// they need only call libogc (TB_SetHooks), so a tester whose menu misbehaves
// with them can turn them all off. main sets it before the Wii Remotes start.
void UsbInputSetEnabled(bool on);

// The menu's first drive scan is over: the drivers may start (their USB
// traffic waits, as the adapter's does: GcAdapterMenuAllowStart).
void UsbInputMenuAllowStart();
// The menu is over (a launch, or leaving): stops them for good.
void UsbInputMenuEnd();
// Stops them before an IOS reload; the menu starts them again after it.
void UsbInputStop();
// An IOS reload went through: a keyboard read left waiting in the old IOS
// is gone with it (UsbKeyboardStop).
void UsbInputIosReloaded();

// UpdatePads calls these on the GUI thread, around UpdatePadPointers:
// before, the keyboard's buttons go into channel 1's pad, so the pointer
// logic sees its D-pad; after, the mouse's and the tablets' pointers and
// buttons, which it would otherwise hide.
void UsbInputBeforePointers();
void UsbInputAfterPointers();

// Pages to turn on Home (Page Up and Page Down, the mouse wheel), taken
// once: negative is back. Only player 1's controls.
int UsbInputTakePages();

// While a GuiSearchKeys is open the keyboard types into it (on: one opened,
// off: one closed); its keys then make no pad buttons except the arrows.
void UsbInputTyping(bool on);
// The next key typed, while typing (UsbInputTyping).
bool UsbInputTyped(KeyPress& out);

bool UsbInputKeyboardConnected();
// A uDraw on any Wii Remote.
bool UsbInputUDrawConnected();
// What is plugged in, for the launch log and problem reports
// (DescribeControllers); empty when nothing is.
std::string UsbInputDescribe();

}  // namespace riftwii::wii
