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
//
// Both only with settings.txt's debug_gx:
// - "watch": the watchdog's lines for a minute after a game's page opens,
//   and once whenever the frames stop with the GUI not halted.
// - "drawdone": that, and the GX trace, for a GPU that stops on a console
//   only. Every element a window draws is written to a small ring before
//   it draws (its class, its DrawTrace name, the last picture it drew:
//   address, size, format), and the GUI thread waits for the GPU after
//   each one (GX_DrawDone). An element that hangs the GPU is then the
//   last one in the ring, and the watchdog logs the ring when the frames
//   stop.
// Off (the usual case) nothing is recorded and nothing waits.

#include <gctypes.h>
#include <ogc/lwp_watchdog.h>

namespace riftwii::wii {

// What the GUI thread is doing, read by the watchdog.
extern const char* volatile g_guiPhase;
extern const char* volatile g_guiElement;
// When the watchdog stops writing (gettime ticks); 0 = not armed.
extern volatile unsigned long long g_drawTraceUntil;
// settings.txt debug_gx = watch or drawdone: the watchdog's lines.
extern volatile bool g_gxWatch;
// settings.txt debug_gx = drawdone: the GX trace is on.
extern volatile bool g_gxTrace;

inline void GuiPhase(const char* phase) { g_guiPhase = phase; }

// The next `seconds` seconds of the watchdog's lines.
inline void DrawTraceArm(unsigned seconds) {
    g_drawTraceUntil = gettime() + static_cast<unsigned long long>(seconds) * TB_TIMER_CLOCK * 1000ull;
}

// One element drawn, as the GX trace keeps it.
struct GxDrawRecord {
    const char* kind = "";      // its class (as the compiler names it)
    const char* name = "";      // its DrawTrace name, when it has one
    const void* texture = nullptr;  // the last picture it drew
    u16 width = 0, height = 0;
    u8 format = 0;              // GX_TF_*
    u32 frame = 0;
};

// Reads settings.txt's debug_gx and, when it asks for it, hooks the GX
// trace into libgui's windows and drawing. Once, before the GUI thread draws.
void GxTraceStart();
// A picture an element drew outside libgui's own calls (skin.cpp, the
// transitions): for the ring, when the trace is on.
void GxTraceTexture(const void* data, int width, int height, int format);
// The last `max` records, newest first; how many there were.
int GxTraceLast(GxDrawRecord* out, int max);
// A GX_TF_* format's name ("RGBA8").
const char* GxFormatName(int format);

class DrawTrace {
public:
    explicit DrawTrace(const char* what);
    ~DrawTrace() { g_guiElement = previous_; }
    DrawTrace(const DrawTrace&) = delete;
    DrawTrace& operator=(const DrawTrace&) = delete;

private:
    const char* previous_;
};

}  // namespace riftwii::wii
