// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "drawtrace.hpp"

#include <gccore.h>

#include <string>
#include <typeinfo>

#include "libwiigui/gui.h"
#include "loadersettings.hpp"
#include "log.hpp"

namespace riftwii::wii {

const char* volatile g_guiPhase = "not started";
const char* volatile g_guiElement = "";
volatile unsigned long long g_drawTraceUntil = 0;
volatile bool g_gxWatch = false;
volatile bool g_gxTrace = false;

namespace {

// Written by the GUI thread only; the watchdog reads it when the frames
// have stopped, so a record half written is at worst one odd line.
constexpr int kRing = 16;
GxDrawRecord g_ring[kRing];
volatile u32 g_next = 0;  // records written so far

GxDrawRecord& Latest() { return g_ring[(g_next + kRing - 1) % kRing]; }

void BeforeElement(GuiElement* element) {
    GxDrawRecord& record = g_ring[g_next % kRing];
    record.kind = typeid(*element).name();
    record.name = "";
    record.texture = nullptr;
    record.width = record.height = 0;
    record.format = 0;
    record.frame = FrameTimer;
    g_next = g_next + 1;
}

// The GPU done with everything this element sent before the next one
// starts: an element that hangs it stays the last in the ring.
void AfterElement(GuiElement*) { GX_DrawDone(); }

void Texture(const void* data, u16 width, u16 height, u8 format) {
    if (g_next == 0) return;
    GxDrawRecord& record = Latest();
    record.texture = data;
    record.width = width;
    record.height = height;
    record.format = format;
}

}  // namespace

void GxTraceStart() {
    const auto& other = Settings().other;
    const auto found = other.find("debug_gx");
    if (found == other.end() || found->second.empty()) return;
    const std::string& mode = found->second;
    if (mode == "watch") {
        g_gxWatch = true;
        logf("Graphics: settings.txt has debug_gx = watch: the watchdog logs the GUI thread's state for a minute after "
             "a game's page opens, and when the frames stop (for testing only)\n");
        return;
    }
    if (mode != "drawdone") {
        logf("Graphics: settings.txt's debug_gx = %s is not known (watch or drawdone); left off\n", mode.c_str());
        return;
    }
    GuiDrawHookBefore = BeforeElement;
    GuiDrawHookAfter = AfterElement;
    Menu_TextureHook = Texture;
    g_gxWatch = true;
    g_gxTrace = true;
    logf("Graphics: settings.txt has debug_gx = drawdone: the GUI waits for the GPU after every element it draws, and "
         "the watchdog logs the last elements drawn if the frames stop (slower drawing, for testing only)\n");
}

void GxTraceTexture(const void* data, int width, int height, int format) {
    if (!g_gxTrace) return;
    Texture(data, static_cast<u16>(width), static_cast<u16>(height), static_cast<u8>(format));
}

int GxTraceLast(GxDrawRecord* out, int max) {
    const u32 written = g_next;
    int count = 0;
    for (; count < max && count < kRing && static_cast<u32>(count) < written; ++count)
        out[count] = g_ring[(written - 1 - count) % kRing];
    return count;
}

const char* GxFormatName(int format) {
    switch (format) {
    case GX_TF_I4: return "I4";
    case GX_TF_I8: return "I8";
    case GX_TF_IA4: return "IA4";
    case GX_TF_IA8: return "IA8";
    case GX_TF_RGB565: return "RGB565";
    case GX_TF_RGB5A3: return "RGB5A3";
    case GX_TF_RGBA8: return "RGBA8";
    case GX_TF_CI4: return "CI4";
    case GX_TF_CI8: return "CI8";
    case GX_TF_CI14: return "CI14";
    case GX_TF_CMPR: return "CMPR";
    default: return "?";
    }
}

DrawTrace::DrawTrace(const char* what) : previous_(g_guiElement) {
    g_guiElement = what;
    if (g_gxTrace && g_next != 0) Latest().name = what;
}

}  // namespace riftwii::wii
