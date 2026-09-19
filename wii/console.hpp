// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii::wii {

// Switches the display to libogc's text console (a fresh framebuffer)
// and mirrors it to a USB Gecko in slot B when one answers. Safe to call
// after the libwiigui renderer has been stopped.
void ConsoleStart(bool video_initialised);

// Blocks until RESET, or HOME/Start on any pad, then returns.
void WaitForExit();

}  // namespace riftwii::wii
