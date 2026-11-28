// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// GameCube controllers as a pointing Wii Remote (runtime/rtgcwii.h with
// RT_PAD_FORM_POINTING, #587): the Wii Remote held upright and pointed at
// the screen, no Nunchuk. #587's pointing layout (22 Nov): A A, B B, X 1, Y
// 2, Z -, Start +, the D-pad as it is; the control stick moving the pointer
// while the game has the camera on (and nothing while it's off), the
// C-stick tilting the remote any way, and R shaking it. This test compiles
// rtgcwii.c and rthome.c itself, with the form's define, as the pad builds
// that carry it do.
#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" {
#include "rtgcwii.h"
#include "rthome.h"
}

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uint32_t kGcLeft = 0x0001, kGcRight = 0x0002, kGcDown = 0x0004, kGcUp = 0x0008;
constexpr std::uint32_t kGcZ = 0x0010, kGcR = 0x0020, kGcL = 0x0040;
constexpr std::uint32_t kGcA = 0x0100, kGcB = 0x0200, kGcX = 0x0400, kGcY = 0x0800, kGcStart = 0x1000;
constexpr std::uint32_t kWrLeft = 0x0001, kWrRight = 0x0002, kWrDown = 0x0004, kWrUp = 0x0008, kWrPlus = 0x0010;
constexpr std::uint32_t kWr2 = 0x0100, kWr1 = 0x0200, kWrB = 0x0400, kWrA = 0x0800, kWrMinus = 0x1000;
constexpr std::uint32_t kGrace = 300;
constexpr int kOneG2 = RTHOME_AXIS_FULL * RTHOME_AXIS_FULL;
constexpr std::uint32_t kFrame = RTGCWII_TICKS_PER_FRAME;

struct Pads {
    std::uint8_t s[48];
    Pads() {
        std::memset(s, 0, sizeof(s));
        for (int i = 0; i < 4; ++i) s[i * 12 + 10] = 0xFF;
    }
    std::uint8_t* plug(int port, std::uint32_t buttons = 0) {
        std::uint8_t* p = s + port * 12;
        p[0] = static_cast<std::uint8_t>(buttons >> 8);
        p[1] = static_cast<std::uint8_t>(buttons);
        p[10] = 0;
        return p;
    }
};

rtgcwii fresh() {
    rtgcwii w;
    std::memset(&w, 0, sizeof(w));
    w.slot.grace = kGrace;
    w.shape.pointer_show = 2000;
    return w;
}

std::uint32_t map(std::uint32_t buttons, int stick_x = 0, int stick_y = 0) {
    std::uint8_t pad[12] = {};
    pad[0] = static_cast<std::uint8_t>(buttons >> 8);
    pad[1] = static_cast<std::uint8_t>(buttons);
    pad[2] = static_cast<std::uint8_t>(static_cast<std::int8_t>(stick_x));
    pad[3] = static_cast<std::uint8_t>(static_cast<std::int8_t>(stick_y));
    return rtgcwii_map_pointing(pad);
}

std::uint8_t s8(int v) { return static_cast<std::uint8_t>(static_cast<std::int8_t>(v)); }

// The buttons, one by one: A A, B B, X 1, Y 2, Z -, Start +; L and R
// nothing (R is the shake); the D-pad as it is (not turned).
void TestButtons() {
    const struct {
        std::uint32_t gc, wii;
    } cases[] = {{kGcA, kWrA},       {kGcB, kWrB},       {kGcX, kWr1},       {kGcY, kWr2},
                 {kGcZ, kWrMinus},   {kGcStart, kWrPlus}, {kGcL, 0u},         {kGcR, 0u},
                 {kGcLeft, kWrLeft}, {kGcRight, kWrRight}, {kGcUp, kWrUp},    {kGcDown, kWrDown}};
    for (const auto& c : cases) EXPECT_EQ(map(c.gc), c.wii);
    EXPECT_EQ(map(kGcA | kGcUp | kGcX), kWrA | kWrUp | kWr1);
    // The control stick is never the D-pad in this form, however far.
    EXPECT_EQ(map(0, 72, 0), 0u);
    EXPECT_EQ(map(0, 0, -72), 0u);
}

// The camera on (the game's pointer screens): the control stick moves the
// pointer, RTGCWII_POINTER_STEP a frame at full push, up on the stick up on
// the screen, and it is shown; inside the stick's dead zone it stays put.
void TestPointer() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0);
    rtgcwii_camera(&w, 0, 1);
    s[2] = 72;  // full right
    rtgcwii_pads(&w, p.s, kFrame);      // the first sample counts as a frame
    rtgcwii_pads(&w, p.s, 2 * kFrame);
    rtshape_out f;
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.pointer_shown, 1);
    EXPECT_EQ(f.pointer[0], 2 * RTGCWII_POINTER_STEP);
    EXPECT_EQ(f.pointer[1], 0);
    s[2] = 0;
    s[3] = 72;  // full up: KPAD's y grows downward
    rtgcwii_pads(&w, p.s, 3 * kFrame);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.pointer[0], 2 * RTGCWII_POINTER_STEP);
    EXPECT_EQ(f.pointer[1], -RTGCWII_POINTER_STEP);
    s[3] = 8;   // a resting stick's drift
    s[2] = s8(-9);
    rtgcwii_pads(&w, p.s, 4 * kFrame);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.pointer[0], 2 * RTGCWII_POINTER_STEP);
    EXPECT_EQ(f.pointer[1], -RTGCWII_POINTER_STEP);
    // Held over: it stops at the screen's edge.
    s[2] = 72;
    s[3] = 0;
    for (std::uint32_t t = 0; t < 100; ++t) rtgcwii_pads(&w, p.s, (5 + t) * kFrame);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.pointer[0], RTGCWII_POINTER_EDGE);
    // The C-stick never moves it.
    s[2] = 0;
    s[4] = s8(-72);
    rtgcwii_pads(&w, p.s, 106 * kFrame);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.pointer[0], RTGCWII_POINTER_EDGE);
}

// By time, not by sample: a game that reads twice a frame moves it no
// faster than one that reads once, and a long gap counts as
// RTGCWII_POINTER_GAP_FRAMES frames.
void TestPointerSpeed() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0);
    rtgcwii_camera(&w, 0, 1);
    rtgcwii_pads(&w, p.s, kFrame);  // plugged in, at rest
    s[2] = 72;
    for (std::uint32_t t = 1; t <= 20; ++t) rtgcwii_pads(&w, p.s, kFrame + t * (kFrame / 2));  // ten frames
    rtshape_out f;
    rtgcwii_pointing(&w, 0, &f);
    // Each step rounds down (under one of 8192 across the screen a sample).
    EXPECT_TRUE(f.pointer[0] <= 10 * RTGCWII_POINTER_STEP && f.pointer[0] > 10 * RTGCWII_POINTER_STEP - 20);
    rtgcwii w2 = fresh();
    Pads p2;
    std::uint8_t* s2 = p2.plug(0);
    rtgcwii_camera(&w2, 0, 1);
    rtgcwii_pads(&w2, p2.s, kFrame);
    s2[2] = 72;
    for (std::uint32_t t = 1; t <= 10; ++t) rtgcwii_pads(&w2, p2.s, kFrame + t * kFrame);
    rtgcwii_pointing(&w2, 0, &f);
    EXPECT_EQ(f.pointer[0], 10 * RTGCWII_POINTER_STEP);
    // Ten frames with no sample: four frames' worth.
    rtgcwii_pads(&w2, p2.s, 21 * kFrame);
    rtgcwii_pointing(&w2, 0, &f);
    EXPECT_EQ(f.pointer[0], (10 + static_cast<int>(RTGCWII_POINTER_GAP_FRAMES)) * RTGCWII_POINTER_STEP);
    // Half a push, a frame: half a step (rounded down).
    s2[2] = s8(41);
    const int before = f.pointer[0];
    rtgcwii_pads(&w2, p2.s, 22 * kFrame);
    rtgcwii_pointing(&w2, 0, &f);
    EXPECT_TRUE(f.pointer[0] - before > RTGCWII_POINTER_STEP / 2 - 4 && f.pointer[0] - before < RTGCWII_POINTER_STEP / 2 + 4);
}

// The camera off: the control stick does nothing, the pointer is hidden
// and not reported, and it doesn't move behind the game's back.
void TestCameraOff() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0);
    s[2] = 72;
    s[3] = 72;
    rtgcwii_pads(&w, p.s, kFrame);
    rtgcwii_pads(&w, p.s, 2 * kFrame);
    rtshape_out f;
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.pointer_shown, 0);
    EXPECT_EQ(f.pointer[0], 0);
    EXPECT_EQ(f.hold, 0u);
    EXPECT_EQ(f.acc[1], -RTHOME_AXIS_FULL);  // nor does it tilt
    rtgcwii_camera(&w, 0, 1);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.pointer_shown, 1);
    EXPECT_EQ(f.pointer[0], 0);  // still where it was
    EXPECT_EQ(f.pointer[1], 0);
}

// Tilt, any way and whatever the camera: the C-stick right rolls it right
// (gravity from -Y towards +X), up dips its nose (towards +Z); a diagonal
// goes no further than a full push one way; gravity's length stays 1 g;
// centred, the remote lies still.
void TestTilt() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0);
    rtgcwii_pads(&w, p.s, 1);
    rtshape_out f;
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.acc[0], 0);
    EXPECT_EQ(f.acc[1], -RTHOME_AXIS_FULL);
    EXPECT_EQ(f.acc[2], 0);
    EXPECT_EQ(f.acc_value, RTHOME_AXIS_FULL);
    const struct {
        int cx, cy, x, z;
    } cases[] = {{72, 0, RTGCWII_TILT_SIN, 0},
                 {-72, 0, -RTGCWII_TILT_SIN, 0},
                 {0, 72, 0, RTGCWII_TILT_SIN},
                 {0, -72, 0, -RTGCWII_TILT_SIN}};
    std::uint32_t t = 17;
    for (const auto& c : cases) {
        s[4] = s8(c.cx);
        s[5] = s8(c.cy);
        rtgcwii_pads(&w, p.s, t);
        t += 16;
        rtgcwii_pointing(&w, 0, &f);
        EXPECT_EQ(f.acc[0], c.x);
        EXPECT_EQ(f.acc[2], c.z);
        EXPECT_TRUE(f.acc[1] < 0);
        const int len2 = f.acc[0] * f.acc[0] + f.acc[1] * f.acc[1] + f.acc[2] * f.acc[2];
        EXPECT_TRUE(len2 <= kOneG2 && len2 > (RTHOME_AXIS_FULL - 2) * (RTHOME_AXIS_FULL - 2));
        EXPECT_EQ(f.acc_value, RTHOME_AXIS_FULL);
    }
    // A full diagonal: both ways at once, no further than a full push.
    s[4] = 72;
    s[5] = 72;
    rtgcwii_pads(&w, p.s, t);
    t += 16;
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_TRUE(f.acc[0] > 0 && f.acc[2] > 0);
    EXPECT_EQ(f.acc[0], f.acc[2]);
    const int tilt2 = f.acc[0] * f.acc[0] + f.acc[2] * f.acc[2];
    EXPECT_TRUE(tilt2 <= RTGCWII_TILT_SIN * RTGCWII_TILT_SIN && tilt2 > (RTGCWII_TILT_SIN - 2) * (RTGCWII_TILT_SIN - 2));
    const int len2 = tilt2 + f.acc[1] * f.acc[1];
    EXPECT_TRUE(len2 <= kOneG2 && len2 > (RTHOME_AXIS_FULL - 2) * (RTHOME_AXIS_FULL - 2));
    // Every C-stick position: the tilt never past a full push (the root
    // rounded up; fork #23).
    for (int cx = -80; cx <= 80; ++cx) {
        for (int cy = -80; cy <= 80; ++cy) {
            s[4] = s8(cx);
            s[5] = s8(cy);
            rtgcwii_pads(&w, p.s, t);
            t += 16;
            rtgcwii_pointing(&w, 0, &f);
            EXPECT_TRUE(f.acc[0] * f.acc[0] + f.acc[2] * f.acc[2] <= RTGCWII_TILT_SIN * RTGCWII_TILT_SIN);
        }
    }
    // Half right: about half as far.
    s[4] = s8(36);
    s[5] = 0;
    rtgcwii_pads(&w, p.s, t);
    t += 16;
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_TRUE(f.acc[0] > 0 && f.acc[0] < RTGCWII_TILT_SIN);
    // With the camera on it still tilts (the pointer is the control stick's).
    rtgcwii_camera(&w, 0, 1);
    s[4] = 72;
    rtgcwii_pads(&w, p.s, t);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.acc[0], RTGCWII_TILT_SIN);
    EXPECT_EQ(f.pointer_shown, 1);
    EXPECT_EQ(f.pointer[0], 0);
    // The Nunchuk's fields stay at rest in this form.
    EXPECT_EQ(f.stick[0], 0);
    EXPECT_EQ(f.fs_acc[1], -RTHOME_AXIS_FULL);
}

// The horizon (the sensor bar's line as the camera sees it) turns with the
// roll alone: level at rest and with the nose dipped, rolled right the same
// way as the acceleration's x, its length 1.
void TestHorizon() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0);
    rtgcwii_pads(&w, p.s, 1);
    rtshape_out f;
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.horizon[0], RTHOME_AXIS_FULL);
    EXPECT_EQ(f.horizon[1], 0);
    s[5] = 72;  // nose down only
    rtgcwii_pads(&w, p.s, 17);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.horizon[0], RTHOME_AXIS_FULL);
    EXPECT_EQ(f.horizon[1], 0);
    s[5] = 0;
    s[4] = 72;  // rolled right
    rtgcwii_pads(&w, p.s, 33);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_TRUE(f.horizon[1] > 0 && f.horizon[0] > 0);
    EXPECT_EQ(f.horizon[1], RTGCWII_TILT_SIN);  // a pure roll: sin over a length of 1 g
    int len2 = f.horizon[0] * f.horizon[0] + f.horizon[1] * f.horizon[1];
    EXPECT_TRUE(len2 <= kOneG2 + 2 * RTHOME_AXIS_FULL && len2 > (RTHOME_AXIS_FULL - 2) * (RTHOME_AXIS_FULL - 2));
    s[4] = s8(-72);
    s[5] = 72;  // rolled left and dipped: still the roll's angle, length 1
    rtgcwii_pads(&w, p.s, 49);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_TRUE(f.horizon[1] < 0);
    len2 = f.horizon[0] * f.horizon[0] + f.horizon[1] * f.horizon[1];
    EXPECT_TRUE(len2 <= kOneG2 + 2 * RTHOME_AXIS_FULL && len2 > (RTHOME_AXIS_FULL - 2) * (RTHOME_AXIS_FULL - 2));
    // No stand-in: level.
    rtgcwii_pointing(&w, 2, &f);
    EXPECT_EQ(f.horizon[0], RTHOME_AXIS_FULL);
    EXPECT_EQ(f.horizon[1], 0);
}

// R (or its analog trigger past a third) shakes it; X (1) and L don't.
void TestShake() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0, kGcX | kGcL);
    s[6] = 255;  // the analog L too
    rtgcwii_pads(&w, p.s, 1);
    rtshape_out f;
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.acc_value, RTHOME_AXIS_FULL);
    EXPECT_EQ(f.hold, kWr1);
    p.plug(0, kGcR);
    s[6] = 0;
    rtgcwii_pads(&w, p.s, 17);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.acc_value, RTGCWII_SHAKE_VALUE);
    EXPECT_EQ(f.acc[0], -RTGCWII_SHAKE_G);
    EXPECT_EQ(f.hold, 0u);
    p.plug(0, 0);
    rtgcwii_pads(&w, p.s, 33);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.acc_value, RTHOME_AXIS_FULL);
    s[7] = 200;  // the analog R pulled
    rtgcwii_pads(&w, p.s, 49);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.acc_value, RTGCWII_SHAKE_VALUE);
    // Shaking wins over tilt (the swing's next count, on its ramp).
    s[4] = 72;
    rtgcwii_pads(&w, p.s, 65);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.acc_speed, RTGCWII_SHAKE_SPEED);
    EXPECT_EQ(f.acc[1], f.acc[0]);
    EXPECT_EQ(f.acc[2], -f.acc[0]);
}

// The words (newly pressed once), and a channel with no stand-in.
void TestWords() {
    rtgcwii w = fresh();
    Pads p;
    p.plug(0, 0);
    rtgcwii_pads(&w, p.s, 1);
    p.plug(0, kGcA | kGcLeft);
    rtgcwii_pads(&w, p.s, 17);
    rtshape_out f;
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.hold, kWrA | kWrLeft);
    EXPECT_EQ(f.trig, kWrA | kWrLeft);
    rtgcwii_pointing(&w, 0, &f);  // a second read in the same sample
    EXPECT_EQ(f.trig, kWrA | kWrLeft);
    rtgcwii_pads(&w, p.s, 33);
    rtgcwii_pointing(&w, 0, &f);
    EXPECT_EQ(f.trig, 0u);
    rtgcwii_pointing(&w, 1, &f);
    EXPECT_EQ(f.hold, 0u);
    EXPECT_EQ(f.acc[1], -RTHOME_AXIS_FULL);
    EXPECT_EQ(f.pointer_shown, 0);
}

}  // namespace

int main() {
    TestButtons();
    TestPointer();
    TestPointerSpeed();
    TestCameraOff();
    TestTilt();
    TestHorizon();
    TestShake();
    TestWords();
    if (g_failures == 0) std::cout << "gcwii_pointing: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
