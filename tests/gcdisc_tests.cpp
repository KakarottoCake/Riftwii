// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A two-disc GameCube game's disc change: the blob's steps from a stop of
// the motor to the cover closing on the other disc (runtime/gc/gc_disc.h),
// and the plugin's check that an image is the game's second disc
// (riftwii/gcdiscs.hpp).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gc_disc.h"
#include "riftwii/gcdiscs.hpp"

namespace {

int failures = 0;

#define EXPECT_EQ(a, b)                                                                       \
    do {                                                                                      \
        const unsigned long long expect_a = static_cast<unsigned long long>(a);               \
        const unsigned long long expect_b = static_cast<unsigned long long>(b);               \
        if (expect_a != expect_b) {                                                           \
            std::printf("FAILED: %s == %s (%llu, %llu) at line %d\n", #a, #b, expect_a, expect_b, \
                        __LINE__);                                                            \
            ++failures;                                                                       \
        }                                                                                     \
    } while (0)
#define EXPECT_TRUE(x) EXPECT_EQ(static_cast<bool>(x), true)
#define EXPECT_FALSE(x) EXPECT_EQ(static_cast<bool>(x), false)

constexpr std::uint32_t kCover = 2000;  // ticks from the stop to the cover closing
constexpr std::uint32_t kLook = 100;    // between looks at a cover nobody waits on

gc_disc_change make_change() {
    gc_disc_change dc;
    std::memset(&dc, 0, sizeof(dc));
    dc.magic = GC_DISC_MAGIC;
    dc.discs[0] = {0x90100000u, 3, 0};
    dc.discs[1] = {0x90100100u, 2, 0};
    dc.cover_ticks = kCover;
    dc.look_ticks = kLook;
    return dc;
}

// The usual change: the game stops the motor and waits on the cover; the
// other disc goes in when it is due, and the interrupt is raised for it.
void test_change_and_back() {
    gc_disc_change dc = make_change();
    std::uint32_t ticks = 0;
    EXPECT_EQ(gc_disc_motor_stopped(&dc, 1000), 1u);  // the caller sets the alarm
    EXPECT_EQ(dc.state, GC_DISC_COVER_OPEN);
    EXPECT_EQ(dc.current, 0u);  // still disc 1 while the cover is open
    // The alarm on time, the game waiting (DICVR's mask set).
    const std::uint32_t todo = gc_disc_cover_alarm(&dc, 1000 + kCover, GC_DICVR_MASK, &ticks);
    EXPECT_EQ(todo, GC_DISC_SWAPPED | GC_DISC_RAISE);
    EXPECT_EQ(dc.current, 1u);
    EXPECT_EQ(dc.state, GC_DISC_IDLE);
    EXPECT_EQ(dc.changes, 1u);
    EXPECT_EQ(dc.raised, 1u);
    EXPECT_EQ(dc.alarm_queued, 0u);
    // Later the game asks for disc 1 again: the same steps take it back.
    EXPECT_EQ(gc_disc_motor_stopped(&dc, 50000), 1u);
    EXPECT_EQ(gc_disc_cover_alarm(&dc, 50000 + kCover, GC_DICVR_MASK, &ticks), GC_DISC_SWAPPED | GC_DISC_RAISE);
    EXPECT_EQ(dc.current, 0u);
    EXPECT_EQ(dc.changes, 2u);
    EXPECT_EQ(dc.stops, 2u);
}

// The motor stopped twice while the cover is open (the game asked again):
// one change, one alarm.
void test_stop_twice() {
    gc_disc_change dc = make_change();
    std::uint32_t ticks = 0;
    EXPECT_EQ(gc_disc_motor_stopped(&dc, 0), 1u);
    EXPECT_EQ(gc_disc_motor_stopped(&dc, 500), 0u);
    EXPECT_EQ(dc.due, kCover);  // the first stop's time still holds
    EXPECT_EQ(gc_disc_cover_alarm(&dc, kCover, GC_DICVR_MASK, &ticks), GC_DISC_SWAPPED | GC_DISC_RAISE);
    EXPECT_EQ(dc.current, 1u);
    EXPECT_EQ(dc.changes, 1u);
}

// The game is not waiting on the cover yet when it closes: the disc is in,
// the blob looks again until the game waits, then raises the interrupt.
void test_game_waits_late() {
    gc_disc_change dc = make_change();
    std::uint32_t ticks = 0;
    gc_disc_motor_stopped(&dc, 0);
    EXPECT_EQ(gc_disc_cover_alarm(&dc, kCover, 0, &ticks), GC_DISC_SWAPPED | GC_DISC_AGAIN);
    EXPECT_EQ(ticks, kLook);
    EXPECT_EQ(dc.state, GC_DISC_CLOSED);
    EXPECT_EQ(dc.alarm_queued, 1u);
    EXPECT_EQ(gc_disc_cover_alarm(&dc, kCover + kLook, 0, &ticks), GC_DISC_AGAIN);
    // Now it waits: the interrupt, and no second swap.
    EXPECT_EQ(gc_disc_cover_alarm(&dc, kCover + 2 * kLook, GC_DICVR_MASK, &ticks), GC_DISC_RAISE);
    EXPECT_EQ(dc.current, 1u);
    EXPECT_EQ(dc.changes, 1u);
    EXPECT_EQ(dc.state, GC_DISC_IDLE);
}

// Nobody ever waits: after GC_DISC_LOOKS looks the interrupt bit is just
// left set, as the drive would leave it, and the alarm stops.
void test_nobody_waits() {
    gc_disc_change dc = make_change();
    std::uint32_t ticks = 0;
    std::uint32_t now = kCover;
    std::uint32_t todo = 0;
    gc_disc_motor_stopped(&dc, 0);
    unsigned alarms = 0;
    do {
        todo = gc_disc_cover_alarm(&dc, now, 0, &ticks);
        now += ticks;
        ++alarms;
    } while ((todo & GC_DISC_AGAIN) != 0 && alarms < 100);
    EXPECT_EQ(todo, GC_DISC_LATCH);
    EXPECT_EQ(alarms, GC_DISC_LOOKS);
    EXPECT_EQ(dc.unseen, 1u);
    EXPECT_EQ(dc.state, GC_DISC_IDLE);
    EXPECT_EQ(dc.alarm_queued, 0u);
    EXPECT_EQ(dc.current, 1u);
}

// A game that missed the interrupt (latched) and later asks for its other
// disc again: a stop after the latch starts a fresh change, back to disc 1
// (the test switch's second stop, and a real game that asks again).
void test_stop_after_latch() {
    gc_disc_change dc = make_change();
    std::uint32_t ticks = 0;
    std::uint32_t now = kCover;
    gc_disc_motor_stopped(&dc, 0);
    while (gc_disc_cover_alarm(&dc, now, 0, &ticks) & GC_DISC_AGAIN) now += ticks;
    EXPECT_EQ(dc.unseen, 1u);
    EXPECT_EQ(dc.current, 1u);
    EXPECT_EQ(gc_disc_motor_stopped(&dc, now + 500), 1u);  // a new alarm
    EXPECT_EQ(dc.state, GC_DISC_COVER_OPEN);
    EXPECT_EQ(gc_disc_cover_alarm(&dc, now + 500 + kCover, GC_DICVR_MASK, &ticks), GC_DISC_SWAPPED | GC_DISC_RAISE);
    EXPECT_EQ(dc.current, 0u);
    EXPECT_EQ(dc.changes, 2u);
    EXPECT_EQ(dc.stops, 2u);
}

// A stop after the other disc went in but before the game looked: a new
// change, timed from this stop, on the alarm already queued (no second
// one), which comes early and waits out the rest.
void test_stop_while_closed() {
    gc_disc_change dc = make_change();
    std::uint32_t ticks = 0;
    gc_disc_motor_stopped(&dc, 0);
    EXPECT_EQ(gc_disc_cover_alarm(&dc, kCover, 0, &ticks), GC_DISC_SWAPPED | GC_DISC_AGAIN);
    EXPECT_EQ(gc_disc_motor_stopped(&dc, kCover + 10), 0u);  // the look alarm is queued
    EXPECT_EQ(dc.state, GC_DISC_COVER_OPEN);
    EXPECT_EQ(gc_disc_cover_alarm(&dc, kCover + kLook, GC_DICVR_MASK, &ticks), GC_DISC_AGAIN);
    EXPECT_EQ(ticks, kCover + 10 - kLook);
    EXPECT_EQ(dc.current, 1u);
    EXPECT_EQ(gc_disc_cover_alarm(&dc, 2 * kCover + 10, GC_DICVR_MASK, &ticks), GC_DISC_SWAPPED | GC_DISC_RAISE);
    EXPECT_EQ(dc.current, 0u);
    EXPECT_EQ(dc.changes, 2u);
}

// The timebase's low word wraps about every 70 s on a Wii: a change that
// spans the wrap is still timed right.
void test_timebase_wrap() {
    gc_disc_change dc = make_change();
    std::uint32_t ticks = 0;
    const std::uint32_t stop = 0xFFFFFF00u;
    gc_disc_motor_stopped(&dc, stop);
    EXPECT_EQ(gc_disc_cover_alarm(&dc, stop + 100, GC_DICVR_MASK, &ticks), GC_DISC_AGAIN);
    EXPECT_EQ(ticks, kCover - 100);
    EXPECT_EQ(gc_disc_cover_alarm(&dc, stop + kCover, GC_DICVR_MASK, &ticks), GC_DISC_SWAPPED | GC_DISC_RAISE);
}

// No second disc (a zeroed state, as for any other game): nothing moves.
void test_no_second_disc() {
    gc_disc_change dc;
    std::memset(&dc, 0, sizeof(dc));
    std::uint32_t ticks = 0;
    EXPECT_EQ(gc_disc_motor_stopped(&dc, 0), 0u);
    EXPECT_EQ(gc_disc_cover_alarm(&dc, kCover, GC_DICVR_MASK, &ticks), 0u);
    EXPECT_EQ(dc.current, 0u);
    EXPECT_EQ(dc.stops, 0u);
    // An alarm with nothing changing (a stray one) does nothing either.
    gc_disc_change idle = make_change();
    EXPECT_EQ(gc_disc_cover_alarm(&idle, 0, GC_DICVR_MASK, &ticks), 0u);
    EXPECT_EQ(idle.current, 0u);
}

// Reset restarts from disc 1, with nothing queued.
void test_restart() {
    gc_disc_change dc = make_change();
    std::uint32_t ticks = 0;
    gc_disc_motor_stopped(&dc, 0);
    gc_disc_cover_alarm(&dc, kCover, 0, &ticks);
    dc.alarm[0] = 0x12;
    gc_disc_restart(&dc);
    EXPECT_EQ(dc.current, 0u);
    EXPECT_EQ(dc.state, GC_DISC_IDLE);
    EXPECT_EQ(dc.alarm_queued, 0u);
    EXPECT_EQ(dc.alarm[0], 0u);
    EXPECT_EQ(dc.changes, 1u);  // the counters stay, for the trace
    EXPECT_EQ(gc_disc_motor_stopped(&dc, 10), 1u);
}

// A disc header (boot.bin's first 0x440 bytes): ID, disc number, magic and
// the FST's place and sizes.
std::vector<std::uint8_t> header(const char* id, std::uint8_t disc, std::uint32_t fst_offset, std::uint32_t fst_size,
                                 std::uint32_t fst_max) {
    std::vector<std::uint8_t> h(riftwii::kGcDiscHeaderBytes, 0);
    std::memcpy(h.data(), id, 6);
    h[6] = disc;
    auto put = [&h](std::size_t at, std::uint32_t v) {
        h[at] = static_cast<std::uint8_t>(v >> 24);
        h[at + 1] = static_cast<std::uint8_t>(v >> 16);
        h[at + 2] = static_cast<std::uint8_t>(v >> 8);
        h[at + 3] = static_cast<std::uint8_t>(v);
    };
    put(0x1C, 0xC2339F3Du);
    put(0x424, fst_offset);
    put(0x428, fst_size);
    put(0x42C, fst_max);
    return h;
}

void test_second_disc_matches() {
    using riftwii::gc_second_disc_matches;
    const std::uint64_t image = 1459978240ull;  // a full GameCube disc
    const auto disc1 = header("GBIE08", 0, 0x400000, 0x9000, 0xC000);
    const auto disc2 = header("GBIE08", 1, 0x500000, 0xB000, 0xC000);
    std::string why;
    EXPECT_TRUE(gc_second_disc_matches(disc1.data(), disc2.data(), image, why));
    // Another game's second disc, even with the same game code: refused.
    const auto other = header("GBIP08", 1, 0x500000, 0xB000, 0xC000);
    EXPECT_FALSE(gc_second_disc_matches(disc1.data(), other.data(), image, why));
    EXPECT_TRUE(why.find("GBIP08") != std::string::npos);
    // Two first discs, or a game started from its second disc.
    EXPECT_FALSE(gc_second_disc_matches(disc1.data(), disc1.data(), image, why));
    EXPECT_FALSE(gc_second_disc_matches(disc2.data(), disc2.data(), image, why));
    // Not a GameCube disc.
    auto no_magic = disc2;
    no_magic[0x1C] = 0;
    EXPECT_FALSE(gc_second_disc_matches(disc1.data(), no_magic.data(), image, why));
    // An FST of no size, too large, or past the image's end (cut short).
    EXPECT_FALSE(gc_second_disc_matches(disc1.data(), header("GBIE08", 1, 0x500000, 0, 0).data(), image, why));
    EXPECT_FALSE(gc_second_disc_matches(
        disc1.data(), header("GBIE08", 1, 0x500000, riftwii::kGcSecondDiscFstMax + 1, 0).data(), image, why));
    EXPECT_FALSE(gc_second_disc_matches(disc1.data(), disc2.data(), 0x500000 + 0xAFFF, why));
    EXPECT_TRUE(gc_second_disc_matches(disc1.data(), disc2.data(), 0x500000 + 0xB000, why));
}

void test_fst_room() {
    using riftwii::gc_two_disc_fst_room;
    // The larger of disc 1's room and disc 2's FST or its "FST max".
    EXPECT_EQ(gc_two_disc_fst_room(0xC000, header("GBIE08", 1, 0, 0xB000, 0xC000).data()), 0xC000u);
    EXPECT_EQ(gc_two_disc_fst_room(0x9000, header("GBIE08", 1, 0, 0xB000, 0).data()), 0xB000u);
    EXPECT_EQ(gc_two_disc_fst_room(0x9000, header("GBIE08", 1, 0, 0xB000, 0xD000).data()), 0xD000u);
    // A header's wild "FST max" is bounded.
    EXPECT_EQ(gc_two_disc_fst_room(0x9000, header("GBIE08", 1, 0, 0xB000, 0xFFFFFFFFu).data()),
              riftwii::kGcSecondDiscFstMax);
}

// An RVZ by its name, in any case, as the DOL's scan takes it.
void test_image_name_is_rvz() {
    using riftwii::gc_image_name_is_rvz;
    EXPECT_TRUE(gc_image_name_is_rvz("sd:/games/GBIE08/disc2.rvz"));
    EXPECT_TRUE(gc_image_name_is_rvz("usb:/games/GBIE08/DISC2.RVZ"));
    EXPECT_TRUE(gc_image_name_is_rvz("disc2.Rvz"));
    EXPECT_FALSE(gc_image_name_is_rvz("sd:/games/GBIE08/disc2.iso"));
    EXPECT_FALSE(gc_image_name_is_rvz("disc2.rvz.iso"));
    EXPECT_FALSE(gc_image_name_is_rvz("rvz"));
    EXPECT_FALSE(gc_image_name_is_rvz(""));
}

}  // namespace

int main() {
    test_change_and_back();
    test_stop_twice();
    test_game_waits_late();
    test_nobody_waits();
    test_stop_after_latch();
    test_stop_while_closed();
    test_timebase_wrap();
    test_no_second_disc();
    test_restart();
    test_second_disc_matches();
    test_fst_room();
    test_image_name_is_rvz();
    if (failures == 0) std::printf("gcdisc: all passed\n");
    return failures == 0 ? 0 : 1;
}
