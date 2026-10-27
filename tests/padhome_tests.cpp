// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The HOME Menu from a GameCube controller (runtime/rthome.h, #224 part
// 1): when the combo starts the stand-in, which channel it takes, the HOME
// press, waiting for the menu, the menu's frames, the stand-in going away,
// the buttons and sticks it carries (on the Classic Controller and on its
// Wii Remote, with the pointer), whether part 1 stays on in a game after a
// stand-in, and the float bits it writes.
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
    HoldCombo(h, 0, 2016);              // the hold began at 0
    const std::uint32_t pressed = 2000;  // 2 s on: the press began at that read
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
    EXPECT_EQ(rthome_stand_in(&h, 2016 + kPress), 0u);
    EXPECT_EQ(h.phase, RTHOME_OPEN);
}

void TestGameRefuses() {
    // A save or a loading screen: the game shows its crossed-out HOME icon,
    // and the menu never opens.
    rthome h = Fresh();
    HoldCombo(h, 0, 2016);
    const std::uint32_t waiting = 2016 + kPress;
    EXPECT_EQ(rthome_stand_in(&h, waiting), 0u);
    EXPECT_EQ(rthome_stand_in(&h, waiting + kWait - 1), 0u);  // the wait counts from the press's end
    EXPECT_EQ(rthome_stand_in(&h, waiting + kWait), RTHOME_NO_CHANNEL);
    EXPECT_EQ(h.phase, RTHOME_DONE);
}

// Times at even ticks: each is noted with its low bit set, which must not
// make the next look like the past (a wrapped difference ended the stand-in
// the moment its press did, in Dolphin).
void TestEvenTicks() {
    rthome h = Fresh();
    for (std::uint32_t t = 0; t <= 2000; t += 16) rthome_pads(&h, Pads().port(1, kGcCombo).s, t);
    EXPECT_EQ(h.phase, RTHOME_PRESSING);
    EXPECT_EQ(rthome_stand_in(&h, 2000 + kPress), 0u);  // even: the press ends, the wait begins
    EXPECT_EQ(h.phase, RTHOME_WAITING);
    EXPECT_EQ(rthome_stand_in(&h, 2000 + kPress), 0u);  // the same tick again: still waiting
    EXPECT_EQ(h.phase, RTHOME_WAITING);
    rthome_menu_frame(&h, 2000 + kPress + 100);
    EXPECT_EQ(rthome_stand_in(&h, 2000 + kPress + 100), 0u);  // a frame at this very tick: open
    EXPECT_EQ(h.phase, RTHOME_OPEN);
}

// A Virtual Console game's Operations Guide: A on it, HBMCalc stops, and
// the stand-in must stay so the guide can be used and closed (Cruis'n USA,
// d2x Dolphin, 27 Oct). HOME closes it; the menu not coming back ends the
// stand-in. A menu closed by HOME ends it at once.
void TestOperationsGuide() {
    rthome h = Fresh();
    HoldCombo(h, 0, 2016);
    (void)rthome_stand_in(&h, 2000 + kPress);
    std::uint32_t hold, trig, release;
    std::int32_t axes[6];
    std::uint32_t t = 2700;
    for (; t < 3000; t += 16) rthome_menu_frame(&h, t);
    rthome_pads(&h, Pads().port(1, 0).s, t);
    rthome_classic(&h, t, &hold, &trig, &release, axes);
    rthome_pads(&h, Pads().port(1, kGcA).s, t + 16);  // A on Operations Guide
    rthome_classic(&h, t + 16, &hold, &trig, &release, axes);
    EXPECT_EQ(trig, RTHOME_CL_A);
    rthome_selected(&h, 3);                         // the menu ends on the Operations Guide
    rthome_menu_frame(&h, t + 16);                  // the menu's last frame
    EXPECT_EQ(rthome_stand_in(&h, t + 16 + kLinger), 0u);  // HBMCalc stopped: aside, still there
    EXPECT_EQ(h.phase, RTHOME_ASIDE);
    EXPECT_EQ(rthome_stand_in(&h, t + 60000), 0u);  // a minute in the guide: still there
    rthome_pads(&h, Pads().port(1, kGcStart).s, t + 60016);  // Start: HOME closes the guide
    rthome_classic(&h, t + 60016, &hold, &trig, &release, axes);
    EXPECT_EQ(trig, RTHOME_CL_HOME);
    EXPECT_EQ(rthome_stand_in(&h, t + 60016 + kWait - 1), 0u);
    EXPECT_EQ(rthome_stand_in(&h, t + 60016 + kWait), RTHOME_NO_CHANNEL);  // back in the game
    // The guide closing back into the menu: open again, as before.
    rthome g = Fresh();
    HoldCombo(g, 0, 2016);
    (void)rthome_stand_in(&g, 2000 + kPress);
    rthome_menu_frame(&g, 2700);
    rthome_pads(&g, Pads().port(1, kGcA).s, 2716);
    rthome_classic(&g, 2716, &hold, &trig, &release, axes);
    rthome_selected(&g, 3);
    EXPECT_EQ(rthome_stand_in(&g, 2700 + kLinger), 0u);
    EXPECT_EQ(g.phase, RTHOME_ASIDE);
    rthome_menu_frame(&g, 9000);
    EXPECT_EQ(g.phase, RTHOME_OPEN);
}

// Close by the pointer and A: HBMGetSelectBtnNum answers 0, the menu has
// closed, the stand-in goes (Cruis'n USA answered 0 for Close, 27 Oct).
void TestClosedByPointer() {
    rthome h = Fresh();
    HoldCombo(h, 0, 2016);
    (void)rthome_stand_in(&h, 2000 + kPress);
    rthome_menu_frame(&h, 2700);
    rthome_pads(&h, Pads().port(1, kGcA).s, 2716);
    rthome_selected(&h, 0);
    EXPECT_EQ(rthome_stand_in(&h, 2700 + kLinger), RTHOME_NO_CHANNEL);
    EXPECT_EQ(h.phase, RTHOME_DONE);
}

void TestClosedByHome() {
    rthome h = Fresh();
    HoldCombo(h, 0, 2016);
    (void)rthome_stand_in(&h, 2000 + kPress);
    std::uint32_t hold, trig, release;
    std::int32_t axes[6];
    rthome_menu_frame(&h, 2700);
    rthome_pads(&h, Pads().port(1, 0).s, 2716);
    rthome_classic(&h, 2716, &hold, &trig, &release, axes);
    rthome_pads(&h, Pads().port(1, kGcStart).s, 2732);
    rthome_classic(&h, 2732, &hold, &trig, &release, axes);
    rthome_menu_frame(&h, 2748);
    EXPECT_EQ(rthome_stand_in(&h, 2748 + kLinger), RTHOME_NO_CHANNEL);
    EXPECT_EQ(h.phase, RTHOME_DONE);
}

void TestButtonsInTheMenu() {
    rthome h = Fresh();
    HoldCombo(h, 0, 2016);
    (void)rthome_stand_in(&h, 2016 + kPress);
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
    // A second read before the next PADRead: the same words, A still newly
    // pressed (Naruto reads the channel twice a frame; the HOME Menu acts on
    // newly pressed buttons).
    rthome_classic(&h, 2750, &hold, &trig, &release, axes);
    EXPECT_EQ(trig, RTHOME_CL_A | RTHOME_CL_UP);
    // The next sample, the same buttons: held, not pressed again.
    rthome_pads(&h, Pads().port(1, kGcA | kGcUp, 50, -80).s, 2764);
    rthome_classic(&h, 2764, &hold, &trig, &release, axes);
    EXPECT_EQ(trig, 0u);
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

// One PADRead sample at `t` on port 1, then the Classic and Remote words
// worked out from it.
struct Sample {
    std::uint32_t hold, trig, release, remote_hold, remote_trig, remote_release;
    std::int32_t axes[6], pointer[2];
};
Sample Read(rthome& h, std::uint32_t t, std::uint32_t buttons, int x = 0, int y = 0) {
    Sample s;
    rthome_pads(&h, Pads().port(1, buttons, x, y).s, t);
    rthome_classic(&h, t, &s.hold, &s.trig, &s.release, s.axes);
    rthome_remote(&h, &s.remote_hold, &s.remote_trig, &s.remote_release, s.pointer);
    return s;
}

// An open menu, the combo let go and nothing held.
rthome OpenMenu() {
    rthome h = Fresh();
    HoldCombo(h, 0, 2016);
    (void)rthome_stand_in(&h, 2016 + kPress);
    rthome_menu_frame(&h, 2700);
    (void)Read(h, 2700, 0);
    return h;
}

// The Wii Remote side: Bomberman Blast opens its HOME Menu on the Remote's
// own HOME and runs it as a plain Remote (27 Oct).
void TestRemoteSide() {
    rthome h = Fresh();
    HoldCombo(h, 0, 2016);
    std::uint32_t hold, trig, release;
    std::int32_t axes[6], pointer[2];
    rthome_classic(&h, 2016, &hold, &trig, &release, axes);
    rthome_remote(&h, &hold, &trig, &release, pointer);
    EXPECT_EQ(hold, RTHOME_WR_HOME);  // pressing: the Remote's HOME too
    EXPECT_EQ(trig, RTHOME_WR_HOME);
    (void)rthome_stand_in(&h, 2016 + kPress);
    rthome_menu_frame(&h, 2700);
    Sample s = Read(h, 2700, kGcStart);  // Start still held from the combo: ignored on both
    EXPECT_EQ(s.remote_hold, 0u);
    EXPECT_EQ(s.remote_release, RTHOME_WR_HOME);
    (void)Read(h, 2716, 0);
    s = Read(h, 2732, kGcA | kGcUp);
    EXPECT_EQ(s.remote_hold, RTHOME_WR_A | RTHOME_WR_UP);
    EXPECT_EQ(s.remote_trig, RTHOME_WR_A | RTHOME_WR_UP);
    EXPECT_EQ(s.hold, RTHOME_CL_A | RTHOME_CL_UP);  // the Classic's as before
    s = Read(h, 2748, kGcStart);  // Start: HOME on both
    EXPECT_EQ(s.remote_trig, RTHOME_WR_HOME);
    EXPECT_EQ(s.remote_release, RTHOME_WR_A | RTHOME_WR_UP);
    EXPECT_EQ(s.trig, RTHOME_CL_HOME);
    // X and Y have no Remote button here; B does.
    (void)Read(h, 2764, 0);
    s = Read(h, 2780, 0x0200 | 0x0400 | 0x0800);  // B, X, Y
    EXPECT_EQ(s.remote_hold, RTHOME_WR_B);
}

// The pointer: from the screen's centre, full tilt crosses half the screen
// in 16 samples, and it stays on the screen; stick up points up (y falls).
void TestPointer() {
    rthome h = OpenMenu();
    std::uint32_t hold, trig, release;
    std::int32_t pointer[2];
    rthome_remote(&h, &hold, &trig, &release, pointer);
    EXPECT_EQ(pointer[0], 0);
    EXPECT_EQ(pointer[1], 0);
    Sample s{};
    std::uint32_t t = 2716;
    for (int i = 0; i < 16; ++i, t += 16) s = Read(h, t, 0, 80, 0);  // full right
    EXPECT_EQ(s.pointer[0], RTHOME_POINTER_FULL / 2);
    for (int i = 0; i < 40; ++i, t += 16) s = Read(h, t, 0, 80, 0);
    EXPECT_EQ(s.pointer[0], RTHOME_POINTER_FULL);  // the right edge
    for (int i = 0; i < 8; ++i, t += 16) s = Read(h, t, 0, 0, 80);  // full up
    EXPECT_EQ(s.pointer[1], -RTHOME_POINTER_FULL / 4);
    // A second read of one sample doesn't move it again.
    std::int32_t again[2];
    rthome_classic(&h, t, &hold, &trig, &release, s.axes);
    rthome_remote(&h, &hold, &trig, &release, again);
    EXPECT_EQ(again[1], s.pointer[1]);
    // At rest it stays.
    s = Read(h, t, 0, 5, -6);
    EXPECT_EQ(s.pointer[0], RTHOME_POINTER_FULL);
    EXPECT_EQ(s.pointer[1], -RTHOME_POINTER_FULL / 4);
    // The next stand-in starts at the centre again.
    rthome g = Fresh();
    g.pointer[0] = 1234;
    HoldCombo(g, 0, 2016);
    EXPECT_EQ(g.pointer[0], 0);
}

// After each stand-in, whether part 1 stays on in this game (rthome_keep).
void TestKeep() {
    {
        rthome h = OpenMenu();  // a menu opened: on, the count starts over
        h.refusals = 1;
        EXPECT_TRUE(rthome_keep(&h, 1));
        EXPECT_EQ(h.refusals, 0u);
    }
    {
        rthome h = Fresh();  // never read: off at once
        HoldCombo(h, 0, 2016);
        (void)rthome_stand_in(&h, 2016 + kPress);  // the press ends
        (void)rthome_stand_in(&h, 2016 + kPress + kWait);  // no menu: refused
        EXPECT_EQ(h.phase, RTHOME_DONE);
        EXPECT_TRUE(!rthome_keep(&h, 0));
    }
    {
        // Read but refused: on after one (Brawl's intro), off after two in a
        // row (Mario Kart Wii); an opened menu between them starts over.
        rthome h = Fresh();
        auto refuse = [&](std::uint32_t at) {
            rthome_pads(&h, Pads().port(1, 0).s, at);  // let go
            HoldCombo(h, at + 16, at + 2100);
            (void)rthome_stand_in(&h, at + 2100 + kPress);
            (void)rthome_stand_in(&h, at + 2100 + kPress + kWait);
            EXPECT_EQ(h.phase, RTHOME_DONE);
            return rthome_keep(&h, 1);
        };
        EXPECT_TRUE(refuse(0));
        EXPECT_TRUE(!refuse(10000));
        h.refusals = 0;
        EXPECT_TRUE(refuse(20000));
        rthome_pads(&h, Pads().port(1, 0).s, 30000);
        HoldCombo(h, 30016, 32100);
        rthome_menu_frame(&h, 32700);  // this time the menu opened
        (void)rthome_stand_in(&h, 32700 + kLinger);
        EXPECT_TRUE(rthome_keep(&h, 1));
        EXPECT_TRUE(refuse(40000));  // one refusal again: still on
    }
    {
        rthome h = Fresh();  // every channel has a controller: no stand-in, proves nothing
        for (std::uint32_t c = 0; c < 4; ++c) rthome_probed(&h, c, 0);
        HoldCombo(h, 0, 2100);
        EXPECT_EQ(h.phase, RTHOME_DONE);
        EXPECT_TRUE(rthome_keep(&h, 0));
    }
}

// When the blob may read the pads itself at a probe (the game stopped
// calling PADRead): with a stand-in up, or a HOME Menu running, not otherwise.
void TestReadsPads() {
    constexpr std::uint32_t kWindow = 100;
    rthome h = Fresh();
    EXPECT_TRUE(!rthome_reads_pads(&h, 1000, kWindow));  // an ordinary probe: no
    HoldCombo(h, 0, 2016);
    EXPECT_TRUE(rthome_reads_pads(&h, 2016, kWindow));   // pressing
    (void)rthome_stand_in(&h, 2016 + kPress);
    EXPECT_TRUE(rthome_reads_pads(&h, 2016 + kPress, kWindow));  // waiting for the menu
    rthome_menu_frame(&h, 2700);
    (void)rthome_stand_in(&h, 2700 + kLinger);  // closed again
    EXPECT_EQ(h.phase, RTHOME_DONE);
    // A Virtual Console game's own HOME Menu, no stand-in: HBMCalc each frame,
    // the emulator (and its PADRead) stopped.
    rthome v = Fresh();
    rthome_menu_frame(&v, 5000);
    EXPECT_TRUE(rthome_reads_pads(&v, 5000 + kWindow - 1, kWindow));
    EXPECT_TRUE(!rthome_reads_pads(&v, 5000 + kWindow, kWindow));  // the menu has closed
    EXPECT_TRUE(!rthome_reads_pads(&v, 60000, kWindow));            // a loading screen later
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
    TestEvenTicks();
    TestOperationsGuide();
    TestClosedByHome();
    TestClosedByPointer();
    TestButtonsInTheMenu();
    TestRemoteSide();
    TestPointer();
    TestKeep();
    TestReadsPads();
    TestFloatBits();
    if (g_failures == 0) std::cout << "padhome: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
