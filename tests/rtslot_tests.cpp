// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Slot ownership (runtime/rtslot.h), the stand-in's one place for "who
// holds each Wii Remote slot, and what the game has been told". Each test
// is a past review finding on #587's pull requests, written as the
// situation and the outcome it needs, so the rules hold whatever the hooks
// around them become (notes: the restructure's findings catalogue).
#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" {
#include "rtslot.h"
}

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { const auto expect_a_ = (a); const auto expect_b_ = (b); if (expect_a_ != expect_b_) { std::cerr << "FAILED: " #a " == " #b " (" << expect_a_ << " != " << expect_b_ << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uint32_t kGrace = 300;  // ticks (1 a millisecond here)
constexpr std::uint32_t kNone = RTSLOT_NO_SLOT;

rtslot fresh() {
    rtslot s;
    std::memset(&s, 0, sizeof(s));
    s.grace = kGrace;
    return s;
}

void plug(rtslot& s, std::uint32_t slot) { s.present |= 1u << slot; }
void unplug(rtslot& s, std::uint32_t slot) { s.present &= ~(1u << slot); }

// Tells every change due now (each delivered), as the hook's pass does;
// returns how many "connected" (joins) and "gone" (leaves) were told.
struct Told {
    int joins = 0, leaves = 0;
};
Told tell_all(rtslot& s, std::uint32_t home = kNone) {
    Told t;
    std::uint32_t skip = 0, slot = 0;
    std::int32_t reason = 0;
    while (rtslot_next_tell(&s, home, skip, &slot, &reason)) {
        (reason == 0 ? t.joins : t.leaves) += 1;
        rtslot_told(&s, slot, reason, 1);
        skip |= 1u << slot;
    }
    return t;
}

// Who holds a slot: a real controller first, then the HOME Menu's stand-in
// on its own slot, then the permanent stand-in where a pad is plugged in.
void TestOwnerOrder() {
    rtslot s = fresh();
    EXPECT_EQ(rtslot_owner(&s, 0, kNone), RTSLOT_NOBODY);
    plug(s, 0);
    EXPECT_EQ(rtslot_owner(&s, 0, kNone), RTSLOT_STANDIN);
    EXPECT_EQ(rtslot_owner(&s, 0, 0), RTSLOT_HOME);
    rtslot_probed(&s, 0, 0);  // a real Wii Remote answered there
    EXPECT_EQ(rtslot_owner(&s, 0, 0), RTSLOT_REAL);
    EXPECT_EQ(rtslot_owner(&s, 0, kNone), RTSLOT_REAL);
    EXPECT_EQ(rtslot_owner(&s, 9, kNone), RTSLOT_NOBODY);  // past the four slots
}

// #609 (CodeRabbit): a real Wii Remote's samples are kept when the HOME
// Menu's stand-in shares its slot: the HOME stand-in yields to it.
void TestRealBeatsHome() {
    rtslot s = fresh();
    rtslot_probed(&s, 1, 0);
    EXPECT_EQ(rtslot_owner(&s, 1, 1), RTSLOT_REAL);  // the HOME stand-in's slot, a real one there
    EXPECT_EQ(rtslot_owner(&s, 2, 2), RTSLOT_HOME);
}

// A real Wii Remote that takes a slot the stand-in was told on: the game
// hears neither "gone" nor "connected" from the stand-in (its own library
// tells it about the real one).
void TestRealTakesToldSlot() {
    rtslot s = fresh();
    plug(s, 0);
    EXPECT_EQ(tell_all(s).joins, 1);
    rtslot_probed(&s, 0, 0);
    const Told t = tell_all(s);
    EXPECT_EQ(t.leaves, 0);
    EXPECT_EQ(t.joins, 0);
}

// The HOME Menu's stand-in's slot is never told "gone" while it is up.
void TestHomeKeepsItsSlot() {
    rtslot s = fresh();
    plug(s, 2);
    EXPECT_EQ(tell_all(s).joins, 1);
    unplug(s, 2);
    EXPECT_EQ(tell_all(s, 2).leaves, 0);
    EXPECT_EQ(tell_all(s, kNone).leaves, 1);
}

// #10 (fork, CodeRabbit): a "connected" the game couldn't take (no callback
// yet) isn't marked told; it's told at the next pass. The same for "gone".
void TestUndeliveredStaysToTell() {
    rtslot s = fresh();
    plug(s, 0);
    std::uint32_t slot = 0;
    std::int32_t reason = 1;
    EXPECT_TRUE(rtslot_next_tell(&s, kNone, 0, &slot, &reason));
    EXPECT_EQ(reason, 0);
    EXPECT_EQ(rtslot_told(&s, slot, reason, 0), RTSLOT_AFTER_NOTHING);
    EXPECT_EQ(tell_all(s).joins, 1);  // tried again
    unplug(s, 0);
    EXPECT_TRUE(rtslot_next_tell(&s, kNone, 0, &slot, &reason));
    EXPECT_EQ(reason, -1);
    rtslot_told(&s, slot, reason, 0);
    EXPECT_EQ(tell_all(s).leaves, 1);
}

// A "gone" whose slot a real controller took meanwhile is followed by
// "connected", so the game hears last that a controller is there.
void TestGoneThenRealConnected() {
    rtslot s = fresh();
    plug(s, 3);
    tell_all(s);
    unplug(s, 3);
    std::uint32_t slot = 0;
    std::int32_t reason = 0;
    EXPECT_TRUE(rtslot_next_tell(&s, kNone, 0, &slot, &reason));
    EXPECT_EQ(reason, -1);
    rtslot_probed(&s, 3, 0);  // a real Wii Remote arrives while "gone" is told
    EXPECT_EQ(rtslot_told(&s, slot, reason, 1), RTSLOT_AFTER_CONNECTED);
}

// An older probe answer applied late doesn't undo a newer one (the caller
// takes the count first, and applies only if it hasn't moved).
void TestProbeCountGuard() {
    rtslot s = fresh();
    const std::uint32_t before = rtslot_probe_count(&s, 0);
    rtslot_probed(&s, 0, 0);  // a newer answer: a real Wii Remote
    EXPECT_TRUE(rtslot_probe_count(&s, 0) != before);
    rtslot_probed(&s, 0, -2);  // busy: no answer, no count
    EXPECT_EQ(rtslot_probe_count(&s, 0), before + 1u);
}

// #609 (CodeRabbit): a pause in the game's KPAD reads isn't a controller
// gone; a steady stream of empty reads for the grace period is (pr2b:
// and the count starts over when a controller comes back).
void TestEmptyReads() {
    rtslot s = fresh();
    rtslot_kpad_read(&s, 1, 2, RTSLOT_NO_ERROR, 1000);
    EXPECT_EQ(rtslot_owner(&s, 1, kNone), RTSLOT_REAL);
    for (std::uint32_t i = 0; i + 1 < RTSLOT_EMPTY_READS; ++i) rtslot_kpad_read(&s, 1, 0, RTSLOT_NO_ERROR, 1001 + i);
    rtslot_kpad_read(&s, 1, 0, RTSLOT_NO_ERROR, 1001 + 3 * kGrace);  // after a long pause
    EXPECT_EQ(rtslot_owner(&s, 1, kNone), RTSLOT_REAL);
    std::uint32_t t = 5000;
    for (std::uint32_t i = 0; i <= RTSLOT_EMPTY_READS; ++i) {
        t += kGrace / 10;
        rtslot_kpad_read(&s, 1, 0, RTSLOT_NO_ERROR, t);
    }
    EXPECT_EQ(rtslot_owner(&s, 1, kNone), RTSLOT_NOBODY);
    rtslot_kpad_read(&s, 1, 0, -1, t + 1);  // "no controller" with an error output: gone at once
    rtslot_kpad_read(&s, 1, 1, RTSLOT_NO_ERROR, t + 2);
    EXPECT_EQ(rtslot_owner(&s, 1, kNone), RTSLOT_REAL);
    EXPECT_EQ(s.empty_since[1], 0u);  // a controller there: no empty count carried over
}

// #610 (CodeRabbit): the recheck of a real Wii Remote runs the game's
// WPADProbe with interrupts on; a hooked probe or a KPAD read with samples
// meanwhile is newer, and the recheck's older "no controller" mustn't undo it.
void TestRecheckOlderAnswerLoses() {
    rtslot s = fresh();
    plug(s, 2);
    rtslot_probed(&s, 2, 0);
    std::uint32_t begun = rtslot_recheck_begin(&s, 2);
    rtslot_probed(&s, 2, 0);              // the game's own probe, meanwhile
    rtslot_rechecked(&s, 2, -1, begun);   // the recheck's older answer
    EXPECT_EQ(rtslot_owner(&s, 2, kNone), RTSLOT_REAL);
    begun = rtslot_recheck_begin(&s, 2);
    rtslot_kpad_read(&s, 2, 3, RTSLOT_NO_ERROR, 500);
    rtslot_rechecked(&s, 2, -1, begun);
    EXPECT_EQ(rtslot_owner(&s, 2, kNone), RTSLOT_REAL);
    begun = rtslot_recheck_begin(&s, 2);
    rtslot_rechecked(&s, 2, -1, begun);   // nothing newer: it counts
    EXPECT_EQ(rtslot_owner(&s, 2, kNone), RTSLOT_STANDIN);
}

// The HOME Menu's stand-in on a slot the permanent stand-in holds (or told
// the game of, its controller just unplugged): the game has a controller
// there already, so the HOME one isn't announced.
void TestHomeAnnounced() {
    rtslot s = fresh();
    EXPECT_EQ(rtslot_home_announced(&s, 1), 1u);
    EXPECT_EQ(rtslot_home_announced(&s, kNone), kNone);
    plug(s, 1);
    EXPECT_EQ(rtslot_home_announced(&s, 1), kNone);
    unplug(s, 1);
    s.told = 1u << 1;
    EXPECT_EQ(rtslot_home_announced(&s, 1), kNone);
    // A real Wii Remote there: its library tells the game (fork #22).
    rtslot r = fresh();
    rtslot_probed(&r, 2, 0);
    EXPECT_EQ(rtslot_home_announced(&r, 2), kNone);
    EXPECT_EQ(rtslot_home_announced(&r, 3), 3u);
}

// #609 (Cake's review): while one telling runs, a nested PADRead leaves
// the permanent stand-in's news to it; a count, so two at once each give
// back only their own share.
void TestTellingCount() {
    rtslot s = fresh();
    EXPECT_TRUE(!rtslot_tell_busy(&s));
    rtslot_tell_take(&s);
    rtslot_tell_take(&s);
    rtslot_tell_give(&s);
    EXPECT_TRUE(rtslot_tell_busy(&s));
    rtslot_tell_give(&s);
    EXPECT_TRUE(!rtslot_tell_busy(&s));
}

}  // namespace

// Fork #22 (CodeRabbit): a corrective "connected" delivered after the real
// controller had already left: the slot counts as told, so "gone" follows;
// with the controller still there, nothing more is told.
void TestCorrectedAfterLeave() {
    rtslot s = fresh();
    rtslot_probed(&s, 1, 0);   // a real Wii Remote on slot 1
    rtslot_probed(&s, 1, -1);  // gone before the correction was delivered
    rtslot_corrected(&s, 1);
    EXPECT_EQ(tell_all(s).leaves, 1);
    rtslot t = fresh();
    rtslot_probed(&t, 1, 0);
    rtslot_corrected(&t, 1);
    EXPECT_EQ(tell_all(t).leaves, 0);
    // A GameCube controller behind the slot when the correction went out,
    // unplugged before the next pass: "gone" follows (CodeRabbit CLI).
    rtslot u = fresh();
    plug(u, 1);
    rtslot_probed(&u, 1, 0);
    rtslot_probed(&u, 1, -1);
    rtslot_corrected(&u, 1);
    unplug(u, 1);
    EXPECT_EQ(tell_all(u).leaves, 1);
}

// Fork #22 (CodeRabbit): a real Wii Remote took the slot the HOME Menu's
// stand-in held; the game isn't told "gone" for a controller that's there.
void TestHomeLeaveNotForReal() {
    rtslot s = fresh();
    EXPECT_TRUE(rtslot_home_may_leave(&s, 0));
    rtslot_probed(&s, 0, 0);
    EXPECT_TRUE(!rtslot_home_may_leave(&s, 0));
    EXPECT_TRUE(rtslot_home_may_leave(&s, kNone));
    // A GameCube controller on the HOME Menu's slot, already told
    // connected by the permanent stand-in: no "gone" from the HOME Menu's
    // end (fork #22's second review).
    rtslot t = fresh();
    plug(t, 2);
    EXPECT_TRUE(rtslot_home_may_leave(&t, 2));
    EXPECT_EQ(tell_all(t).joins, 1);
    EXPECT_TRUE(!rtslot_home_may_leave(&t, 2));
}

// Fork #22: a gap of exactly the grace between two empty reads keeps the
// run counting; one tick more starts it over (from an even tick too).
void TestEmptyGapExact() {
    rtslot s = fresh();
    rtslot_kpad_read(&s, 1, 2, RTSLOT_NO_ERROR, 1000);
    rtslot_kpad_read(&s, 1, 0, RTSLOT_NO_ERROR, 2000);
    rtslot_kpad_read(&s, 1, 0, RTSLOT_NO_ERROR, 2000 + kGrace);
    EXPECT_EQ(s.empty_reads[1], 2u);
    rtslot_kpad_read(&s, 1, 0, RTSLOT_NO_ERROR, 2000 + 2 * kGrace + 1);
    EXPECT_EQ(s.empty_reads[1], 1u);
    // From an odd tick.
    rtslot o = fresh();
    rtslot_kpad_read(&o, 1, 2, RTSLOT_NO_ERROR, 1000);
    rtslot_kpad_read(&o, 1, 0, RTSLOT_NO_ERROR, 2001);
    rtslot_kpad_read(&o, 1, 0, RTSLOT_NO_ERROR, 2001 + kGrace);
    EXPECT_EQ(o.empty_reads[1], 2u);
    rtslot_kpad_read(&o, 1, 0, RTSLOT_NO_ERROR, 2001 + 2 * kGrace + 1);
    EXPECT_EQ(o.empty_reads[1], 1u);
}

int main() {
    TestEmptyGapExact();
    TestOwnerOrder();
    TestRealBeatsHome();
    TestRealTakesToldSlot();
    TestHomeKeepsItsSlot();
    TestUndeliveredStaysToTell();
    TestGoneThenRealConnected();
    TestProbeCountGuard();
    TestEmptyReads();
    TestRecheckOlderAnswerLoses();
    TestHomeAnnounced();
    TestTellingCount();
    TestCorrectedAfterLeave();
    TestHomeLeaveNotForReal();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "rtslot tests passed" << std::endl;
    return 0;
}
