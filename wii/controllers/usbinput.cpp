// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2026 quatric (libwiiinput) <https://github.com/quatric/libwiiinput>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "usbinput.hpp"

#include <gccore.h>
#include <ogc/usbmouse.h>
#include <wiiuse/wpad.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "libwiigui/gui.h"
#include "log.hpp"
#include "pentablet.h"
#include "skin.hpp"
#include "video.h"

namespace riftwii::wii {
namespace {

constexpr int kChannels = 4;
// A Remote's pointer moving this far (menu units) takes the pointer back
// from the mouse or the tablet, as it does from a pad (input.cpp kRemoteMove).
constexpr float kRemoteMove = 40.0f;
// A hovering pen moving this far (menu units) takes the pointer; less is
// the pen's jitter.
constexpr float kPenMove = 4.0f;

bool g_allowed = false;  // UsbInputMenuAllowStart
bool g_ended = false;    // UsbInputMenuEnd
bool g_started = false;
bool g_mouse_started = false;
bool g_tablet_started = false;
bool g_tablet_waiting = false;  // TB_Init refused: an earlier scan was still stuck

// What the logs said last, so each change is one line.
bool g_said_keyboard = false, g_said_mouse = false, g_said_tablet = false;
bool g_said_udraw[kChannels] = {};

// The keyboard's pad buttons last frame (PAD_* bits), for btns_u.
u16 g_keyboard_held = 0;
// Channel 1's pad buttons pressed this frame by the GameCube port and the
// adapter, and by the keyboard: UpdatePadPointers then gave the channel to
// a pad, and the mouse lets go of the pointer.
u16 g_pad_pressed = 0;

volatile int g_typing = 0;  // GuiSearchKeys open
int g_pages = 0;            // this frame's, until Home takes them (UsbInputTakePages)

// Who has player 1's pointer among the USB devices.
enum class Owner { None, Mouse, Tablet };
Owner g_owner = Owner::None;
float g_x = 320, g_y = 240;          // the mouse's pointer, menu units
bool g_anchored = false;             // where the Remote pointed when the USB pointer took over
float g_anchor_x = 0, g_anchor_y = 0;
u8 g_mouse_buttons = 0;              // held last frame
u8 g_tablet_buttons = 0;             // the USB tablet's, held last frame
bool g_pen_was_near = false;         // the USB tablet's pen in range last frame
float g_pen_x = 0, g_pen_y = 0;      // where the pen last took or moved the pointer

// The uDraws, per Wii Remote channel: the latest sample, and its buttons
// last frame.
tb_sample g_udraw[kChannels];
bool g_udraw_seen[kChannels] = {};
u8 g_udraw_buttons[kChannels] = {};
// The USB tablet's latest sample.
tb_sample g_tablet;
bool g_tablet_seen = false;

int TabletLog(const char* format, ...) {
    char line[200];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    const char* text = line;
    while (*text == ' ') ++text;
    logf("Pen tablet: %s", text);
    return 0;
}

// The tablet driver's buffers, in MEM2: MEM1 is the heap's.
void* TabletAlloc(u32 bytes) { return skin::Mem2Alloc(bytes); }

// Starts the tablet driver. While an earlier, stuck scan has not returned,
// TB_Init refuses, and each frame tries again until it has.
void StartTablet() {
    const s32 tablet = TB_Init();
    g_tablet_started = tablet == TB_OK;
    if (g_tablet_started && g_tablet_waiting) logf("Pen tablet: the earlier scan returned, started\n");
    else if (tablet == TB_ESTATE && !g_tablet_waiting) logf("Pen tablet: not started yet (an earlier scan is still waiting)\n");
    else if (!g_tablet_started && tablet != TB_ESTATE) logf("Pen tablet: its thread did not start\n");
    g_tablet_waiting = tablet == TB_ESTATE;
}

void Start() {
    g_started = true;
    if (UsbKeyboardStart()) logf("USB keyboard: looking for one\n");
    else logf("USB keyboard: not started (libwiikeyboard has no IPC heap, or an earlier read is still waiting)\n");
    // libogc's mouse driver follows plugging in by itself.
    g_mouse_started = MOUSE_Init() >= 0;
    if (!g_mouse_started) logf("USB mouse: libogc's driver did not start\n");
    TB_SetLog(TabletLog);
    TB_SetAlloc(TabletAlloc);
    StartTablet();
}

// One line each time something is plugged in or taken out.
void LogChanges() {
    const bool keyboard = UsbKeyboardConnected();
    if (keyboard != g_said_keyboard) logf("USB keyboard %s\n", keyboard ? "connected" : "disconnected");
    g_said_keyboard = keyboard;
    const bool mouse = g_mouse_started && MOUSE_IsConnected();
    if (mouse != g_said_mouse) logf("USB mouse %s\n", mouse ? "connected" : "disconnected");
    g_said_mouse = mouse;
    tb_info info;
    const bool tablet = g_tablet_started && TB_GetInfo(&info) && info.vid != 0;
    if (tablet != g_said_tablet) {
        if (tablet) logf("Pen tablet connected: %s (%04x:%04x)\n", info.name, info.vid, info.pid);
        else logf("Pen tablet disconnected\n");
    }
    g_said_tablet = tablet;
    for (int c = 0; c < kChannels; ++c) {
        const bool udraw = TB_UDrawConnected(c);
        if (udraw != g_said_udraw[c]) logf("uDraw GameTablet %s Wii Remote %d\n", udraw ? "on" : "off", c + 1);
        g_said_udraw[c] = udraw;
    }
}

struct KeyButton {
    u8 code;
    u16 pad;
};

// The keyboard as a GameCube controller (the menu's pad bits): the arrows,
// A, B and Start as named above, 1 as the Remote's 1 (Y on a GameCube
// controller everywhere in the menu).
constexpr KeyButton kKeyButtons[] = {
    {key::kUp, PAD_BUTTON_UP},       {key::kDown, PAD_BUTTON_DOWN},   {key::kLeft, PAD_BUTTON_LEFT},
    {key::kRight, PAD_BUTTON_RIGHT}, {key::kEnter, PAD_BUTTON_A},     {key::kPadEnter, PAD_BUTTON_A},
    {key::kEscape, PAD_BUTTON_B},    {key::kBackspace, PAD_BUTTON_B}, {key::kHome, PAD_BUTTON_START},
    {key::kF1, PAD_BUTTON_START},    {key::kOne, PAD_BUTTON_Y},
};
constexpr u16 kArrows = PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT;

void KeyboardToPad() {
    UsbKeyboardPoll();
    const bool typing = g_typing > 0;
    // Outside the typing the presses are not wanted as text: a search
    // opened later must not start with them.
    if (!typing) UsbKeyboardFlush();
    u16 held = 0, pressed = 0;
    for (const KeyButton& k : kKeyButtons) {
        if (typing && !(k.pad & kArrows)) continue;
        if (UsbKeyHeld(k.code)) held |= k.pad;
        if (UsbKeyPressed(k.code)) pressed |= k.pad;
    }
    if (!typing) {
        if (UsbKeyPressed(key::kPageUp)) --g_pages;
        if (UsbKeyPressed(key::kPageDown)) ++g_pages;
    }
    PADData& pad = userInput[0].pad;
    g_pad_pressed = static_cast<u16>(pad.btns_d | pressed);
    pad.btns_h |= held;
    pad.btns_d |= pressed;
    pad.btns_u |= g_keyboard_held & ~held;
    g_keyboard_held = held;
}

// A tablet sample in menu units, over the whole screen.
void TabletPoint(const tb_sample& s, float& x, float& y) {
    f32 vx, vy, vw, vh;
    Menu_VisibleArea(&vx, &vy, &vw, &vh);
    int sx = 0, sy = 0;
    TB_ToScreen(&s, static_cast<int>(vw), static_cast<int>(vh), &sx, &sy);
    x = vx + sx;
    y = vy + sy;
}

void DrainTablets() {
    tb_sample s;
    int guard = 0;
    while (guard++ < 512 && TB_Pop(&s)) {
        if (s.finger) continue;  // a Bamboo's touch surface: the pen only
        if (s.wiimote >= 0 && s.wiimote < kChannels) {
            g_udraw[s.wiimote] = s;
            g_udraw_seen[s.wiimote] = true;
        } else if (s.wiimote < 0) {
            g_tablet = s;
            g_tablet_seen = true;
        }
    }
    for (int c = 0; c < kChannels; ++c)
        if (!TB_UDrawConnected(c)) g_udraw_seen[c] = false;
}

// A button of the mouse or a pen as the menu's A and B, on `chan`.
void PenButtons(int chan, bool a_now, bool a_before, bool b_now, bool b_before) {
    PADData& pad = userInput[chan].pad;
    const auto apply = [&pad](u16 bit, bool now, bool before) {
        if (now) pad.btns_h |= bit;
        if (now && !before) pad.btns_d |= bit;
        if (!now && before) pad.btns_u |= bit;
    };
    apply(PAD_BUTTON_A, a_now, a_before);
    apply(PAD_BUTTON_B, b_now, b_before);
}

void ShowPointer(WPADData* w, float x, float y) {
    w->ir.valid = 1;
    w->ir.x = x;
    w->ir.y = y;
    w->ir.angle = 0;
}

// The USB pointer lets go when player 1 really uses something else: a
// Remote's button, a pad's or the keyboard's buttons (UpdatePadPointers
// gave the channel to them), or the Remote's pointer moving.
bool OtherControlUsed(const WPADData* w) {
    u32 type = 0;
    const bool remote = WPAD_Probe(0, &type) == WPAD_ERR_NONE;
    if (remote && (w->btns_d & 0xFFFF)) return true;
    if (g_pad_pressed) return true;
    if (remote && w->ir.valid) {
        if (!g_anchored) {
            g_anchor_x = w->ir.x;
            g_anchor_y = w->ir.y;
            g_anchored = true;
        } else if (std::hypot(w->ir.x - g_anchor_x, w->ir.y - g_anchor_y) > kRemoteMove) {
            return true;
        }
    } else {
        g_anchored = false;
    }
    return false;
}

void Take(Owner owner, const WPADData* w) {
    if (g_owner == owner) return;
    // The mouse starts where player 1's pointer is.
    if (owner == Owner::Mouse && w->ir.valid) {
        g_x = w->ir.x;
        g_y = w->ir.y;
    }
    g_owner = owner;
    g_anchored = false;
}

void MouseAndTablet() {
    WPADData* w = userInput[0].wpad;
    if (!w) return;
    int dx = 0, dy = 0, wheel = 0;
    u8 buttons = g_mouse_buttons;
    if (g_mouse_started && MOUSE_IsConnected()) {
        mouse_event ev;
        int guard = 0;
        while (guard++ < 64 && MOUSE_GetEvent(&ev) > 0) {
            dx += ev.rx;
            dy += ev.ry;
            wheel += ev.rz;
            buttons = ev.button & 7;
        }
    } else {
        buttons = 0;
    }
    const bool mouse_used = dx || dy || (buttons & ~g_mouse_buttons);
    const bool pen_near = g_tablet_seen && g_tablet.in_range;
    // The pen takes the pointer when it comes into range, moves or presses,
    // not while it only hovers, so a Remote can take it back from a still pen.
    bool pen_used = false;
    if (pen_near) {
        float px, py;
        TabletPoint(g_tablet, px, py);
        pen_used = !g_pen_was_near || std::hypot(px - g_pen_x, py - g_pen_y) > kPenMove ||
                   (g_tablet.buttons & ~g_tablet_buttons);
        if (pen_used) {
            g_pen_x = px;
            g_pen_y = py;
        }
    }
    g_pen_was_near = pen_near;

    if (g_owner != Owner::None && OtherControlUsed(w)) g_owner = Owner::None;
    if (pen_used) Take(Owner::Tablet, w);
    else if (mouse_used) Take(Owner::Mouse, w);
    else if (g_owner == Owner::Tablet && !pen_near) g_owner = Owner::None;  // the pen went away

    // A wheel notch away from the player is a page back, as on a computer.
    g_pages += wheel;

    f32 vx, vy, vw, vh;
    Menu_VisibleArea(&vx, &vy, &vw, &vh);
    if (g_owner == Owner::Mouse) {
        g_x = std::fmin(std::fmax(g_x + dx, vx), vx + vw - 1);
        g_y = std::fmin(std::fmax(g_y + dy, vy), vy + vh - 1);
        ShowPointer(w, g_x, g_y);
    } else if (g_owner == Owner::Tablet) {
        float x, y;
        TabletPoint(g_tablet, x, y);
        ShowPointer(w, x, y);
    }
    // Clicks count whoever points, so a click right after a Remote moved
    // still lands; the pointer follows the device that moved last.
    PenButtons(0, buttons & 1, g_mouse_buttons & 1, buttons & 2, g_mouse_buttons & 2);
    g_mouse_buttons = buttons;
    const u8 pen = pen_near ? g_tablet.buttons : 0;
    PenButtons(0, pen & TB_BUTTON_TIP, g_tablet_buttons & TB_BUTTON_TIP, pen & TB_BUTTON_BARREL,
               g_tablet_buttons & TB_BUTTON_BARREL);
    g_tablet_buttons = pen;
}

void UDraws() {
    for (int c = 0; c < kChannels; ++c) {
        WPADData* w = userInput[c].wpad;
        const bool near = g_udraw_seen[c] && g_udraw[c].in_range;
        const u8 buttons = near ? g_udraw[c].buttons : 0;
        if (w && near) {
            float x, y;
            TabletPoint(g_udraw[c], x, y);
            ShowPointer(w, x, y);
        }
        PenButtons(c, buttons & TB_BUTTON_TIP, g_udraw_buttons[c] & TB_BUTTON_TIP, buttons & TB_BUTTON_BARREL,
                   g_udraw_buttons[c] & TB_BUTTON_BARREL);
        g_udraw_buttons[c] = buttons;
    }
}

}  // namespace

void UsbInputMenuAllowStart() { g_allowed = true; }

void UsbInputStop() {
    if (!g_started) return;
    g_started = false;
    const bool keyboard_closed = UsbKeyboardStop();
    if (g_mouse_started) MOUSE_Deinit();
    g_mouse_started = false;
    if (g_tablet_started) TB_Shutdown();
    g_tablet_started = false;
    g_tablet_waiting = false;
    g_owner = Owner::None;
    g_keyboard_held = 0;
    g_mouse_buttons = g_tablet_buttons = 0;
    g_pen_was_near = false;
    g_tablet_seen = false;
    for (int c = 0; c < kChannels; ++c) {
        g_udraw_seen[c] = false;
        g_udraw_buttons[c] = 0;
    }
    logf("USB keyboard, mouse and pen tablet stopped%s\n",
         keyboard_closed ? "" : " (the keyboard's read was still waiting: left to the next IOS reload)");
}

void UsbInputMenuEnd() {
    g_ended = true;
    UsbInputStop();
}

void UsbInputIosReloaded() { UsbKeyboardIosReloaded(); }

void UsbInputBeforePointers() {
    if (!g_started && g_allowed && !g_ended) Start();
    g_pad_pressed = 0;
    g_pages = 0;  // a page asked for while Home was not shown is not turned later
    if (!g_started) return;
    if (g_tablet_waiting) StartTablet();
    LogChanges();
    KeyboardToPad();
}

void UsbInputAfterPointers() {
    if (!g_started) return;
    DrainTablets();
    MouseAndTablet();
    UDraws();
}

int UsbInputTakePages() {
    const int pages = g_pages;
    g_pages = 0;
    return pages;
}

void UsbInputTyping(bool on) {
    g_typing = g_typing + (on ? 1 : -1);
    if (g_typing < 0) g_typing = 0;
}

bool UsbInputTyped(KeyPress& out) { return g_typing > 0 && UsbKeyboardPop(out); }

bool UsbInputKeyboardConnected() { return UsbKeyboardConnected(); }

bool UsbInputUDrawConnected() {
    for (int c = 0; c < kChannels; ++c)
        if (TB_UDrawConnected(c)) return true;
    return false;
}

std::string UsbInputDescribe() {
    std::string out;
    const auto add = [&out](const std::string& s) { out += (out.empty() ? "" : ", ") + s; };
    if (UsbKeyboardConnected()) add("USB keyboard");
    if (g_mouse_started && MOUSE_IsConnected()) add("USB mouse");
    tb_info info;
    if (g_tablet_started && TB_GetInfo(&info) && info.vid != 0) add(std::string("pen tablet ") + info.name);
    for (int c = 0; c < kChannels; ++c)
        if (TB_UDrawConnected(c)) add("uDraw on Wii Remote " + std::to_string(c + 1));
    return out;
}

}  // namespace riftwii::wii
