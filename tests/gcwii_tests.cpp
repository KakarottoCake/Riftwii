// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// GameCube controllers as Classic Controllers (runtime/rtgcwii.h, #587):
// a controller in port N stands in on channel N-1 while it's plugged in,
// a short gap doesn't unplug it, a real controller on the channel wins and
// the stand-in comes back after it, the game's connect callback hears of
// each change once, and the buttons and sticks follow #587's default
// layout. Also the HOME Menu's stand-in taking the player's own channel.
#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" {
#include "rtgcwii.h"
#include "rthome.h"
}

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { const auto expect_a_ = (a); const auto expect_b_ = (b); if (expect_a_ != expect_b_) { std::cerr << "FAILED: " #a " == " #b " (" << expect_a_ << " != " << expect_b_ << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uint32_t kGcA = 0x0100;
constexpr std::uint32_t kGcB = 0x0200;
constexpr std::uint32_t kGcZ = 0x0010;
constexpr std::uint32_t kGcStart = 0x1000;
constexpr std::uint32_t kGcCombo = 0x1070;  // Start, L, R, Z
constexpr std::uint32_t kGrace = 300;        // ticks (1 a millisecond here)

// Four PADStatus entries; a port with no controller reports error -1.
struct Pads {
    std::uint8_t s[48];
    Pads() {
        std::memset(s, 0, sizeof(s));
        for (int i = 0; i < 4; ++i) s[i * 12 + 10] = 0xFF;
    }
    void plug(int port, std::uint32_t buttons = 0, std::int8_t stick_x = 0) {
        std::uint8_t* p = s + port * 12;
        p[0] = static_cast<std::uint8_t>(buttons >> 8);
        p[1] = static_cast<std::uint8_t>(buttons);
        p[2] = static_cast<std::uint8_t>(stick_x);
        p[10] = 0;
    }
    void unplug(int port) { s[port * 12 + 10] = 0xFF; }
};

rtgcwii fresh() {
    rtgcwii w;
    std::memset(&w, 0, sizeof(w));
    w.slot.grace = kGrace;
    return w;
}

void TestComesAndGoes() {
    rtgcwii w = fresh();
    Pads pads;
    std::uint32_t leave = 0, join = 0;
    rtgcwii_pads(&w, pads.s, 1000);
    EXPECT_EQ(rtgcwii_on(&w, 0), 0);
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(join, 0u);

    // Port 2 plugged in: channel 1 (port N is channel N-1), told once.
    pads.plug(1);
    rtgcwii_pads(&w, pads.s, 1016);
    EXPECT_EQ(rtgcwii_on(&w, 1), 1);
    EXPECT_EQ(rtgcwii_on(&w, 0), 0);
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(join, 2u);
    EXPECT_EQ(leave, 0u);
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(join, 0u);

    // A dropped report or two: still there.
    pads.unplug(1);
    rtgcwii_pads(&w, pads.s, 1100);
    EXPECT_EQ(rtgcwii_on(&w, 1), 1);
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(leave, 0u);
    // Back before the grace period ended: nothing to tell.
    pads.plug(1);
    rtgcwii_pads(&w, pads.s, 1200);
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(leave | join, 0u);

    // Unplugged for longer: gone, told once.
    pads.unplug(1);
    rtgcwii_pads(&w, pads.s, 1300);
    rtgcwii_pads(&w, pads.s, 1300 + kGrace);
    EXPECT_EQ(rtgcwii_on(&w, 1), 0);
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(leave, 2u);
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(leave, 0u);
}

void TestRealControllerWins() {
    rtgcwii w = fresh();
    Pads pads;
    std::uint32_t leave = 0, join = 0;
    pads.plug(0);
    rtgcwii_pads(&w, pads.s, 1000);
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(join, 1u);

    // A real Wii Remote on channel 0: the stand-in steps aside, and the
    // game isn't told the channel's controller left (it has one there).
    rtgcwii_probed(&w, 0, 0);
    EXPECT_EQ(rtgcwii_on(&w, 0), 0);
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(leave | join, 0u);
    // A busy answer changes nothing.
    rtgcwii_probed(&w, 0, -2);
    EXPECT_EQ(rtgcwii_on(&w, 0), 0);

    // The Wii Remote turned off (the game's library told the game): the
    // stand-in is back and announced afresh.
    rtgcwii_probed(&w, 0, -1);
    EXPECT_EQ(rtgcwii_on(&w, 0), 1);
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(join, 1u);
}

void TestButtonsAndSticks() {
    rtgcwii w = fresh();
    Pads pads;
    std::uint32_t hold = 0, trig = 0, release = 0;
    std::int32_t axes[6];
    // Plugged in with A already held: held, not newly pressed.
    pads.plug(0, kGcA, 72);
    rtgcwii_pads(&w, pads.s, 1000);
    rtgcwii_classic(&w, 0, &hold, &trig, &release, axes);
    EXPECT_EQ(hold, RTHOME_CL_A);
    EXPECT_EQ(trig, 0u);
    EXPECT_EQ(axes[0], RTHOME_AXIS_FULL);  // full tilt right
    EXPECT_EQ(axes[1], 0);

    // B and Z pressed, A let go: Start is +, Z is ZR (#587's layout).
    pads.plug(0, kGcB | kGcZ | kGcStart);
    rtgcwii_pads(&w, pads.s, 1016);
    rtgcwii_classic(&w, 0, &hold, &trig, &release, axes);
    EXPECT_EQ(hold, RTHOME_CL_B | RTHOME_CL_ZR | RTHOME_CL_PLUS);
    EXPECT_EQ(trig, RTHOME_CL_B | RTHOME_CL_ZR | RTHOME_CL_PLUS);
    EXPECT_EQ(release, RTHOME_CL_A);
    // A second read before the next PADRead: the same words (a game may
    // read twice a frame and must still see the press).
    rtgcwii_classic(&w, 0, &hold, &trig, &release, axes);
    EXPECT_EQ(trig, RTHOME_CL_B | RTHOME_CL_ZR | RTHOME_CL_PLUS);

    // A channel without the stand-in reads nothing.
    rtgcwii_classic(&w, 3, &hold, &trig, &release, axes);
    EXPECT_EQ(hold | trig | release, 0u);
}

// The HOME Menu's stand-in on a port that already has a permanent one
// takes that port's channel, not a free one.
void TestHomeOnOwnChannel() {
    rthome h;
    std::memset(&h, 0, sizeof(h));
    h.hold = 100;
    h.press = 50;
    h.wait = 1000;
    h.linger = 100;
    h.channel = RTHOME_NO_CHANNEL;
    h.selected = RTHOME_NO_SELECTION;
    // Every channel probed, 0 and 1 free.
    for (std::uint32_t c = 0; c < 4; ++c) rthome_probed(&h, c, c < 2 ? -1 : 0);
    Pads pads;
    pads.plug(1, kGcCombo);
    rthome_pads_own(&h, pads.s, 1000, 1u << 1, 0u);  // port 2's controller stands in on channel 1
    rthome_pads_own(&h, pads.s, 1200, 1u << 1, 0u);
    EXPECT_EQ(rthome_stand_in(&h, 1200), 1u);
}

// When the blob calls the game's PADRead itself: only when the game hasn't
// within 50 ms, at most every 8 ms, and never fooled by the time base's low
// word wrapping (a signed difference 35 s after the game's one PADRead
// looked recent, and Dragon Knight's save screens took no presses).
void TestShouldRead() {
    constexpr std::uint32_t kTicksPerMs = 1000;  // the tests' clock
    rtgcwii w = fresh();
    // The game never called PADRead: read, then not again within 8 ms.
    EXPECT_EQ(rtgcwii_should_read(&w, 1000000, kTicksPerMs), 1);
    EXPECT_EQ(rtgcwii_should_read(&w, 1000000 + 7 * kTicksPerMs, kTicksPerMs), 0);
    EXPECT_EQ(rtgcwii_should_read(&w, 1000000 + 9 * kTicksPerMs, kTicksPerMs), 1);
    // The game reads its pads itself: the blob stays out while it does.
    rtgcwii_read_by(&w, 2000000, 0);
    EXPECT_EQ(rtgcwii_should_read(&w, 2000000 + 20 * kTicksPerMs, kTicksPerMs), 0);
    // The blob's own reads don't count as the game's.
    rtgcwii_read_by(&w, 2000000 + 30 * kTicksPerMs, 1);
    EXPECT_EQ(rtgcwii_should_read(&w, 2000000 + 60 * kTicksPerMs, kTicksPerMs), 1);
    // The game called it once, long ago (more than half the time base's
    // range): still stale, and forgotten, not "just now".
    rtgcwii v = fresh();
    rtgcwii_read_by(&v, 0x10000000u, 0);
    EXPECT_EQ(rtgcwii_should_read(&v, 0x10000000u + 0x90000000u, kTicksPerMs), 1);
    EXPECT_EQ(v.game_read, 0u);
    // ... and once forgotten, the low word coming round again can't bring it back.
    EXPECT_EQ(rtgcwii_should_read(&v, 0x10000000u + 9 * kTicksPerMs, kTicksPerMs), 1);
    // Not while its own call is under way (the game's PADRead runs the hook).
    v.reading = 1;
    EXPECT_EQ(rtgcwii_should_read(&v, 0x20000000u, kTicksPerMs), 0);
}

// A press the blob read at a WPADProbe, before the game's next KPAD read,
// still reaches the game as newly pressed, even after another sample of it
// held; and a game whose connect callback wasn't set yet is told later.
void TestPressBetweenReadsAndLateCallback() {
    rtgcwii w = fresh();
    Pads pads;
    std::uint32_t hold = 0, trig = 0, release = 0;
    std::int32_t axes[6];
    pads.plug(0);
    rtgcwii_pads(&w, pads.s, 1000);
    rtgcwii_classic(&w, 0, &hold, &trig, &release, axes);
    EXPECT_EQ(trig, 0u);
    pads.plug(0, kGcA);
    rtgcwii_pads(&w, pads.s, 1010);  // at a probe
    rtgcwii_pads(&w, pads.s, 1020);  // at the KPAD read, A still held
    rtgcwii_classic(&w, 0, &hold, &trig, &release, axes);
    EXPECT_EQ(trig, RTHOME_CL_A);
    rtgcwii_classic(&w, 0, &hold, &trig, &release, axes);  // read twice
    EXPECT_EQ(trig, RTHOME_CL_A);
    rtgcwii_pads(&w, pads.s, 1036);
    rtgcwii_classic(&w, 0, &hold, &trig, &release, axes);
    EXPECT_EQ(trig, 0u);  // held, already got
    EXPECT_EQ(rtgcwii_hold(&w, 0), RTHOME_CL_A);

    std::uint32_t leave = 0, join = 0;
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(join, 1u);
    rtgcwii_not_told(&w, 0);  // no callback yet
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(join, 1u);
    rtgcwii_connect_change(&w, &leave, &join);
    EXPECT_EQ(join, 0u);
    // A channel number past the four is ignored, not shifted out of range.
    rtgcwii_probed(&w, 40, 0);
    EXPECT_EQ(w.slot.real, 0u);
}

// The connect callback hears one change at a time, from what is true now;
// a KPAD read that brings samples marks a real controller before any
// WPADProbe, and "no controller" from the read gives the channel back; the
// HOME Menu's stand-in never takes another player's channel.
void TestOneChangeAtATimeAndKpadRead() {
    rtgcwii w = fresh();
    Pads pads;
    pads.plug(0);
    pads.plug(2);
    rtgcwii_pads(&w, pads.s, 1000);
    std::uint32_t chan = 99;
    std::int32_t reason = 5;
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);
    EXPECT_EQ(chan, 0u);
    EXPECT_EQ(reason, 0);
    // A nested read unplugs port 1 before port 3 is told: the next change
    // comes from the state now, so channel 0 goes, then channel 2 joins.
    pads.unplug(0);
    rtgcwii_pads(&w, pads.s, 1000 + kGrace);
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);
    EXPECT_EQ(chan, 0u);
    EXPECT_EQ(reason, -1);
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);
    EXPECT_EQ(chan, 2u);
    EXPECT_EQ(reason, 0);
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 0);

    // A real Wii Remote's samples on channel 2: it wins at once.
    rtgcwii_kpad_read(&w, 2, 1, RTGCWII_NO_ERROR, 2000);
    EXPECT_EQ(rtgcwii_on(&w, 2), 0);
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 0);  // its library tells the game
    // No samples this frame isn't "gone"; the read's "no controller" is.
    rtgcwii_kpad_read(&w, 2, 0, RTGCWII_NO_ERROR, 2001);
    EXPECT_EQ(rtgcwii_on(&w, 2), 0);
    rtgcwii_kpad_read(&w, 2, 0, -1, 2002);
    EXPECT_EQ(rtgcwii_on(&w, 2), 1);
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);
    EXPECT_EQ(chan, 2u);
    EXPECT_EQ(reason, 0);

    // The HOME Menu's stand-in for port 1 (a real Wii Remote on channel 0,
    // so not its own) skips channel 1, which port 2's stand-in holds, even
    // though the game never asked about channel 1.
    rthome h;
    std::memset(&h, 0, sizeof(h));
    h.hold = 100;
    h.press = 50;
    h.wait = 1000;
    h.linger = 100;
    h.channel = RTHOME_NO_CHANNEL;
    h.selected = RTHOME_NO_SELECTION;
    rthome_probed(&h, 0, 0);
    Pads combo;
    combo.plug(0, kGcCombo);
    rthome_pads_own(&h, combo.s, 1000, 1u << 1, 0u);
    rthome_pads_own(&h, combo.s, 1200, 1u << 1, 0u);
    EXPECT_EQ(rthome_stand_in(&h, 1200), 2u);
    // A real Wii Remote found on channel 2 by KPAD reads since its last
    // probe: the HOME stand-in takes channel 3 instead (CodeRabbit, fork #22).
    rthome r;
    std::memset(&r, 0, sizeof(r));
    r.hold = 100;
    r.press = 50;
    r.wait = 1000;
    r.linger = 100;
    r.channel = RTHOME_NO_CHANNEL;
    r.selected = RTHOME_NO_SELECTION;
    rthome_probed(&r, 0, 0);
    rthome_probed(&r, 2, -1);
    rthome_pads_own(&r, combo.s, 1000, 1u << 1, 1u << 2);
    rthome_pads_own(&r, combo.s, 1200, 1u << 1, 1u << 2);
    EXPECT_EQ(rthome_stand_in(&r, 1200), 3u);
}

// A KPAD read with no error output: a real controller's reads that bring no
// samples give the channel back only after `grace` and enough reads in a
// row (a short gap, or a pause in the game's reading, doesn't count), with
// no WPADProbe needed.
void TestEmptyReadsWithoutError() {
    rtgcwii w = fresh();
    Pads pads;
    pads.plug(1);
    rtgcwii_pads(&w, pads.s, 1000);
    rtgcwii_kpad_read(&w, 1, 2, RTGCWII_NO_ERROR, 1000);
    EXPECT_EQ(rtgcwii_on(&w, 1), 0);
    // A long pause between two empty reads: still there.
    rtgcwii_kpad_read(&w, 1, 0, RTGCWII_NO_ERROR, 1010);
    rtgcwii_kpad_read(&w, 1, 0, RTGCWII_NO_ERROR, 1010 + 10 * kGrace);
    EXPECT_EQ(rtgcwii_on(&w, 1), 0);
    // Samples again start the count over.
    rtgcwii_kpad_read(&w, 1, 1, RTGCWII_NO_ERROR, 5000);
    std::uint32_t t = 5000;
    for (std::uint32_t i = 0; i + 1 < RTGCWII_EMPTY_READS; ++i) {
        t += kGrace;  // long enough, but too few reads yet
        rtgcwii_kpad_read(&w, 1, 0, RTGCWII_NO_ERROR, t);
    }
    EXPECT_EQ(rtgcwii_on(&w, 1), 0);
    rtgcwii_kpad_read(&w, 1, 0, RTGCWII_NO_ERROR, t + 1);
    EXPECT_EQ(rtgcwii_on(&w, 1), 1);
    // Many reads in a row inside the grace period aren't enough either.
    rtgcwii_kpad_read(&w, 1, 1, RTGCWII_NO_ERROR, 20000);
    for (std::uint32_t i = 0; i < 2 * RTGCWII_EMPTY_READS; ++i) {
        rtgcwii_kpad_read(&w, 1, 0, RTGCWII_NO_ERROR, 20000 + i);
    }
    EXPECT_EQ(rtgcwii_on(&w, 1), 0);
    // A burst of empty reads, a pause past the grace period, one more: the
    // pause doesn't count, so the controller stays (CodeRabbit on #609).
    rtgcwii_kpad_read(&w, 1, 1, RTGCWII_NO_ERROR, 24000);
    for (std::uint32_t i = 0; i + 1 < RTGCWII_EMPTY_READS; ++i) rtgcwii_kpad_read(&w, 1, 0, RTGCWII_NO_ERROR, 24000 + i);
    rtgcwii_kpad_read(&w, 1, 0, RTGCWII_NO_ERROR, 24000 + 2 * kGrace);
    EXPECT_EQ(rtgcwii_on(&w, 1), 0);
    // A channel with no real controller is untouched by empty reads.
    rtgcwii_kpad_read(&w, 3, 0, RTGCWII_NO_ERROR, 30000);
    EXPECT_EQ(w.slot.empty_since[3], 0u);
    // A controller that leaves and another that comes in the middle of a
    // count: the new one's count starts over, so one empty read later,
    // past the old grace period, it is still there.
    rtgcwii_kpad_read(&w, 1, 1, RTGCWII_NO_ERROR, 40000);
    t = 40000;
    for (std::uint32_t i = 0; i + 1 < RTGCWII_EMPTY_READS; ++i) {
        t += 1;
        rtgcwii_kpad_read(&w, 1, 0, RTGCWII_NO_ERROR, t);
    }
    rtgcwii_probed(&w, 1, -1);
    rtgcwii_probed(&w, 1, 0);
    rtgcwii_kpad_read(&w, 1, 0, RTGCWII_NO_ERROR, t + 2 * kGrace);
    EXPECT_EQ(rtgcwii_on(&w, 1), 0);
}

// A channel whose callback isn't set yet is passed over for this pass; the
// next channel's change is still told.
void TestSkipMissingCallback() {
    rtgcwii w = fresh();
    Pads pads;
    pads.plug(0);
    pads.plug(1);
    rtgcwii_pads(&w, pads.s, 1000);
    std::uint32_t chan = 99;
    std::int32_t reason = 5;
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);
    EXPECT_EQ(chan, 0u);
    rtgcwii_not_told(&w, 0);  // channel 0's callback isn't set
    EXPECT_EQ(rtgcwii_next_change(&w, 1u, 0, &chan, &reason), 1);
    EXPECT_EQ(chan, 1u);
    EXPECT_EQ(reason, 0);
    EXPECT_EQ(rtgcwii_next_change(&w, 3u, 0, &chan, &reason), 0);
    // Next pass: channel 0 again.
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);
    EXPECT_EQ(chan, 0u);

    // A "gone" that couldn't be told stays pending.
    pads.unplug(1);
    rtgcwii_pads(&w, pads.s, 1000 + kGrace);
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);
    EXPECT_EQ(chan, 1u);
    EXPECT_EQ(reason, -1);
    rtgcwii_still_told(&w, 1);
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);
    EXPECT_EQ(chan, 1u);
    EXPECT_EQ(reason, -1);
}

// A dropped report doesn't give a press again; a channel the HOME Menu's
// stand-in holds isn't reported gone until it lets go.
void TestDroppedReportAndHomeKeep() {
    rtgcwii w = fresh();
    Pads pads;
    std::uint32_t hold = 0, trig = 0, release = 0;
    std::int32_t axes[6];
    pads.plug(0);
    rtgcwii_pads(&w, pads.s, 1000);
    rtgcwii_classic(&w, 0, &hold, &trig, &release, axes);
    pads.plug(0, kGcA);
    rtgcwii_pads(&w, pads.s, 1016);
    rtgcwii_classic(&w, 0, &hold, &trig, &release, axes);
    EXPECT_EQ(trig, RTHOME_CL_A);
    pads.unplug(0);                    // a dropped report
    rtgcwii_pads(&w, pads.s, 1032);
    rtgcwii_classic(&w, 0, &hold, &trig, &release, axes);
    EXPECT_EQ(trig, 0u);               // not pressed again
    EXPECT_EQ(hold, RTHOME_CL_A);      // still held

    std::uint32_t chan = 99;
    std::int32_t reason = 5;
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);  // channel 0 joined
    rtgcwii_pads(&w, pads.s, 1032 + kGrace);                       // unplugged for good
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 1u, &chan, &reason), 0);  // the HOME Menu's stand-in has it
    EXPECT_EQ(rtgcwii_next_change(&w, 0, 0, &chan, &reason), 1);   // let go: gone now
    EXPECT_EQ(reason, -1);
}

}  // namespace

// A real controller's channels are checked with the game's WPADProbe every
// 250 ms while one is real, whether or not the game reads them (#609).
static void TestRealCheckDue() {
    rtgcwii w{};
    const std::uint32_t ms = 10;  // ticks per ms
    EXPECT_TRUE(!rtgcwii_real_check_due(&w, 1000, ms));  // no real controller: nothing to check
    w.slot.real = 1u << 2;
    EXPECT_TRUE(rtgcwii_real_check_due(&w, 1000, ms));
    EXPECT_TRUE(!rtgcwii_real_check_due(&w, 1000 + 249 * ms, ms));
    EXPECT_TRUE(rtgcwii_real_check_due(&w, 1000 + 250 * ms, ms));
    // The Wii Remote found gone: the channel is the stand-in's again.
    rtgcwii_probed(&w, 2, -1);
    EXPECT_TRUE(w.slot.real == 0);
    EXPECT_TRUE(!rtgcwii_real_check_due(&w, 1000 + 600 * ms, ms));
}

// Each probe result counts (rtgcwii_probe_count), busy doesn't: a caller
// that took the count before a slow probe applies its answer only if no
// newer one came in between (CodeRabbit on #610).
void TestProbeCount() {
    rtgcwii w = fresh();
    EXPECT_EQ(rtgcwii_probe_count(&w, 1), 0u);
    rtgcwii_probed(&w, 1, 0);
    EXPECT_EQ(rtgcwii_probe_count(&w, 1), 1u);
    rtgcwii_probed(&w, 1, -2);  // busy: no answer
    EXPECT_EQ(rtgcwii_probe_count(&w, 1), 1u);
    rtgcwii_probed(&w, 1, -1);
    EXPECT_EQ(rtgcwii_probe_count(&w, 1), 2u);
    EXPECT_EQ(rtgcwii_probe_count(&w, 0), 0u);
    EXPECT_EQ(rtgcwii_probe_count(&w, RTGCWII_CHANNELS), 0u);
}

int main() {
    TestProbeCount();
    TestComesAndGoes();
    TestRealControllerWins();
    TestButtonsAndSticks();
    TestHomeOnOwnChannel();
    TestShouldRead();
    TestRealCheckDue();
    TestPressBetweenReadsAndLateCallback();
    TestOneChangeAtATimeAndKpadRead();
    TestEmptyReadsWithoutError();
    TestSkipMissingCallback();
    TestDroppedReportAndHomeKeep();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "gcwii tests passed" << std::endl;
    return 0;
}
