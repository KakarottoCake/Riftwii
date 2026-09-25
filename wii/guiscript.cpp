// SPDX-License-Identifier: GPL-3.0-or-later
#include "guiscript.hpp"

#include <gccore.h>
#include <wiiuse/wpad.h>

#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "input.h"
#include "libwiigui/gui.h"
#include "log.hpp"
#include "memlimits.hpp"

namespace riftwii::wii {
namespace {

std::vector<std::string> g_lines;
std::size_t g_next = 0;
bool g_active = false;
int g_wait = 0;
bool g_pointing = false;
float g_x = 0, g_y = 0;
u32 g_press = 0;
int g_press_frames = 0;
u32 g_held = 0, g_held_new = 0;
float g_from_x = 0, g_from_y = 0, g_to_x = 0, g_to_y = 0;
int g_glide = 0, g_glide_frames = 0;
std::string g_final_shot;

u32 ButtonNamed(const std::string& name) {
    if (name == "A") return WPAD_BUTTON_A;
    if (name == "B") return WPAD_BUTTON_B;
    if (name == "PLUS") return WPAD_BUTTON_PLUS;
    if (name == "MINUS") return WPAD_BUTTON_MINUS;
    if (name == "1") return WPAD_BUTTON_1;
    if (name == "2") return WPAD_BUTTON_2;
    if (name == "HOME") return WPAD_BUTTON_HOME;
    if (name == "UP") return WPAD_BUTTON_UP;
    if (name == "DOWN") return WPAD_BUTTON_DOWN;
    if (name == "LEFT") return WPAD_BUTTON_LEFT;
    if (name == "RIGHT") return WPAD_BUTTON_RIGHT;
    return 0;
}

u8 Clamp(float v) { return static_cast<u8>(v < 0 ? 0 : v > 255 ? 255 : v); }

// YUYV (two pixels share U and V) to a bottom-up 24-bit BMP.
void Shot(const std::string& path, const void* xfb, int width, int height) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        logf("guiscript: cannot write %s\n", path.c_str());
        return;
    }
    const int row = width * 3, pad = (4 - row % 4) % 4;
    const u32 image = static_cast<u32>((row + pad) * height);
    u8 header[54] = {'B', 'M'};
    const auto put32 = [&](int at, u32 v) {
        for (int i = 0; i < 4; ++i) header[at + i] = static_cast<u8>(v >> (8 * i));
    };
    put32(2, 54 + image);
    put32(10, 54);
    put32(14, 40);
    put32(18, static_cast<u32>(width));
    put32(22, static_cast<u32>(height));
    header[26] = 1;
    header[28] = 24;
    put32(34, image);
    std::fwrite(header, 1, sizeof(header), f);
    const u8* src = static_cast<const u8*>(xfb);
    std::vector<u8> line(static_cast<std::size_t>(row + pad), 0);
    for (int y = height - 1; y >= 0; --y) {
        const u8* p = src + static_cast<std::size_t>(y) * width * 2;
        for (int x = 0; x < width; x += 2) {
            const float u = p[x * 2 + 1] - 128.0f, v = p[x * 2 + 3] - 128.0f;
            for (int k = 0; k < 2; ++k) {
                const float yy = 1.164f * (p[x * 2 + k * 2] - 16.0f);
                u8* o = &line[static_cast<std::size_t>(x + k) * 3];
                o[0] = Clamp(yy + 2.018f * u);             // B
                o[1] = Clamp(yy - 0.391f * u - 0.813f * v); // G
                o[2] = Clamp(yy + 1.596f * v);             // R
            }
        }
        std::fwrite(line.data(), 1, line.size(), f);
    }
    std::fflush(f);
    fsync(fileno(f));  // the FAT too: nothing else may flush it before power-off
    std::fclose(f);
    logf("guiscript: saved %s\n", path.c_str());
}

bool g_fail_launch = false;
bool g_launch_shots = false;

// Runs commands until one needs frames to pass.
void Advance(const void* xfb, int width, int height) {
    while (g_active && g_wait <= 0 && g_press_frames == 0) {
        if (g_next >= g_lines.size()) {
            logf("guiscript: done\n");
            g_active = false;
            g_pointing = false;
            return;
        }
        std::istringstream words(g_lines[g_next++]);
        std::string cmd;
        words >> cmd;
        if (cmd.empty() || cmd[0] == '#') continue;
        if (cmd == "wait") {
            words >> g_wait;
        } else if (cmd == "point") {
            words >> g_x >> g_y;
            g_pointing = true;
        } else if (cmd == "nopoint") {
            g_pointing = false;
        } else if (cmd == "press") {
            std::string name;
            words >> name;
            g_press = ButtonNamed(name);
            g_press_frames = g_press ? 1 : 0;
            g_wait = 2;
        } else if (cmd == "hold") {
            std::string name;
            words >> name;
            const u32 b = ButtonNamed(name);
            g_held_new |= b & ~g_held;
            g_held |= b;
            g_wait = 1;
        } else if (cmd == "release") {
            g_held = 0;
            g_wait = 1;
        } else if (cmd == "glide") {
            g_from_x = g_x;
            g_from_y = g_y;
            words >> g_to_x >> g_to_y >> g_glide_frames;
            if (g_glide_frames < 1) g_glide_frames = 1;
            g_glide = 0;
            g_wait = g_glide_frames;
        } else if (cmd == "launchshots") {
            g_launch_shots = true;
        } else if (cmd == "failnext") {
            g_fail_launch = true;
        } else if (cmd == "crash") {
            logf("guiscript: crashing on purpose\n");
            asm volatile("trap");  // a program exception, in Dolphin too
        } else if (cmd == "finalshot") {
            words >> g_final_shot;
        } else if (cmd == "mem") {
            std::string label;
            words >> label;
            mem::LogUsage(label.empty() ? "guiscript" : label.c_str());
        } else if (cmd == "shot") {
            std::string path;
            words >> path;
            if (xfb) Shot(path, xfb, width, height);
        } else {
            logf("guiscript: unknown command '%s'\n", cmd.c_str());
        }
    }
}

}  // namespace

bool GuiScriptFailLaunch() { return g_fail_launch; }

void GuiScriptLaunchShot(int percent, const void* xfb, int width, int height) {
    if (g_launch_shots) Shot("sd:/riftwii/launch_" + std::to_string(percent) + ".bmp", xfb, width, height);
}

void GuiScriptCrashShot(const void* xfb, int width, int height) {
    if (!g_lines.empty()) Shot("sd:/riftwii/crashscreen.bmp", xfb, width, height);
}

bool GuiScriptLoad(const char* path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        g_lines.push_back(line);
    }
    g_active = !g_lines.empty();
    logf("guiscript: %u line(s) from %s\n", static_cast<unsigned>(g_lines.size()), path);
    return g_active;
}


void GuiScriptApply() {
    if (!g_active) return;
    WPADData* w = userInput[0].wpad;
    if (!w) return;
    if (g_glide_frames > 0) {
        ++g_glide;
        const float t = static_cast<float>(g_glide) / g_glide_frames;
        g_x = g_from_x + (g_to_x - g_from_x) * t;
        g_y = g_from_y + (g_to_y - g_from_y) * t;
        if (g_glide >= g_glide_frames) g_glide_frames = 0;
    }
    if (g_pointing) {
        w->ir.valid = 1;
        w->ir.x = g_x;
        w->ir.y = g_y;
        w->ir.angle = 0;
    } else {
        w->ir.valid = 0;
    }
    w->btns_d = g_held_new;
    w->btns_h = g_held;
    g_held_new = 0;
    if (g_press_frames > 0) {
        w->btns_d |= g_press;
        w->btns_h |= g_press;
        --g_press_frames;
    }
}

void GuiScriptFinalShot(const void* xfb, int width, int height) {
    if (g_final_shot.empty() || !xfb) return;
    Shot(g_final_shot, xfb, width, height);
    g_final_shot.clear();
}

void GuiScriptAfterFrame(const void* xfb, int width, int height) {
    if (!g_active) return;
    if (g_wait > 0) --g_wait;
    Advance(xfb, width, height);
}

}  // namespace riftwii::wii
