// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// GameCube controllers as a sideways Wii Remote (runtime/rtgcwii.h with
// RT_PAD_FORM_SIDEWAYS, #587): #587's default sideways layout, the D-pad
// (and the control stick past half way) turned as the screen shows it held
// sideways, the C-stick tilting the remote while the game has the camera off
// and moving the pointer while it's on, and R shaking it. This test compiles
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
    return rtgcwii_map_sideways(pad);
}

// The buttons, one by one: A 2, B 1, X A, Y B, Z -, Start +; L and R
// nothing (R is the shake).
void TestButtons() {
    const struct {
        std::uint32_t gc, wii;
    } cases[] = {{kGcA, kWr2},    {kGcB, kWr1},         {kGcX, kWrA}, {kGcY, kWrB},
                 {kGcZ, kWrMinus}, {kGcStart, kWrPlus}, {kGcL, 0u},   {kGcR, 0u}};
    for (const auto& c : cases) EXPECT_EQ(map(c.gc), c.wii);
}

// The D-pad turned as the screen shows it held sideways (D-pad on the
// left): screen left is the remote's Up, right its Down, up its Right,
// down its Left; the control stick past half way the same.
void TestTurnedDpad() {
    EXPECT_EQ(map(kGcLeft), kWrUp);
    EXPECT_EQ(map(kGcRight), kWrDown);
    EXPECT_EQ(map(kGcUp), kWrRight);
    EXPECT_EQ(map(kGcDown), kWrLeft);
    EXPECT_EQ(map(0, -72, 0), kWrUp);
    EXPECT_EQ(map(0, 72, 0), kWrDown);
    EXPECT_EQ(map(0, 0, 72), kWrRight);
    EXPECT_EQ(map(0, 0, -72), kWrLeft);
    EXPECT_EQ(map(0, 72, 72), kWrDown | kWrRight);  // a diagonal
    EXPECT_EQ(map(0, 20, 0), 0u);                    // under half way: nothing
}

// Tilt: the C-stick held right steers right (gravity from -Y towards -Z),
// left the other way; gravity's length stays 1 g; centred, the remote lies
// still.
void TestTilt() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0);
    rtgcwii_pads(&w, p.s, 1);
    rtshape_out f;
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_EQ(f.acc[0], 0);
    EXPECT_EQ(f.acc[1], -RTHOME_AXIS_FULL);
    EXPECT_EQ(f.acc[2], 0);
    s[4] = 72;  // C-stick full right
    rtgcwii_pads(&w, p.s, 17);
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_EQ(f.acc[0], 0);
    EXPECT_EQ(f.acc[2], -RTGCWII_TILT_SIN);
    EXPECT_TRUE(f.acc[1] < 0);
    const int len2 = f.acc[1] * f.acc[1] + f.acc[2] * f.acc[2];
    EXPECT_TRUE(len2 <= RTHOME_AXIS_FULL * RTHOME_AXIS_FULL && len2 > (RTHOME_AXIS_FULL - 1) * (RTHOME_AXIS_FULL - 1));
    EXPECT_EQ(f.acc_value, RTHOME_AXIS_FULL);
    EXPECT_EQ(f.pointer_shown, 0);
    s[4] = static_cast<std::uint8_t>(-72);  // full left
    rtgcwii_pads(&w, p.s, 33);
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_EQ(f.acc[2], RTGCWII_TILT_SIN);
    s[4] = static_cast<std::uint8_t>(-36);  // half left: about half as far
    rtgcwii_pads(&w, p.s, 49);
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_TRUE(f.acc[2] > 0 && f.acc[2] < RTGCWII_TILT_SIN);
    // The Nunchuk's fields stay at rest in this form.
    EXPECT_EQ(f.stick[0], 0);
    EXPECT_EQ(f.fs_acc[1], -RTHOME_AXIS_FULL);
}

// The camera on (a pointer menu): the C-stick moves the pointer instead,
// and the remote lies still; off again: tilt again.
void TestCamera() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0);
    rtgcwii_camera(&w, 0, 1);
    s[4] = 72;
    rtgcwii_pads(&w, p.s, 1);
    rtgcwii_pads(&w, p.s, 17);
    rtshape_out f;
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_EQ(f.pointer_shown, 1);
    EXPECT_EQ(f.pointer[0], 2 * RTGCWII_POINTER_STEP);
    EXPECT_EQ(f.acc[2], 0);
    EXPECT_EQ(f.acc[1], -RTHOME_AXIS_FULL);
    rtgcwii_camera(&w, 0, 0);
    rtgcwii_pads(&w, p.s, 33);
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_EQ(f.pointer_shown, 0);
    EXPECT_EQ(f.acc[2], -RTGCWII_TILT_SIN);
    EXPECT_EQ(f.pointer[0], 0);  // hidden: not reported
}

// R (or its analog trigger past a third) shakes it; X doesn't.
void TestShake() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0, kGcX);
    rtgcwii_pads(&w, p.s, 1);
    rtshape_out f;
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_EQ(f.acc_value, RTHOME_AXIS_FULL);
    p.plug(0, kGcR);
    rtgcwii_pads(&w, p.s, 17);
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_EQ(f.acc_value, RTGCWII_SHAKE_VALUE);
    EXPECT_EQ(f.acc[0], -RTGCWII_SHAKE_G);
    p.plug(0, 0);
    rtgcwii_pads(&w, p.s, 33);
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_EQ(f.acc_value, RTHOME_AXIS_FULL);
    s[7] = 200;  // the analog R pulled
    rtgcwii_pads(&w, p.s, 49);
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_EQ(f.acc_value, RTGCWII_SHAKE_VALUE);
    // Shaking wins over tilt (the swing's next count, on its ramp).
    s[4] = 72;
    rtgcwii_pads(&w, p.s, 65);
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_EQ(f.acc_speed, RTGCWII_SHAKE_SPEED);
    EXPECT_EQ(f.acc[1], f.acc[0]);
    EXPECT_EQ(f.acc[2], -f.acc[0]);
}

// The words, and a channel with no stand-in.
void TestWords() {
    rtgcwii w = fresh();
    Pads p;
    p.plug(0, kGcA | kGcLeft);
    rtgcwii_pads(&w, p.s, 1);
    rtgcwii_pads(&w, p.s, 17);
    rtshape_out f;
    rtgcwii_sideways(&w, 0, &f);
    EXPECT_EQ(f.hold, kWr2 | kWrUp);
    rtgcwii_sideways(&w, 1, &f);
    EXPECT_EQ(f.hold, 0u);
    EXPECT_EQ(f.acc[1], -RTHOME_AXIS_FULL);
    EXPECT_EQ(f.pointer_shown, 0);
}

}  // namespace

int main() {
    TestButtons();
    TestTurnedDpad();
    TestTilt();
    TestCamera();
    TestShake();
    TestWords();
    if (g_failures == 0) std::cout << "gcwii_sideways: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
