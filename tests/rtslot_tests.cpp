// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Slot ownership (runtime/rtslot.h), the stand-in's one place for "who
// holds each Wii Remote slot, and what the game has been told". Each test
// is a past review finding on #587's pull requests (KakarottoCake/Riftwii
// #609, #610, #616 and their fork copies), written as the situation and
// the outcome it needs, so the rules hold whatever the hooks around them
// become (notes: the restructure's findings catalogue).
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
    int joins = 0, leaves = 0, extensions = 0;
};
Told tell_all(rtslot& s, std::uint32_t home = kNone, int remote_shapes = 1) {
    Told t;
    std::uint32_t skip = 0, slot = 0;
    std::int32_t reason = 0;
    while (rtslot_next_tell(&s, home, remote_shapes, skip, &slot, &reason)) {
        (reason == 0 ? t.joins : t.leaves) += 1;
        if (rtslot_told(&s, slot, reason, 1) == RTSLOT_AFTER_EXTENSION) ++t.extensions;
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

// #610 (CodeRabbit): when the HOME Menu ends on a slot a real Wii Remote
// now holds, the game isn't told "gone" for it.
void TestHomeEndsOnRealSlot() {
    rtslot s = fresh();
    plug(s, 0);
    EXPECT_EQ(tell_all(s).joins, 1);
    rtslot_probed(&s, 0, 0);  // a real Wii Remote took slot 0
    const Told t = tell_all(s);
    EXPECT_EQ(t.leaves, 0);
    EXPECT_EQ(t.joins, 0);
}

// #616 (CodeRabbit): while the HOME Menu's stand-in holds a slot in another
// shape (a Classic Controller, in a Wii Remote form), the permanent
// stand-in's "connected" there waits until it ends; then it's told.
void TestHomeHoldsDifferentShape() {
    rtslot s = fresh();
    rtslot_home_took(&s, 0);  // nothing of the stand-in's there: Classic-shaped HOME
    EXPECT_EQ(rtslot_home_keeps_shape(&s, 0), 0);
    plug(s, 0);               // a GameCube controller plugged in under the menu
    EXPECT_EQ(tell_all(s, 0).joins, 0);
    EXPECT_EQ(tell_all(s, kNone).joins, 1);  // the menu ended
    // The Classic form has no other shape to hold for: told at once.
    rtslot c = fresh();
    rtslot_home_took(&c, 0);
    plug(c, 0);
    EXPECT_EQ(tell_all(c, 0, 0).joins, 1);
}

// #12 (fork, CodeRabbit): the HOME stand-in keeps the shape it took the
// slot in, though the permanent stand-in's pad unplugs under it.
void TestHomeKeepsShapeAfterUnplug() {
    rtslot s = fresh();
    plug(s, 2);
    EXPECT_EQ(tell_all(s).joins, 1);
    rtslot_home_took(&s, 2);
    EXPECT_EQ(rtslot_home_keeps_shape(&s, 2), 1);
    unplug(s, 2);
    EXPECT_EQ(rtslot_home_keeps_shape(&s, 2), 1);
    EXPECT_EQ(tell_all(s, 2).leaves, 0);  // never "gone" while the menu has it
    EXPECT_EQ(tell_all(s, kNone).leaves, 1);
}

// #10 (fork, CodeRabbit): a "connected" the game couldn't take (no callback
// yet) isn't marked told; it's told at the next pass. The same for "gone".
void TestUndeliveredStaysToTell() {
    rtslot s = fresh();
    plug(s, 0);
    std::uint32_t slot = 0;
    std::int32_t reason = 1;
    EXPECT_TRUE(rtslot_next_tell(&s, kNone, 1, 0, &slot, &reason));
    EXPECT_EQ(reason, 0);
    EXPECT_EQ(rtslot_told(&s, slot, reason, 0), RTSLOT_AFTER_NOTHING);
    EXPECT_EQ(tell_all(s).joins, 1);  // tried again
    unplug(s, 0);
    EXPECT_TRUE(rtslot_next_tell(&s, kNone, 1, 0, &slot, &reason));
    EXPECT_EQ(reason, -1);
    rtslot_told(&s, slot, reason, 0);
    EXPECT_EQ(tell_all(s).leaves, 1);
}

// A delivered "connected" on a slot that is still the stand-in's tells its
// attachment too; a "gone" whose slot a real controller took meanwhile
// (wpsx, ws2: CodeRabbit CLI) is followed by "connected", so the game hears
// last that a controller is there.
void TestAfterTelling() {
    rtslot s = fresh();
    plug(s, 3);
    EXPECT_EQ(tell_all(s).extensions, 1);
    unplug(s, 3);
    std::uint32_t slot = 0;
    std::int32_t reason = 0;
    EXPECT_TRUE(rtslot_next_tell(&s, kNone, 1, 0, &slot, &reason));
    EXPECT_EQ(reason, -1);
    rtslot_probed(&s, 3, 0);  // a real Wii Remote arrives while "gone" is told
    EXPECT_EQ(rtslot_told(&s, slot, reason, 1), RTSLOT_AFTER_CONNECTED);
}

// #610 (CodeRabbit): an older probe answer applied late doesn't undo a
// newer one (the caller takes the count first, and applies only if it
// hasn't moved).
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
    rtslot_kpad_read(&s, 1, 1, RTSLOT_NO_ERROR, t + 1);  // a real controller again
    EXPECT_EQ(rtslot_owner(&s, 1, kNone), RTSLOT_REAL);
    rtslot_kpad_read(&s, 1, 0, -1, t + 2);  // "no controller" with an error output: gone at once
    EXPECT_EQ(rtslot_owner(&s, 1, kNone), RTSLOT_NOBODY);
    rtslot_kpad_read(&s, 1, 1, RTSLOT_NO_ERROR, t + 3);
    EXPECT_EQ(rtslot_owner(&s, 1, kNone), RTSLOT_REAL);
    EXPECT_EQ(s.empty_since[1], 0u);  // a controller there: no empty count carried over
}

// ws15 (CodeRabbit CLI): a new "connected" clears `heard`, so only a read
// after it says the game heard it; ws2: a channel due a retell keeps its
// told state.
void TestHeardAndRetell() {
    rtslot s = fresh();
    s.retell_ticks = 100;
    plug(s, 0);
    tell_all(s);
    rtslot_heard(&s, 0);
    EXPECT_TRUE((s.heard & 1u) != 0);
    unplug(s, 0);
    tell_all(s);
    plug(s, 0);
    tell_all(s);
    EXPECT_EQ(s.heard & 1u, 0u);  // a new "connected": not heard yet
    rtslot_retell(&s, 1000);
    rtslot_retell(&s, 1200);  // unheard for retell_ticks: due again
    EXPECT_TRUE((s.retell & 1u) != 0);
    EXPECT_TRUE((s.told & 1u) != 0);  // still counts as told meanwhile
    EXPECT_EQ(tell_all(s).joins, 1);
    // Due again, but the game reads the channel before the retell goes out:
    // it has heard it, so no "connected" again (fork #24).
    rtslot_retell(&s, 1400);
    rtslot_retell(&s, 1600);
    EXPECT_TRUE((s.retell & 1u) != 0);
    rtslot_heard(&s, 0);
    EXPECT_EQ(tell_all(s).joins, 0);
    EXPECT_EQ(s.retell & 1u, 0u);
}

// ws5 (CodeRabbit CLI): once quiet, quiet until the next Wii controller
// read, even when the time base comes round to just after the old read.
void TestQuietLatch() {
    rtslot s = fresh();
    rtslot_wii_read(&s, 1000);
    EXPECT_EQ(rtslot_quiet_due(&s, 1000 + 2999, 1), 0);
    EXPECT_EQ(rtslot_quiet_due(&s, 1000 + 3000, 1), 1);
    EXPECT_EQ(rtslot_quiet_due(&s, 1010, 1), 1);  // a wrapped clock
    rtslot_wii_read(&s, 9000);
    EXPECT_EQ(rtslot_quiet_due(&s, 9100, 1), 0);
}

// Mario Strikers Charged's WPAD copies an untouched, all-zero report with
// error 0 on a slot no Wii Remote ever had: not a real one, the stand-in
// keeps the slot. A blank report where a real one is known stays real.
void TestBlankReportIsNotReal() {
    rtslot s = fresh();
    plug(s, 0);
    EXPECT_EQ(rtslot_wpad_read(&s, 0, 0, 1), 0);
    EXPECT_EQ(rtslot_owner(&s, 0, kNone), RTSLOT_STANDIN);
    EXPECT_EQ(rtslot_wpad_read(&s, 0, 0, 0), 1);
    EXPECT_EQ(rtslot_owner(&s, 0, kNone), RTSLOT_REAL);
    EXPECT_EQ(rtslot_wpad_read(&s, 0, 0, 1), 1);
    EXPECT_EQ(rtslot_owner(&s, 0, kNone), RTSLOT_REAL);
}

// Punch-Out!! reads only WPAD: "no controller" there is how it learns a
// real Wii Remote left, and the stand-in comes back (and is told again).
void TestWpadReadNoController() {
    rtslot s = fresh();
    plug(s, 1);
    EXPECT_EQ(rtslot_wpad_read(&s, 1, 0, 0), 1);
    EXPECT_EQ(rtslot_owner(&s, 1, kNone), RTSLOT_REAL);
    EXPECT_EQ(rtslot_wpad_read(&s, 1, -1, 1), 0);
    EXPECT_EQ(rtslot_owner(&s, 1, kNone), RTSLOT_STANDIN);
    EXPECT_EQ(rtslot_wpad_read(&s, 7, 0, 0), 0);  // no such slot
}

// #610 (CodeRabbit): the recheck of a real Wii Remote runs the game's
// WPADProbe with interrupts on; a hooked probe answering meanwhile is
// newer, and the recheck's older "no controller" mustn't undo it.
void TestRecheckOlderAnswerLoses() {
    rtslot s = fresh();
    plug(s, 2);
    rtslot_probed(&s, 2, 0);
    std::uint32_t begun = rtslot_recheck_begin(&s, 2);
    rtslot_probed(&s, 2, 0);              // the game's own probe, meanwhile
    rtslot_rechecked(&s, 2, -1, begun);   // the recheck's older answer
    EXPECT_EQ(rtslot_owner(&s, 2, kNone), RTSLOT_REAL);
    // A KPAD read with samples meanwhile is newer too (CodeRabbit CLI, rall).
    begun = rtslot_recheck_begin(&s, 2);
    rtslot_kpad_read(&s, 2, 3, RTSLOT_NO_ERROR, 500);
    rtslot_rechecked(&s, 2, -1, begun);
    EXPECT_EQ(rtslot_owner(&s, 2, kNone), RTSLOT_REAL);
    begun = rtslot_recheck_begin(&s, 2);
    rtslot_rechecked(&s, 2, -1, begun);   // nothing newer: it counts
    EXPECT_EQ(rtslot_owner(&s, 2, kNone), RTSLOT_STANDIN);
}

// The HOME Menu's stand-in on a slot the permanent stand-in holds (or
// told the game of, its controller just unplugged): the game has a
// controller there already, so the HOME one isn't announced. In the Wii
// Remote forms, a HOME stand-in that took a free slot in its Classic shape
// keeps that claim though a GameCube controller plugs in (#610).
void TestHomeAnnounced() {
    rtslot s = fresh();
    EXPECT_EQ(rtslot_home_announced(&s, 1, 0), 1u);
    EXPECT_EQ(rtslot_home_announced(&s, kNone, 1), kNone);
    plug(s, 1);
    EXPECT_EQ(rtslot_home_announced(&s, 1, 0), kNone);
    unplug(s, 1);
    s.told = 1u << 1;
    EXPECT_EQ(rtslot_home_announced(&s, 1, 0), kNone);
    s.told = 0;
    // Wii Remote forms: took slot 2 free (Classic shape), then a pad plugs in.
    rtslot_home_took(&s, 2);
    plug(s, 2);
    EXPECT_EQ(rtslot_home_announced(&s, 2, 1), 2u);
    // Took a slot the Wii Remote stand-in already held: same shape, the game has it.
    rtslot_home_took(&s, 2);
    EXPECT_EQ(rtslot_home_announced(&s, 2, 1), kNone);
    // A real Wii Remote there: its library tells the game (fork #22).
    rtslot r = fresh();
    rtslot_probed(&r, 2, 0);
    EXPECT_EQ(rtslot_home_announced(&r, 2, 0), kNone);
    EXPECT_EQ(rtslot_home_announced(&r, 2, 1), kNone);
    EXPECT_EQ(rtslot_home_announced(&r, 3, 0), 3u);
}

// #610 (CodeRabbit): a real Wii Remote took the slot the HOME Menu's
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

// CodeRabbit CLI (restructure rs1): a retell the game's callback can't take
// yet keeps the first, delivered "connected": the slot stays told and the
// retell stays due, and a controller unplugged meanwhile is still told gone.
void TestFailedRetellKeepsTold() {
    rtslot s = fresh();
    s.retell_ticks = 100;
    plug(s, 0);
    tell_all(s);
    rtslot_retell(&s, 1000);
    rtslot_retell(&s, 1200);
    EXPECT_TRUE((s.retell & 1u) != 0);
    std::uint32_t slot = 9;
    std::int32_t reason = 9;
    EXPECT_TRUE(rtslot_next_tell(&s, kNone, 1, 0, &slot, &reason));
    EXPECT_EQ(reason, 0);
    EXPECT_EQ(rtslot_told(&s, slot, reason, 0), RTSLOT_AFTER_NOTHING);  // no callback just now
    EXPECT_TRUE((s.told & 1u) != 0);
    EXPECT_TRUE((s.retell & 1u) != 0);
    unplug(s, 0);
    EXPECT_EQ(tell_all(s).leaves, 1);
    // A first "connected" that couldn't be delivered stays unannounced.
    rtslot t = fresh();
    plug(t, 1);
    EXPECT_TRUE(rtslot_next_tell(&t, kNone, 1, 0, &slot, &reason));
    rtslot_told(&t, slot, reason, 0);
    EXPECT_EQ(t.told & 2u, 0u);
}

// CodeRabbit CLI (restructure rs1): a "connected" whose slot was
// unannounced while the callback ran (a real Wii Remote found, then gone)
// isn't followed by its attachment.
void TestNoExtensionAfterUnannounce() {
    rtslot s = fresh();
    plug(s, 2);
    std::uint32_t slot = 9;
    std::int32_t reason = 9;
    EXPECT_TRUE(rtslot_next_tell(&s, kNone, 1, 0, &slot, &reason));
    rtslot_probed(&s, 2, 0);   // in the callback: a real one
    rtslot_probed(&s, 2, -1);  // and gone again
    EXPECT_EQ(rtslot_told(&s, slot, reason, 1), RTSLOT_AFTER_NOTHING);
    EXPECT_EQ(tell_all(s).joins, 1);  // announced afresh, with its attachment
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

// A real Wii Remote that comes and goes while "connected" is being told:
// its library's "gone" reaches the game after the stand-in's "connected"
// (it came during that callback), so the game's last word is "gone" and the
// slot is not marked told. With the GameCube controller still there, the
// next pass announces it again; unplugged, there is nothing to tell, which
// matches what the game last heard (CodeRabbit on #609, answered).
void TestConnectedToldAfterRealCameAndWent() {
    rtslot s = fresh();
    plug(s, 2);
    std::uint32_t slot = 0;
    std::int32_t reason = -1;
    EXPECT_TRUE(rtslot_next_tell(&s, kNone, 1, 0, &slot, &reason));
    EXPECT_EQ(reason, 0);
    rtslot_probed(&s, 2, 0);   // a real Wii Remote during the callback
    rtslot_probed(&s, 2, -1);  // and gone again: the game heard "gone"
    EXPECT_EQ(rtslot_told(&s, slot, reason, 1), RTSLOT_AFTER_NOTHING);
    EXPECT_TRUE(!((s.told >> 2) & 1u));
    Told t = tell_all(s);
    EXPECT_EQ(t.joins, 1);  // announced again
    EXPECT_EQ(t.leaves, 0);
    rtslot u = fresh();
    plug(u, 1);
    EXPECT_TRUE(rtslot_next_tell(&u, kNone, 1, 0, &slot, &reason));
    rtslot_probed(&u, 1, 0);
    rtslot_probed(&u, 1, -1);
    rtslot_told(&u, slot, reason, 1);
    unplug(u, 1);
    t = tell_all(u);
    EXPECT_EQ(t.joins, 0);
    EXPECT_EQ(t.leaves, 0);  // the game's last word is already "gone"
}

int main() {
    TestEmptyGapExact();
    TestOwnerOrder();
    TestRealBeatsHome();
    TestHomeEndsOnRealSlot();
    TestHomeHoldsDifferentShape();
    TestHomeKeepsShapeAfterUnplug();
    TestUndeliveredStaysToTell();
    TestAfterTelling();
    TestProbeCountGuard();
    TestEmptyReads();
    TestHeardAndRetell();
    TestQuietLatch();
    TestBlankReportIsNotReal();
    TestWpadReadNoController();
    TestRecheckOlderAnswerLoses();
    TestHomeAnnounced();
    TestHomeLeaveNotForReal();
    TestTellingCount();
    TestFailedRetellKeepsTold();
    TestNoExtensionAfterUnannounce();
    TestCorrectedAfterLeave();
    TestConnectedToldAfterRealCameAndWent();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "rtslot tests passed" << std::endl;
    return 0;
}
