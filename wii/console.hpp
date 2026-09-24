// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii::wii {

// Switches the display to libogc's text console (a fresh framebuffer)
// and mirrors it to a USB Gecko in slot B when one answers. Safe to call
// after the libwiigui renderer has been stopped.
void ConsoleStart(bool video_initialised);

// Starts the console in a window of the picture already on screen (the
// menu's launch frame): `xfb` is that YUYV framebuffer, `fb_width` x
// `fb_height`; the text goes into x, y, width, height, dark on white.
void ConsoleStartInFrame(void* xfb, int fb_width, int fb_height, int x, int y, int width, int height);

// Blocks until RESET, or HOME/Start on any pad, then returns.
void WaitForExit();

// After a failed launch or a crash: A (any pad) or RESET means start
// RiftWii again, HOME (Start on a GameCube pad) means leave it. Gives up
// after `timeout_seconds` (0: never) with Restart.
enum class ExitChoice { Restart, Leave };
ExitChoice WaitForChoice(int timeout_seconds);

}  // namespace riftwii::wii
