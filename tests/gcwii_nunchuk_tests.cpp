// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// GameCube controllers as a Wii Remote + Nunchuk (runtime/rtgcwii.h with
// RT_PAD_FORM_NUNCHUK, #587): #587's default Nunchuk layout, the analog
// triggers as the Nunchuk's buttons, the pointer following the C-stick and
// hiding after a while, and X shaking the Wii Remote (the axes flipping
// every few samples). This test compiles rtgcwii.c and rthome.c itself,
// with the form's define, as the pad builds that carry it do.
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
constexpr std::uint32_t kWr2 = 0x0100, kWrB = 0x0400, kWrA = 0x0800, kWrMinus = 0x1000;
constexpr std::uint32_t kNcZ = 0x2000, kNcC = 0x4000;
constexpr std::uint32_t kGrace = 300;     // ticks (1 a millisecond here)
constexpr std::uint32_t kShowFor = 2000;  // the pointer's time shown

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
    w.shape.pointer_show = kShowFor;
    return w;
}

// The buttons, one by one, and the triggers past a third of the way.
void TestMapping() {
    std::uint8_t pad[12] = {};
    const struct {
        std::uint32_t gc, wii;
    } cases[] = {{kGcA, kWrA},       {kGcB, kWrB},         {kGcY, kWr2},       {kGcZ, kWrMinus},
                 {kGcStart, kWrPlus}, {kGcL, kNcZ},         {kGcR, kNcC},       {kGcLeft, kWrLeft},
                 {kGcRight, kWrRight}, {kGcUp, kWrUp},      {kGcDown, kWrDown}, {kGcX, 0u}};
    for (const auto& c : cases) {
        pad[0] = static_cast<std::uint8_t>(c.gc >> 8);
        pad[1] = static_cast<std::uint8_t>(c.gc);
        EXPECT_EQ(rtgcwii_map_nunchuk(pad), c.wii);
    }
    pad[0] = pad[1] = 0;
    pad[6] = 84;  // L just short of a third
    EXPECT_EQ(rtgcwii_map_nunchuk(pad), 0u);
    pad[6] = 85;
    EXPECT_EQ(rtgcwii_map_nunchuk(pad), kNcZ);
    pad[6] = 0;
    pad[7] = 200;
    EXPECT_EQ(rtgcwii_map_nunchuk(pad), kNcC);
}

// The words for the game: newly pressed once, then held, then let go.
void TestWords() {
    rtgcwii w = fresh();
    Pads p;
    p.plug(0);
    rtgcwii_pads(&w, p.s, 1);
    rtshape_out f;
    rtgcwii_nunchuk(&w, 0, 1, &f);
    EXPECT_EQ(f.hold, 0u);
    p.plug(0, kGcA | kGcL);
    rtgcwii_pads(&w, p.s, 17);
    rtgcwii_nunchuk(&w, 0, 17, &f);
    EXPECT_EQ(f.hold, kWrA | kNcZ);
    EXPECT_EQ(f.trig, kWrA | kNcZ);
    rtgcwii_nunchuk(&w, 0, 18, &f);  // a second read of the same sample
    EXPECT_EQ(f.trig, kWrA | kNcZ);
    p.plug(0, kGcL);
    rtgcwii_pads(&w, p.s, 33);
    rtgcwii_nunchuk(&w, 0, 33, &f);
    EXPECT_EQ(f.hold, kNcZ);
    EXPECT_EQ(f.trig, 0u);
    EXPECT_EQ(f.release, kWrA);
    // A channel with no stand-in: a still controller, nothing held.
    rtgcwii_nunchuk(&w, 1, 33, &f);
    EXPECT_EQ(f.hold, 0u);
    EXPECT_EQ(f.acc[1], -RTHOME_AXIS_FULL);
    EXPECT_EQ(f.acc_value, RTHOME_AXIS_FULL);
    EXPECT_EQ(f.pointer_shown, 0);
}

// The stick as the Nunchuk's, the controllers still.
void TestStickAndRest() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0);
    s[2] = 72;  // full right
    s[3] = static_cast<std::uint8_t>(-72);
    rtgcwii_pads(&w, p.s, 1);
    rtshape_out f;
    rtgcwii_nunchuk(&w, 0, 1, &f);
    EXPECT_EQ(f.stick[0], RTHOME_AXIS_FULL);
    EXPECT_EQ(f.stick[1], -RTHOME_AXIS_FULL);
    EXPECT_EQ(f.acc[0], 0);
    EXPECT_EQ(f.acc[1], -RTHOME_AXIS_FULL);
    EXPECT_EQ(f.fs_acc[1], -RTHOME_AXIS_FULL);
    EXPECT_EQ(f.fs_acc_value, RTHOME_AXIS_FULL);
    EXPECT_EQ(f.acc_speed, 0);
}

// The pointer moves with the C-stick, stops at the edge, and hides a
// while after the C-stick lets go.
void TestPointer() {
    rtgcwii w = fresh();
    Pads p;
    std::uint8_t* s = p.plug(0);
    s[4] = 59;  // C-stick full right
    rtgcwii_pads(&w, p.s, 100);
    rtshape_out f;
    rtgcwii_nunchuk(&w, 0, 100, &f);
    EXPECT_EQ(f.pointer[0], RTGCWII_POINTER_STEP);
    EXPECT_EQ(f.pointer[1], 0);
    EXPECT_EQ(f.pointer_shown, 1);
    for (int i = 0; i < 40; ++i) rtgcwii_pads(&w, p.s, 101 + i);
    rtgcwii_nunchuk(&w, 0, 140, &f);
    EXPECT_EQ(f.pointer[0], RTGCWII_POINTER_EDGE);
    // Up on the C-stick is up on the screen: y goes negative.
    s[4] = 0;
    s[5] = 59;
    rtgcwii_pads(&w, p.s, 200);
    rtgcwii_nunchuk(&w, 0, 200, &f);
    EXPECT_EQ(f.pointer[1], -RTGCWII_POINTER_STEP);
    // Let go: it stays where it was, shown until kShowFor has passed.
    s[5] = 0;
    rtgcwii_pads(&w, p.s, 300);
    rtgcwii_nunchuk(&w, 0, 300 + kShowFor - 101, &f);
    EXPECT_EQ(f.pointer_shown, 1);
    EXPECT_EQ(f.pointer[0], RTGCWII_POINTER_EDGE);
    rtgcwii_nunchuk(&w, 0, 200 + kShowFor, &f);
    EXPECT_EQ(f.pointer_shown, 0);
    // Hidden for good: a sample after its time clears the mark, so the
    // time base wrapping round (about every 70 s) doesn't show it again.
    rtgcwii_pads(&w, p.s, 200 + kShowFor);
    rtgcwii_nunchuk(&w, 0, 200 + kShowFor + 1, &f);
    EXPECT_EQ(f.pointer_shown, 0);
    rtgcwii_pads(&w, p.s, 200 + 4u * kShowFor);
    rtgcwii_nunchuk(&w, 0, 0xFFFFFFFFu, &f);  // past the wrap
    EXPECT_EQ(f.pointer_shown, 0);
    rtgcwii_nunchuk(&w, 0, 250, &f);  // an hour of ticks later, wrapped round
    EXPECT_EQ(f.pointer_shown, 0);
    EXPECT_EQ(f.pointer[0], RTGCWII_POINTER_EDGE);  // still where it was left
}

// With shake_flip (the loader's 33 ms), the shake keeps time, not samples:
// read every 4 ms or every 16 ms, the swing turns about every two
// shake_flips (a whole swing RTGCWII_SHAKE_PERIOD counts, four a flip),
// and every read while it's held has the steady speed (Wario Land: Shake
// It!'s earthquake punch wants motion every frame).
void TestShakeKeepsTime() {
    for (std::uint32_t step : {4u, 16u}) {
        rtgcwii w = fresh();
        w.shape.shake_flip = 33;
        Pads p;
        p.plug(0, kGcX);
        rtshape_out f;
        int turns = 0, reads = 0, moving = 0;
        std::int32_t last = 0;
        for (std::uint32_t t = 1000; t < 1000 + 330; t += step) {
            rtgcwii_pads(&w, p.s, t);
            rtgcwii_nunchuk(&w, 0, t, &f);
            if (t > 1000 && ((last < 0) != (f.acc[0] < 0))) ++turns;
            if (t > 1000 && f.acc[0] != last) ++moving;
            ++reads;
            EXPECT_EQ(f.acc_speed, RTGCWII_SHAKE_SPEED);
            last = f.acc[0];
        }
        EXPECT_TRUE(turns >= 4 && turns <= 6);  // 330 ms over a half-swing of about 66 ms, whatever the step
        if (step == 16) EXPECT_TRUE(moving * 3 >= (reads - 1) * 2);  // a read a frame: it moves on most
    }
    // Samples every 10 ms, reads every 20 ms (off the samples): every read
    // while it's held has the speed (CodeRabbit CLI, #610).
    {
        rtgcwii w = fresh();
        w.shape.shake_flip = 40;
        Pads p;
        p.plug(0, kGcX);
        rtshape_out f;
        int reads = 0, with_speed = 0;
        for (std::uint32_t t = 1000; t < 1000 + 400; t += 10) {
            rtgcwii_pads(&w, p.s, t);
            if ((t / 10) % 2 == 1) {
                rtgcwii_nunchuk(&w, 0, t + 5, &f);
                ++reads;
                if (f.acc_speed == RTGCWII_SHAKE_SPEED) ++with_speed;
            }
        }
        EXPECT_EQ(with_speed, reads);
    }
    // X pressed for a single sample, let go before any read: that read
    // still gets the speed (CodeRabbit CLI, #610).
    {
        rtgcwii w = fresh();
        Pads p;
        p.plug(0, kGcX);
        rtshape_out f;
        rtgcwii_pads(&w, p.s, 1000);
        p.plug(0);
        rtgcwii_pads(&w, p.s, 1016);
        rtgcwii_nunchuk(&w, 0, 1016, &f);
        EXPECT_EQ(f.acc_speed, RTGCWII_SHAKE_SPEED);
        rtgcwii_nunchuk(&w, 0, 1032, &f);
        EXPECT_EQ(f.acc_speed, 0);
    }
    // A step, then X let go before any read: the next read still gets the
    // jump in speed, at rest; the read after doesn't (fork #23).
    {
        rtgcwii w = fresh();
        Pads p;
        p.plug(0, kGcX);
        rtshape_out f;
        std::uint32_t t = 1000;
        for (std::uint32_t i = 0; i < RTGCWII_SHAKE_FLIP; ++i) rtgcwii_pads(&w, p.s, t += 16);
        p.plug(0);
        rtgcwii_pads(&w, p.s, t += 16);
        rtgcwii_nunchuk(&w, 0, t, &f);
        EXPECT_EQ(f.acc_speed, RTGCWII_SHAKE_SPEED);
        EXPECT_EQ(f.acc[1], -RTHOME_AXIS_FULL);
        rtgcwii_nunchuk(&w, 0, t + 16, &f);
        EXPECT_EQ(f.acc_speed, 0);
    }
}

// X held shakes the Wii Remote: the axes swing as a trapezoid between
// +-RTGCWII_SHAKE_G, moving every sample on the ramps and holding each peak
// for a few (Batman: The Brave and the Bold's 3.2 g is always seen), the
// speed steady; let go, it's still.
void TestShake() {
    rtgcwii w = fresh();
    Pads p;
    p.plug(0, kGcX);
    rtshape_out f;
    int moving = 0, at_top = 0, at_bottom = 0, run = 0, longest = 0;
    std::int32_t last = 0;
    for (std::uint32_t i = 0; i < 2 * RTGCWII_SHAKE_PERIOD; ++i) {
        rtgcwii_pads(&w, p.s, 1 + i * 16);
        rtgcwii_nunchuk(&w, 0, 1 + i * 16, &f);
        EXPECT_TRUE(f.acc[0] >= -RTGCWII_SHAKE_G && f.acc[0] <= RTGCWII_SHAKE_G);
        EXPECT_EQ(f.acc[1], f.acc[0]);
        EXPECT_EQ(f.acc[2], -f.acc[0]);
        EXPECT_EQ(f.acc_speed, RTGCWII_SHAKE_SPEED);
        if (f.acc[0] == RTGCWII_SHAKE_G || f.acc[0] == -RTGCWII_SHAKE_G) EXPECT_EQ(f.acc_value, RTGCWII_SHAKE_VALUE);
        if (f.acc[0] == RTGCWII_SHAKE_G) ++at_top;
        if (f.acc[0] == -RTGCWII_SHAKE_G) ++at_bottom;
        run = (i > 0 && f.acc[0] == last && (f.acc[0] == RTGCWII_SHAKE_G || f.acc[0] == -RTGCWII_SHAKE_G)) ? run + 1 : 1;
        if (run > longest) longest = run;
        if (i > 0 && f.acc[0] != last) ++moving;
        last = f.acc[0];
        EXPECT_EQ(f.fs_acc_value, RTHOME_AXIS_FULL);  // the Nunchuk stays still
    }
    EXPECT_TRUE(at_top >= 4 && at_bottom >= 4);  // both peaks, every swing
    EXPECT_EQ(longest, static_cast<int>(RTGCWII_SHAKE_TOP));  // each peak held a few samples, no longer
    EXPECT_TRUE(moving >= static_cast<int>(2 * RTGCWII_SHAKE_PERIOD) * 2 / 3);  // moving on most samples
    EXPECT_EQ(f.hold, 0u);  // X itself presses nothing
    p.plug(0);
    rtgcwii_pads(&w, p.s, 1000);
    rtgcwii_nunchuk(&w, 0, 1000, &f);
    EXPECT_EQ(f.acc_value, RTHOME_AXIS_FULL);
    EXPECT_EQ(f.acc[1], -RTHOME_AXIS_FULL);
}

// The game turning the camera on shows the pointer with no C-stick, and
// off hides it again (unless the C-stick has just moved it).
void TestCamera() {
    rtgcwii w = fresh();
    Pads p;
    p.plug(0);
    rtgcwii_pads(&w, p.s, 10);
    rtshape_out f;
    rtgcwii_nunchuk(&w, 0, 10, &f);
    EXPECT_EQ(f.pointer_shown, 0);
    rtgcwii_camera(&w, 0, 1);  // KPADEnableDPD(0)
    rtgcwii_nunchuk(&w, 0, 5000, &f);
    EXPECT_EQ(f.pointer_shown, 1);
    EXPECT_EQ(f.pointer[0], 0);  // the middle of the screen, until the C-stick moves it
    rtgcwii_nunchuk(&w, 1, 5000, &f);
    EXPECT_EQ(f.pointer_shown, 0);  // another channel's camera is its own
    rtgcwii_camera(&w, 0, 0);
    rtgcwii_nunchuk(&w, 0, 5001, &f);
    EXPECT_EQ(f.pointer_shown, 0);
    rtgcwii_camera(&w, 9, 3);  // no such channel: nothing
    EXPECT_EQ(w.shape.camera, 0u);
}

// The status's floats with no cap at 1: the shake's 3.4 g and the
// distance at rest reach the game as themselves (rthome_float_bits stops at
// 1, which would turn the shake into a still Wii Remote).
void TestFloatBits() {
    auto as_float = [](std::uint32_t bits) { float f; std::memcpy(&f, &bits, sizeof f); return f; };
    EXPECT_EQ(rtgcwii_float_bits(0), 0u);
    EXPECT_EQ(as_float(rtgcwii_float_bits(RTHOME_AXIS_FULL)), 1.0f);
    EXPECT_EQ(as_float(rtgcwii_float_bits(-RTHOME_AXIS_FULL / 2)), -0.5f);
    EXPECT_EQ(as_float(rtgcwii_float_bits(RTGCWII_SHAKE_G)), 3.3984375f);
    EXPECT_EQ(as_float(rtgcwii_float_bits(-RTGCWII_SHAKE_G)), -3.3984375f);
    EXPECT_EQ(as_float(rtgcwii_float_bits(RTGCWII_SHAKE_VALUE)), 5.890625f);
    EXPECT_EQ(as_float(rtgcwii_float_bits(498)), 1.9453125f);
    EXPECT_EQ(as_float(rtgcwii_float_bits(1)), 1.0f / 256.0f);
    EXPECT_EQ(as_float(rtgcwii_float_bits(0x40000000)), 4194304.0f);  // past 2^23: shifted right
    EXPECT_EQ(as_float(rtgcwii_float_bits(INT32_MIN)), -8388608.0f);  // the most negative: -2^31 / 256
    for (std::int32_t n = -RTHOME_AXIS_FULL; n <= RTHOME_AXIS_FULL; ++n)
        EXPECT_EQ(rtgcwii_float_bits(n), rthome_float_bits(n));  // the same up to 1
}

}  // namespace

int main() {
    TestShakeKeepsTime();
    TestMapping();
    TestWords();
    TestStickAndRest();
    TestPointer();
    TestShake();
    TestCamera();
    TestFloatBits();
    if (g_failures == 0) std::cout << "gcwii_nunchuk: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
