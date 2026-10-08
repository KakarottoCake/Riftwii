// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The menu's transitions: no screen, page or popup cuts in. Every frame
// the GUI thread keeps a picture of what it drew (the EFB, before the
// pointers); when the screen changes, that picture of the old screen is
// drawn over the new one and fades, zooms or slides away while the new
// one comes in through the camera (video.cpp's Menu_SetCamera).
//
// A window added to or taken from the menu's root window starts a
// crossfade by itself; a screen that knows better asks first with Begin
// (zoom into the game that was picked, slide to a page under this one).
// Both are called with the GUI halted, before the change.

namespace riftwii::wii::transition {

enum class Kind {
    Fade,          // a crossfade
    ZoomIn,        // into `from` (the tile picked): the new screen grows out of it
    ZoomOut,       // back out to `from`: the old screen shrinks into it
    SlideForward,  // to a page under this one: in from the right
    SlideBack,     // back from it: in from the left
    PopOpen,       // a popup comes up (its window scales itself, PopWindow)
    PopClose,      // and goes
    PageForward,   // Home's next page: the old one slides out to the left, inside `from` only
    PageBack,      // the page before: out to the right
    FromBlack,     // the menu's start
};

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;  // menu units
};

void Init();  // after the video and the skin

void Begin(Kind kind, Rect from = {});
void BeginAuto();  // a crossfade, unless one was asked for already
// The last frame stays on screen, whatever is drawn under it, until the
// next screen comes (a window added, or Begin): then `next` plays from
// it. For a screen that goes before the next one is ready (a sub-page
// built with the GUI running, a game opening behind its banner).
void Hold(Kind next, Rect from = {});

// GUI thread, each frame: before drawing the menu (the camera for the new
// screen), then after it (the old screen over it, then the picture kept).
void FrameStart();
void FrameEnd();
bool Busy();  // input waits while the new screen is still arriving
// Any thread, the GUI running: until the transition on screen is done
// (the launch screen stays as the last frame, its log printed into it).
void Settle(unsigned maxMs = 1500);
void SetSlowMotion(float factor);  // the GUI script's "slowmo": every transition that much longer
float SlowMotion();

// Easing curves, t from 0 to 1.
float Ease(float t);       // in and out
float EaseOut(float t);    // fast, then settling
float EaseBack(float t);   // past the end a little, then back (a pop)

}  // namespace riftwii::wii::transition
