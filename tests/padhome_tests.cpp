// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The HOME Menu from a GameCube controller (runtime/rthome.h, #224 part
// 1): when the combo starts the stand-in, which channel it takes, the HOME
// press, waiting for the menu, the menu's frames, the stand-in going away,
// the buttons and sticks it carries, and the float bits it writes.
#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" {
#include "rthome.h"
}

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uint32_t kGcCombo = 0x1070;  // Start, L, R, Z
constexpr std::uint32_t kGcA = 0x0100;
constexpr std::uint32_t kGcStart = 0x1000;
constexpr std::uint32_t kGcUp = 0x0008;
// Ticks, as the loader fills them in at 1 tick a millisecond for the tests.
constexpr std::uint32_t kHold = 2000, kPress = 600, kWait = 1500, kLinger = 300;

struct Pads {
    std::uint8_t s[48];
    Pads() {
        std::memset(s, 0, sizeof s);
        for (int i = 0; i < 4; ++i) s[i * 12 + 10] = 0xFF;  // nothing plugged in
    }
    // Port `port` plugged in, with these buttons and its stick.
    Pads& port(int port, std::uint32_t buttons, int x = 0, int y = 0) {
        std::uint8_t* p = s + port * 12;
        p[0] = static_cast<std::uint8_t>(buttons >> 8);
        p[1] = static_cast<std::uint8_t>(buttons);
        p[2] = static_cast<std::uint8_t>(static_cast<std::int8_t>(x));
        p[3] = static_cast<std::uint8_t>(static_cast<std::int8_t>(y));
        p[10] = 0;
        return *this;
    }
};

rthome Fresh() {
    rthome h;
    std::memset(&h, 0, sizeof h);
    h.hold = kHold;
    h.press = kPress;
    h.wait = kWait;
    h.linger = kLinger;
    h.channel = RTHOME_NO_CHANNEL;
    return h;
}

std::uint32_t Hold(rthome& h) {
    std::uint32_t hold, trig, release;
    std::int32_t axes[6];
    rthome_classic(&h, 0, &hold, &trig, &release, axes);
    return hold;
}

// The combo held from `from` through `to` on port 1, a read every 16 ms.
void HoldCombo(rthome& h, std::uint32_t from, std::uint32_t to, std::uint32_t extra = 0) {
    for (std::uint32_t t = from; t <= to; t += 16) rthome_pads(&h, Pads().port(1, kGcCombo | extra).s, t);
}

void TestComboStartsStandIn() {
    rthome h = Fresh();
    HoldCombo(h, 1000, 2900);  // 1.9 s: not yet
    EXPECT_EQ(h.phase, RTHOME_IDLE);
    EXPECT_EQ(rthome_stand_in(&h, 2900), RTHOME_NO_CHANNEL);
    HoldCombo(h, 2916, 3100);
    EXPECT_EQ(h.phase, RTHOME_PRESSING);
    EXPECT_EQ(h.port, 1u);
    EXPECT_EQ(rthome_stand_in(&h, 3100), 0u);  // no channel probed yet: channel 0
    EXPECT_EQ(Hold(h), RTHOME_CL_HOME);
}

void TestLettingGoStartsOver() {
    rthome h = Fresh();
    HoldCombo(h, 1000, 2500);
    rthome_pads(&h, Pads().port(1, 0).s, 2516);  // let go
    HoldCombo(h, 2532, 4000);  // 1.5 s more: not 2 s in one hold
    EXPECT_EQ(h.phase, RTHOME_IDLE);
}

void TestChannelChoice() {
    {
        rthome h = Fresh();
        rthome_probed(&h, 0, 0);   // a Wii Remote on channel 0
        rthome_probed(&h, 1, -1);  // none on 1
        rthome_probed(&h, 2, -1);
        HoldCombo(h, 0, 2100);
        EXPECT_EQ(rthome_stand_in(&h, 2100), 1u);  // the lowest free one
    }
    {
        rthome h = Fresh();
        rthome_probed(&h, 0, 0);
        rthome_probed(&h, 1, -2);  // busy: not free
        HoldCombo(h, 0, 2100);
        EXPECT_EQ(rthome_stand_in(&h, 2100), 2u);  // never asked about
    }
    {
        rthome h = Fresh();
        for (std::uint32_t c = 0; c < 4; ++c) rthome_probed(&h, c, 0);  // four Wii Remotes
        HoldCombo(h, 0, 2100);
        EXPECT_EQ(h.phase, RTHOME_DONE);
        EXPECT_EQ(rthome_stand_in(&h, 2100), RTHOME_NO_CHANNEL);
    }
    {
        rthome h = Fresh();
        rthome_probed(&h, 7, -1);  // no such channel: ignored
        EXPECT_EQ(h.probed, 0u);
    }
}

void TestMenuOpensAndCloses() {
    rthome h = Fresh();
    HoldCombo(h, 0, 2016);              // the hold began at 0 (noted as 1): 2 s at 2016
    const std::uint32_t pressed = 2017;  // the press began then (noted with its low bit)
    EXPECT_EQ(rthome_stand_in(&h, pressed + kPress - 1), 0u);
    EXPECT_EQ(Hold(h), RTHOME_CL_HOME);
    EXPECT_EQ(rthome_stand_in(&h, pressed + kPress), 0u);  // the press ends
    EXPECT_EQ(h.phase, RTHOME_WAITING);
    EXPECT_TRUE(!rthome_menu_open(&h));
    rthome_menu_frame(&h, pressed + kPress + 100);
    EXPECT_EQ(h.phase, RTHOME_OPEN);
    EXPECT_TRUE(rthome_menu_open(&h));
    // Frames for a while, the stand-in stays.
    for (std::uint32_t t = pressed + kPress + 116; t < pressed + 9000; t += 16) rthome_menu_frame(&h, t);
    EXPECT_EQ(rthome_stand_in(&h, pressed + 9000), 0u);
    // Closed: no frame for `linger`.
    EXPECT_EQ(rthome_stand_in(&h, pressed + 9000 + kLinger), RTHOME_NO_CHANNEL);
    EXPECT_EQ(h.phase, RTHOME_DONE);
    // Still held: nothing until let go.
    HoldCombo(h, pressed + 9400, pressed + 12000);
    EXPECT_EQ(h.phase, RTHOME_DONE);
    rthome_pads(&h, Pads().port(1, 0).s, pressed + 12016);
    EXPECT_EQ(h.phase, RTHOME_IDLE);
}

void TestMenuOpenedDuringPress() {
    rthome h = Fresh();
    HoldCombo(h, 0, 2016);
    for (std::uint32_t t = 2100; t <= 2600; t += 16) rthome_menu_frame(&h, t);  // the game opened it at once
    EXPECT_EQ(h.phase, RTHOME_PRESSING);
    EXPECT_EQ(rthome_stand_in(&h, 2017 + kPress), 0u);
    EXPECT_EQ(h.phase, RTHOME_OPEN);
}

void TestGameRefuses() {
    // A save or a loading screen: the game shows its crossed-out HOME icon,
    // and the menu never opens.
    rthome h = Fresh();
    HoldCombo(h, 0, 2016);
    const std::uint32_t waiting = 2017 + kPress;
    EXPECT_EQ(rthome_stand_in(&h, waiting), 0u);
    EXPECT_EQ(rthome_stand_in(&h, waiting + kWait - 1), 0u);  // the wait counts from the press's end (noted as waiting | 1)
    EXPECT_EQ(rthome_stand_in(&h, waiting + kWait + 1), RTHOME_NO_CHANNEL);
    EXPECT_EQ(h.phase, RTHOME_DONE);
}

void TestButtonsInTheMenu() {
    rthome h = Fresh();
    HoldCombo(h, 0, 2016);
    (void)rthome_stand_in(&h, 2017 + kPress);
    rthome_menu_frame(&h, 2700);
    std::uint32_t hold, trig, release;
    std::int32_t axes[6];
    rthome_classic(&h, 2700, &hold, &trig, &release, axes);  // HOME let go after the press
    // Start is still held from the combo: it doesn't close the menu.
    rthome_pads(&h, Pads().port(1, kGcStart).s, 2716);
    rthome_classic(&h, 2716, &hold, &trig, &release, axes);
    EXPECT_EQ(hold, 0u);
    // Let go, then A and Up pressed.
    rthome_pads(&h, Pads().port(1, 0).s, 2732);
    rthome_pads(&h, Pads().port(1, kGcA | kGcUp, 50, -80).s, 2748);
    rthome_classic(&h, 2748, &hold, &trig, &release, axes);
    EXPECT_EQ(hold, RTHOME_CL_A | RTHOME_CL_UP);
    EXPECT_EQ(trig, RTHOME_CL_A | RTHOME_CL_UP);
    EXPECT_EQ(axes[0], (50 - 10) * RTHOME_AXIS_FULL / 62);
    EXPECT_EQ(axes[1], -RTHOME_AXIS_FULL);  // past full tilt
    rthome_classic(&h, 2764, &hold, &trig, &release, axes);
    EXPECT_EQ(trig, 0u);  // held, not pressed again
    // Start now presses HOME: the menu closes on it.
    rthome_pads(&h, Pads().port(1, kGcStart).s, 2780);
    rthome_classic(&h, 2780, &hold, &trig, &release, axes);
    EXPECT_EQ(hold, RTHOME_CL_HOME);
    EXPECT_EQ(release, RTHOME_CL_A | RTHOME_CL_UP);
    // The stick at rest reads 0.
    rthome_pads(&h, Pads().port(1, 0, 6, -7).s, 2796);
    rthome_classic(&h, 2796, &hold, &trig, &release, axes);
    EXPECT_EQ(axes[0], 0);
    EXPECT_EQ(axes[1], 0);
}

void TestFloatBits() {
    EXPECT_EQ(rthome_float_bits(0), 0u);
    EXPECT_EQ(rthome_float_bits(RTHOME_AXIS_FULL), 0x3F800000u);       // 1.0
    EXPECT_EQ(rthome_float_bits(-RTHOME_AXIS_FULL), 0xBF800000u);      // -1.0
    EXPECT_EQ(rthome_float_bits(RTHOME_AXIS_FULL / 2), 0x3F000000u);   // 0.5
    EXPECT_EQ(rthome_float_bits(RTHOME_AXIS_FULL * 3 / 4), 0x3F400000u);  // 0.75
    EXPECT_EQ(rthome_float_bits(1), 0x3B800000u);                      // 1/256
    EXPECT_EQ(rthome_float_bits(RTHOME_AXIS_FULL * 2), 0x3F800000u);   // past full: 1.0
    // Every value against the host's own floats.
    for (std::int32_t n = -RTHOME_AXIS_FULL; n <= RTHOME_AXIS_FULL; ++n) {
        const float expected = static_cast<float>(n) / RTHOME_AXIS_FULL;
        std::uint32_t bits;
        std::memcpy(&bits, &expected, 4);
        if (n == 0) bits = 0;
        EXPECT_EQ(rthome_float_bits(n), bits);
    }
}

}  // namespace

int main() {
    TestComboStartsStandIn();
    TestLettingGoStartsOver();
    TestChannelChoice();
    TestMenuOpensAndCloses();
    TestMenuOpenedDuringPress();
    TestGameRefuses();
    TestButtonsInTheMenu();
    TestFloatBits();
    if (g_failures == 0) std::cout << "padhome: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
