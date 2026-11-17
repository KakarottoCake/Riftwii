// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii::wii {

// Switches the display to libogc's text console (a fresh framebuffer)
// and mirrors it to a USB Gecko in slot B when one answers (with
// Settings > USB Gecko log on, the log's own copy of stdout stays).
// Safe to call after the libwiigui renderer has been stopped.
void ConsoleStart(bool video_initialised);

// Starts the console in a window of the picture already on screen (the
// menu's launch frame): `xfb` is that YUYV framebuffer, `fb_width` x
// `fb_height`; the text goes into x, y, width, height, dark on white.
void ConsoleStartInFrame(void* xfb, int fb_width, int fb_height, int x, int y, int width, int height);

// The launch screen with the game's box on its card instead of the log
// (Settings > Launch screen): remembers where the log would go, without
// touching the screen; its lines are kept meanwhile (LogKeepScreen).
void ConsoleDeferInFrame(void* xfb, int fb_width, int fb_height, int x, int y, int width, int height);
// Shows a deferred log now, over the box: on a button, or before the
// player is asked anything (WaitForChoice and WaitForExit call it).
// Does nothing when the log is on screen already.
void ConsoleRevealDeferred();
// Whether the log is held back behind the box now.
bool ConsoleDeferred();

// Blocks until RESET, or HOME/Start on any pad, then returns. The
// GameCube adapter's ports count as pads (GcAdapterScreenStart).
void WaitForExit();

// After a failed launch or a crash: A (any pad) or RESET means start
// RiftWii again, HOME (Start on a GameCube pad) means leave it. Gives up
// after `timeout_seconds` (0: never) with Restart. `adapter`: the
// GameCube adapter's ports count as pads too (not after a crash, which
// may have left the heap or the driver damaged).
enum class ExitChoice { Restart, Leave };
ExitChoice WaitForChoice(int timeout_seconds, bool adapter = true);

}  // namespace riftwii::wii
