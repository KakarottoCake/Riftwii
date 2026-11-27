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

int s16_at(const std::uint8_t* p) { return static_cast<std::int16_t>((p[0] << 8) | p[1]); }

// The raw status for a game that reads WPAD itself (rtgcwii_wpad_status): a
// bare Wii Remote's 42 bytes, its buttons, lying flat (+z up, one g), no
// camera objects until the camera is on; then, with the format the game
// asked for (2: with the camera) or KPAD's switch, the two dots around the
// camera's centre, moving against the pointer, and the other two empty.
void TestWpadStatus() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0, kGcA | kGcUp);
    rtgcwii_pads(&w, p.s, 1);
    std::uint8_t out[RTGCWII_WPAD_FS_BYTES + 4];
    std::memset(out, 0xEE, sizeof(out));
    EXPECT_EQ(rtgcwii_wpad_status(&w, 0, out, 1, 0), RTGCWII_WPAD_CORE_BYTES);
    EXPECT_EQ((out[0] << 8) | out[1], static_cast<int>(kWr2 | kWrRight));
    EXPECT_EQ(s16_at(out + 2), 0);
    EXPECT_EQ(s16_at(out + 4), 0);
    EXPECT_EQ(s16_at(out + 6), RTGCWII_ACC_G);
    for (int i = 8; i < 40; ++i) EXPECT_EQ(out[i], 0);
    EXPECT_EQ(out[40], 0);  // a bare Wii Remote
    EXPECT_EQ(out[41], 0);  // no error
    EXPECT_EQ(out[RTGCWII_WPAD_CORE_BYTES], 0xEE);  // nothing past the status
    // Tilted right by the C-stick (camera off): along the remote's length.
    s[4] = 72;
    rtgcwii_pads(&w, p.s, 17);
    rtgcwii_wpad_status(&w, 0, out, 17, 0);
    EXPECT_EQ(s16_at(out + 4), -RTGCWII_TILT_SIN * RTGCWII_ACC_G / RTHOME_AXIS_FULL);
    EXPECT_TRUE(s16_at(out + 6) > 0);
    // The game asked for the camera's format: the dots, centred while the
    // pointer is, and the C-stick now moves the pointer (right: dots left).
    s[4] = 0;
    w.format[0] = 2;
    rtgcwii_pads(&w, p.s, 33);
    rtgcwii_wpad_status(&w, 0, out, 33, 0);
    EXPECT_EQ(s16_at(out + 8), 512 + RTGCWII_DOT_APART / 2 + 1);
    EXPECT_EQ(s16_at(out + 10), 384);
    EXPECT_EQ(s16_at(out + 12), static_cast<int>(RTGCWII_DOT_SIZE));
    EXPECT_EQ(out[14], 0);
    EXPECT_EQ(s16_at(out + 16), 512 - RTGCWII_DOT_APART / 2);
    EXPECT_EQ(out[22], 1);
    EXPECT_EQ(s16_at(out + 24), 0);
    EXPECT_EQ(s16_at(out + 26), 767);
    EXPECT_EQ(s16_at(out + 28), 0);  // nothing seen
    EXPECT_EQ(out[30], 2);
    EXPECT_EQ(out[38], 3);
    s[4] = 72;
    s[5] = 72;  // C-stick right and up
    rtgcwii_pads(&w, p.s, 49);
    rtgcwii_wpad_status(&w, 0, out, 49, 0);
    EXPECT_TRUE(s16_at(out + 8) < 512 + RTGCWII_DOT_APART / 2 + 1);
    EXPECT_TRUE(s16_at(out + 10) > 384);  // pointing up: the bar lower in the camera's view
    EXPECT_EQ(s16_at(out + 6), RTGCWII_ACC_G);  // the camera on: no tilt
    // Camera off again, but KPAD's switch on: still the dots.
    w.format[0] = 1;
    rtgcwii_wpad_status(&w, 0, out, 49, 1);
    EXPECT_EQ(s16_at(out + 12), static_cast<int>(RTGCWII_DOT_SIZE));
    rtgcwii_wpad_status(&w, 0, out, 49, 0);
    EXPECT_EQ(s16_at(out + 12), 0);
    // A channel with no stand-in: nothing written.
    std::memset(out, 0xEE, sizeof(out));
    EXPECT_EQ(rtgcwii_wpad_status(&w, 1, out, 49, 0), 0u);
    EXPECT_EQ(out[0], 0xEE);
    // A raw read doesn't take a press from the game's next KPAD read: X
    // pressed, read raw first, is still new (A's trig) at the KPAD read.
    {
        rtgcwii w2 = fresh();
        Pads p2;
        p2.plug(0, 0);
        rtgcwii_pads(&w2, p2.s, 1);
        rtshape_out f;
        rtgcwii_sideways(&w2, 0, &f);
        p2.plug(0, kGcX);
        rtgcwii_pads(&w2, p2.s, 17);
        rtgcwii_wpad_status(&w2, 0, out, 17, 0);
        EXPECT_EQ((out[0] << 8) | out[1], static_cast<int>(kWrA));
        // The same sample: the KPAD read still sees A as new.
        rtgcwii_sideways(&w2, 0, &f);
        EXPECT_EQ(f.trig, kWrA);
        // A raw read on a later sample, X still held, then the KPAD read
        // works its words out again: no new press, nothing lost or doubled.
        rtgcwii_pads(&w2, p2.s, 33);
        rtgcwii_wpad_status(&w2, 0, out, 33, 0);
        rtgcwii_sideways(&w2, 0, &f);
        EXPECT_EQ(f.hold, kWrA);
        EXPECT_EQ(f.trig, 0u);
    }
    {
        // X pressed between two KPAD reads, read raw first on its own
        // sample: still new at the next KPAD read (it works its words out
        // again from what the game was last given).
        rtgcwii w3 = fresh();
        Pads p3;
        p3.plug(0, 0);
        rtgcwii_pads(&w3, p3.s, 1);
        rtshape_out f;
        rtgcwii_sideways(&w3, 0, &f);
        p3.plug(0, kGcX);
        rtgcwii_pads(&w3, p3.s, 17);
        rtgcwii_wpad_status(&w3, 0, out, 17, 0);
        rtgcwii_pads(&w3, p3.s, 33);
        rtgcwii_sideways(&w3, 0, &f);
        EXPECT_EQ(f.trig, kWrA);
    }
}

// Telling again (rtgcwii_retell): a channel told and not read since is told
// again every retell_ticks, at most RTGCWII_RETELLS times; one the game read
// isn't; its controller unplugged and back starts afresh. Off at 0.
void TestRetell() {
    rtgcwii w = fresh();
    Pads p;
    p.plug(0);
    rtgcwii_pads(&w, p.s, 1);
    std::uint32_t chan = 9;
    std::int32_t reason = 9;
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);
    EXPECT_EQ(chan, 0u);
    EXPECT_EQ(reason, 0);
    rtgcwii_retell(&w, 10);  // off
    rtgcwii_retell(&w, 100000);
    EXPECT_EQ(w.slot.told, 1u);
    w.slot.retell_ticks = 100;
    rtgcwii_retell(&w, 200);  // notes when
    rtgcwii_retell(&w, 299);
    EXPECT_EQ(w.slot.told, 1u);
    rtgcwii_retell(&w, 300);
    EXPECT_EQ(w.slot.told, 1u);  // still told...
    EXPECT_EQ(w.slot.retell, 1u);  // ...and due to be told again
    EXPECT_EQ(w.slot.retells[0], 1u);
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);
    EXPECT_EQ(reason, 0);
    // Read now: never again.
    rtgcwii_heard(&w, 0);
    rtgcwii_retell(&w, 400);
    rtgcwii_retell(&w, 1000);
    EXPECT_EQ(w.slot.told, 1u);
    // At most RTGCWII_RETELLS times while it stays.
    w.slot.heard = 0;
    std::uint32_t t = 2000;
    for (unsigned i = 0; i < 2 * RTGCWII_RETELLS; ++i) {
        rtgcwii_retell(&w, t);
        rtgcwii_retell(&w, t + 100);
        rtgcwii_next_change(&w, 0, 0, &chan, &reason);
        t += 200;
    }
    EXPECT_EQ(w.slot.retells[0], RTGCWII_RETELLS);
    // Unplugged: forgotten, so the next controller is told again as new.
    Pads none;
    rtgcwii_pads(&w, none.s, t + kGrace);
    rtgcwii_retell(&w, t + kGrace);
    EXPECT_EQ(w.slot.retells[0], 0u);
    EXPECT_EQ(w.slot.heard, 0u);
    // A read before the callback was told (a WPADRead) doesn't count as
    // heard once it is: the new "connected" clears it.
    {
        rtgcwii w3 = fresh();
        Pads p3;
        p3.plug(0);
        rtgcwii_pads(&w3, p3.s, 1);
        rtgcwii_heard(&w3, 0);
        EXPECT_EQ(rtgcwii_next_change(&w3, 0, 0, &chan, &reason), 1);
        EXPECT_EQ(w3.slot.heard, 0u);
    }
    // Due to be told again, but unplugged first: told it's gone, not left
    // connected in the game (CodeRabbit CLI, #15).
    {
        rtgcwii w4 = fresh();
        Pads p4;
        p4.plug(0);
        rtgcwii_pads(&w4, p4.s, 1);
        EXPECT_EQ(rtgcwii_next_change(&w4, 0, 0, &chan, &reason), 1);
        w4.slot.retell_ticks = 100;
        rtgcwii_retell(&w4, 200);
        rtgcwii_retell(&w4, 300);
        EXPECT_EQ(w4.slot.retell, 1u);
        Pads none4;
        rtgcwii_pads(&w4, none4.s, 300 + 2 * kGrace);
        EXPECT_EQ(rtgcwii_on(&w4, 0), 0);
        chan = 9;
        reason = 9;
        EXPECT_EQ(rtgcwii_next_change(&w4, 0, 0, &chan, &reason), 1);
        EXPECT_EQ(chan, 0u);
        EXPECT_EQ(reason, -1);
        EXPECT_EQ(w4.slot.told, 0u);
        EXPECT_EQ(w4.slot.retell, 0u);
    }
    // A channel still to be told isn't counted.
    rtgcwii w2 = fresh();
    w2.slot.retell_ticks = 100;
    rtgcwii_pads(&w2, p.s, 1);
    rtgcwii_retell(&w2, 1);
    rtgcwii_retell(&w2, 500);
    EXPECT_EQ(w2.slot.retells[0], 0u);
    EXPECT_EQ(w2.slot.told_at[0], 0u);
}

}  // namespace

int main() {
    TestButtons();
    TestTurnedDpad();
    TestTilt();
    TestCamera();
    TestShake();
    TestWords();
    TestWpadStatus();
    TestRetell();
    if (g_failures == 0) std::cout << "gcwii_sideways: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
