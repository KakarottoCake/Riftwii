// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A tester's Wii froze (the menu stuck, the music playing on) some seconds
// after a game's page opened, and the log said nothing about where. The
// GUI thread says where it is: each frame it names the phase it is in
// (GuiPhase), and each element it draws names itself (DrawTrace). A thread
// that is never halted (the menu watchdog) writes both to the log once a
// second for a while after DrawTraceArm, so the last lines before the
// freeze show whether the frames kept coming and where the GUI thread
// stopped. Not armed (the usual case) the cost is a few stores a frame.

#include <ogc/lwp_watchdog.h>

namespace riftwii::wii {

// What the GUI thread is doing, read by the watchdog.
extern const char* volatile g_guiPhase;
extern const char* volatile g_guiElement;
// When the watchdog stops writing (gettime ticks); 0 = not armed.
extern volatile unsigned long long g_drawTraceUntil;

inline void GuiPhase(const char* phase) { g_guiPhase = phase; }

// The next `seconds` seconds of the watchdog's lines.
inline void DrawTraceArm(unsigned seconds) {
    g_drawTraceUntil = gettime() + static_cast<unsigned long long>(seconds) * TB_TIMER_CLOCK * 1000ull;
}

class DrawTrace {
public:
    explicit DrawTrace(const char* what) : previous_(g_guiElement) { g_guiElement = what; }
    ~DrawTrace() { g_guiElement = previous_; }
    DrawTrace(const DrawTrace&) = delete;
    DrawTrace& operator=(const DrawTrace&) = delete;

private:
    const char* previous_;
};

}  // namespace riftwii::wii
