// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// GameCube controller calibration (riftwii/padcal.hpp, runtime/rtpadcal.h):
// the stretch a game applies, worked out by the menu and applied with the
// in-game code itself, and the file the page keeps it in.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "riftwii/padcal.hpp"
#include "rtgcad.h"
#include "rtpadcal.h"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

using riftwii::PadCalibration;
using riftwii::PadCalibrations;
using riftwii::PadSlot;

// A PADStatus's six analog bytes after the in-game code has run on them.
struct Axes {
    int stick_x, stick_y, c_x, c_y, l, r;
};

static Axes Apply(const rtpadcal_pad& pad, int stick_x, int stick_y, int c_x, int c_y, int l, int r) {
    std::uint8_t bytes[6] = {static_cast<std::uint8_t>(stick_x), static_cast<std::uint8_t>(stick_y),
                             static_cast<std::uint8_t>(c_x),     static_cast<std::uint8_t>(c_y),
                             static_cast<std::uint8_t>(l),       static_cast<std::uint8_t>(r)};
    rtpadcal_apply(&pad, bytes);
    return Axes{static_cast<std::int8_t>(bytes[0]), static_cast<std::int8_t>(bytes[1]),
                static_cast<std::int8_t>(bytes[2]), static_cast<std::int8_t>(bytes[3]), bytes[4], bytes[5]};
}

static PadCalibration Healthy() {
    PadCalibration c;
    c.main = {110, 108, 112, 106};
    c.c_stick = {100, 98, 101, 99};
    c.trigger_l = 200;
    c.trigger_r = 190;
    return c;
}

// A stick that reaches a healthy one's gate, with no dead zone, gives the
// game exactly what it read, everywhere, both signs and the extremes.
static void TestHealthyUnchanged() {
    const rtpadcal_pad pad = riftwii::pad_calibration_stretch(Healthy());
    EXPECT_EQ(pad.used, 1u);
    for (int value = -128; value <= 127; ++value) {
        const Axes a = Apply(pad, value, value, value, value, 0, 0);
        EXPECT_EQ(a.stick_x, value);
        EXPECT_EQ(a.stick_y, value);
        EXPECT_EQ(a.c_x, value);
        EXPECT_EQ(a.c_y, value);
    }
    for (int value = 0; value <= 255; ++value) {
        const Axes a = Apply(pad, 0, 0, 0, 0, value, value);
        EXPECT_EQ(a.l, value);
        EXPECT_EQ(a.r, value);
    }
    // A default calibration is no calibration at all.
    const rtpadcal_pad none = riftwii::pad_calibration_stretch(PadCalibration{});
    EXPECT_EQ(none.used, 0u);
    const Axes a = Apply(none, -77, 55, 3, -3, 12, 250);
    EXPECT_EQ(a.stick_x, -77);
    EXPECT_EQ(a.r, 250);
    // Nor does a null one (a controller the table has none for).
    std::uint8_t bytes[6] = {1, 2, 3, 4, 5, 6};
    rtpadcal_apply(nullptr, bytes);
    EXPECT_EQ(bytes[5], 6u);
}

// A worn stick that stops short reaches a healthy one's full tilt in each
// direction, half way is about half way, and the stick keeps its sign.
static void TestWornReachesFullRange() {
    PadCalibration worn;
    worn.main = {70, 80, 64, 90};
    worn.c_stick = {60, 60, 72, 50};
    const rtpadcal_pad pad = riftwii::pad_calibration_stretch(worn);
    Axes a = Apply(pad, 70, 64, 60, 72, 0, 0);
    EXPECT_EQ(a.stick_x, riftwii::kPadMainStickReach);
    EXPECT_EQ(a.stick_y, riftwii::kPadMainStickReach);
    EXPECT_EQ(a.c_x, riftwii::kPadCStickReach);
    EXPECT_EQ(a.c_y, riftwii::kPadCStickReach);
    a = Apply(pad, -80, -90, -60, -50, 0, 0);
    EXPECT_EQ(a.stick_x, -riftwii::kPadMainStickReach);
    EXPECT_EQ(a.stick_y, -riftwii::kPadMainStickReach);
    EXPECT_EQ(a.c_x, -riftwii::kPadCStickReach);
    EXPECT_EQ(a.c_y, -riftwii::kPadCStickReach);
    a = Apply(pad, 35, -45, 0, 0, 0, 0);
    EXPECT_EQ(a.stick_x, 52);  // 35 of 70 is half of 104
    EXPECT_EQ(a.stick_y, -52);
    // The centre stays the centre.
    a = Apply(pad, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(a.stick_x, 0);
    EXPECT_EQ(a.c_y, 0);
    // Only a direction that falls short is stretched: a stick worn on one
    // side alone keeps the other as it was.
    PadCalibration one_side = Healthy();
    one_side.main[0] = 70;
    const rtpadcal_pad side = riftwii::pad_calibration_stretch(one_side);
    EXPECT_EQ(Apply(side, 70, 0, 0, 0, 0, 0).stick_x, riftwii::kPadMainStickReach);
    EXPECT_EQ(Apply(side, -100, 0, 0, 0, 0, 0).stick_x, -100);
}

// Nothing is pushed past what a real pad gives: a value past the measured
// reach (a harder push than during the calibration) is held at the
// healthy reach, and the signed byte never wraps.
static void TestClampedToRealRange() {
    PadCalibration worn;
    worn.main = {40, 40, 40, 40};
    worn.c_stick = {40, 40, 40, 40};
    worn.trigger_l = 60;
    const rtpadcal_pad pad = riftwii::pad_calibration_stretch(worn);
    for (int value = -128; value <= 127; ++value) {
        const Axes a = Apply(pad, value, value, value, value, 0, 0);
        EXPECT_TRUE(a.stick_x <= riftwii::kPadMainStickReach && a.stick_x >= -riftwii::kPadMainStickReach);
        EXPECT_TRUE(a.c_x <= riftwii::kPadCStickReach && a.c_x >= -riftwii::kPadCStickReach);
        // The same sign as the stick, or the centre.
        EXPECT_TRUE((value > 0 && a.stick_x > 0) || (value < 0 && a.stick_x < 0) || (value == 0 && a.stick_x == 0));
    }
    EXPECT_EQ(Apply(pad, 127, -128, 0, 0, 0, 0).stick_x, riftwii::kPadMainStickReach);
    EXPECT_EQ(Apply(pad, 127, -128, 0, 0, 0, 0).stick_y, -riftwii::kPadMainStickReach);
    for (int value = 0; value <= 255; ++value)
        EXPECT_TRUE(Apply(pad, 0, 0, 0, 0, value, 0).l <= riftwii::kPadTriggerReach);
    // Unstretched directions keep a signed byte's own ends.
    PadCalibration dead_only;
    dead_only.dead_zone = 10;
    const rtpadcal_pad dz = riftwii::pad_calibration_stretch(dead_only);
    EXPECT_EQ(Apply(dz, 127, -128, 0, 0, 0, 0).stick_x, 127);
    EXPECT_EQ(Apply(dz, 127, -128, 0, 0, 0, 0).stick_y, -128);
}

// The dead zone: a drifting stick near the centre reads as the centre,
// and what lies past it still reaches full tilt, so nothing jumps.
static void TestDeadZone() {
    PadCalibration c = Healthy();
    c.dead_zone = 12;
    const rtpadcal_pad pad = riftwii::pad_calibration_stretch(c);
    EXPECT_EQ(pad.dead[0], 12u);
    EXPECT_EQ(pad.dead[1], 12u);
    for (int value = -12; value <= 12; ++value) {
        const Axes a = Apply(pad, value, value, value, value, 0, 0);
        EXPECT_EQ(a.stick_x, 0);
        EXPECT_EQ(a.c_y, 0);
    }
    EXPECT_EQ(Apply(pad, 13, 0, 0, 0, 0, 0).stick_x, 1);  // just past it: barely moved
    EXPECT_EQ(Apply(pad, 110, 0, 0, 0, 0, 0).stick_x, 110);  // full reach stays full reach
    EXPECT_EQ(Apply(pad, -108, 0, 0, 0, 0, 0).stick_x, -108);
    // A worn stick with a dead zone: past it, still up to the healthy reach.
    PadCalibration worn;
    worn.main = {70, 70, 70, 70};
    worn.dead_zone = 10;
    const rtpadcal_pad wp = riftwii::pad_calibration_stretch(worn);
    EXPECT_EQ(Apply(wp, 10, -10, 0, 0, 0, 0).stick_x, 0);
    EXPECT_EQ(Apply(wp, 70, -70, 0, 0, 0, 0).stick_x, riftwii::kPadMainStickReach);
    EXPECT_EQ(Apply(wp, 70, -70, 0, 0, 0, 0).stick_y, -riftwii::kPadMainStickReach);
    // Values only grow with the stick, never step back.
    int last = 0;
    for (int value = 0; value <= 127; ++value) {
        const int now = Apply(wp, value, 0, 0, 0, 0, 0).stick_x;
        EXPECT_TRUE(now >= last);
        last = now;
    }
    // A dead zone alone is a calibration: the game gets it.
    PadCalibration dead_only;
    dead_only.dead_zone = riftwii::kPadDeadZoneStep;
    EXPECT_TRUE(!dead_only.is_default());
    EXPECT_EQ(riftwii::pad_calibration_stretch(dead_only).used, 1u);
}

// Triggers: one that stops short is stretched to a healthy one's travel,
// a healthy one is left alone, and none goes past it once stretched.
static void TestTriggers() {
    PadCalibration c;
    c.trigger_l = 120;
    c.trigger_r = 200;
    const rtpadcal_pad pad = riftwii::pad_calibration_stretch(c);
    EXPECT_EQ(Apply(pad, 0, 0, 0, 0, 120, 200).l, riftwii::kPadTriggerReach);
    EXPECT_EQ(Apply(pad, 0, 0, 0, 0, 120, 200).r, 200);  // healthy: as it is
    EXPECT_EQ(Apply(pad, 0, 0, 0, 0, 60, 0).l, 90);       // half way is half way
    EXPECT_EQ(Apply(pad, 0, 0, 0, 0, 0, 0).l, 0);
    EXPECT_EQ(Apply(pad, 0, 0, 0, 0, 255, 255).l, riftwii::kPadTriggerReach);
    EXPECT_EQ(Apply(pad, 0, 0, 0, 0, 255, 255).r, 255);
    // The sticks of a triggers-only calibration are untouched.
    EXPECT_EQ(Apply(pad, -90, 33, 127, -128, 0, 0).stick_x, -90);
    EXPECT_EQ(Apply(pad, -90, 33, 127, -128, 0, 0).c_y, -128);
}

// What the page notes while the sticks go round and the triggers go down.
static void TestMeasuring() {
    PadCalibration m;
    const std::int8_t round[][4] = {{70, 0, 0, 50}, {50, 50, 0, 0}, {0, 64, -60, 0}, {-80, 0, 0, -55}, {0, -90, 61, 0}};
    for (const auto& s : round) riftwii::pad_calibration_note(m, s, 0, 0);
    riftwii::pad_calibration_note(m, round[0], 130, 9);
    riftwii::pad_calibration_note(m, round[0], 40, 210);
    EXPECT_EQ(m.main[0], 70u);
    EXPECT_EQ(m.main[1], 80u);
    EXPECT_EQ(m.main[2], 64u);
    EXPECT_EQ(m.main[3], 90u);
    EXPECT_EQ(m.c_stick[0], 61u);
    EXPECT_EQ(m.c_stick[1], 60u);
    EXPECT_EQ(m.c_stick[2], 50u);
    EXPECT_EQ(m.c_stick[3], 55u);
    EXPECT_EQ(m.trigger_l, 130u);
    EXPECT_EQ(m.trigger_r, 210u);
    const std::int8_t extreme[4] = {-128, -128, -128, -128};
    riftwii::pad_calibration_note(m, extreme, 0, 0);
    EXPECT_EQ(m.main[1], 128u);
    EXPECT_TRUE(riftwii::pad_stick_measured(m.main));
    EXPECT_TRUE(!riftwii::pad_stick_measured({70, 80, 30, 90}));  // never pushed up
}

static void TestSlots() {
    PadSlot slot;
    EXPECT_TRUE(riftwii::parse_pad_slot("wii1", slot));
    EXPECT_TRUE(slot.kind == PadSlot::Kind::Wii && slot.id == 0);
    EXPECT_TRUE(riftwii::parse_pad_slot("adapter4", slot));
    EXPECT_TRUE(slot.kind == PadSlot::Kind::Adapter && slot.id == 3);
    EXPECT_TRUE(riftwii::parse_pad_slot("usb-057e2009", slot));
    EXPECT_TRUE(slot.kind == PadSlot::Kind::Usb && slot.id == 0x057E2009u);
    EXPECT_EQ(riftwii::pad_slot_key(slot), std::string("usb-057e2009"));
    EXPECT_EQ(riftwii::pad_slot_key(PadSlot{PadSlot::Kind::Wii, 2}), std::string("wii3"));
    EXPECT_EQ(riftwii::pad_slot_key(PadSlot{PadSlot::Kind::Adapter, 0}), std::string("adapter1"));
    for (const char* bad : {"wii0", "wii5", "adapter", "adapter12", "usb-", "usb-00000000", "usb-057E2009",
                            "usb-057e20090", "usb-xyz", "gamecube1", ""})
        EXPECT_TRUE(!riftwii::parse_pad_slot(bad, slot));
}

static void TestFile() {
    PadCalibrations all;
    PadCalibration worn;
    worn.main = {70, 80, 64, 90};
    worn.c_stick = {60, 61, 62, 63};
    worn.trigger_l = 120;
    worn.dead_zone = 8;
    EXPECT_TRUE(all.set(PadSlot{PadSlot::Kind::Wii, 0}, worn));
    EXPECT_TRUE(all.set(PadSlot{PadSlot::Kind::Adapter, 2}, Healthy()));
    EXPECT_TRUE(all.set(PadSlot{PadSlot::Kind::Usb, 0x057E2009u}, worn));
    const std::string text = all.serialize();
    EXPECT_TRUE(text.find("wii1 = 70 80 64 90 60 61 62 63 120 0 8\n") != std::string::npos);

    PadCalibrations back;
    back.parse(text);
    EXPECT_EQ(back.entries().size(), 3u);
    const PadCalibration* read = back.find(PadSlot{PadSlot::Kind::Wii, 0});
    EXPECT_TRUE(read != nullptr && read->main == worn.main && read->c_stick == worn.c_stick &&
                read->trigger_l == 120 && read->dead_zone == 8);
    EXPECT_EQ(back.serialize(), text);

    // Reset to default forgets the controller.
    EXPECT_TRUE(back.set(PadSlot{PadSlot::Kind::Wii, 0}, PadCalibration{}));
    EXPECT_TRUE(back.find(PadSlot{PadSlot::Kind::Wii, 0}) == nullptr);
    back.forget(PadSlot{PadSlot::Kind::Adapter, 2});
    EXPECT_EQ(back.entries().size(), 1u);

    // Lenient: comments, CRLF and spaces are fine; a line with a value out
    // of range, too few values, a bad slot or a dead zone past the slider
    // is dropped; the last line for a slot wins.
    PadCalibrations lenient;
    lenient.parse("# mine\r\n"
                  "wii2 = 1 2 3 4 5 6 7 8 9 10 11\r\n"
                  "  wii2=70 70 70 70 0 0 0 0 0 0 4   # again\n"
                  "wii3 = 300 70 70 70 0 0 0 0 0 0 0\n"
                  "wii4 = 70 70 70\n"
                  "wii9 = 70 70 70 70 0 0 0 0 0 0 0\n"
                  "adapter1 = 70 70 70 70 0 0 0 0 0 0 31\n"
                  "adapter2 = -5 70 70 70 0 0 0 0 0 0 0\n"
                  "adapter3 = 0 0 0 0 0 0 0 0 0 0 0\n"
                  "usb-054c0268 = 70 70 70 70 0 0 0 0 0 0 0 extra\n");
    EXPECT_EQ(lenient.entries().size(), 1u);
    const PadCalibration* second = lenient.find(PadSlot{PadSlot::Kind::Wii, 1});
    EXPECT_TRUE(second != nullptr && second->main[0] == 70 && second->dead_zone == 4);

    // At most RTPADCAL_USB_PADS USB pads; ports are never refused.
    PadCalibrations many;
    for (std::uint32_t i = 0; i < RTPADCAL_USB_PADS; ++i)
        EXPECT_TRUE(many.set(PadSlot{PadSlot::Kind::Usb, 0x12340000u + i}, worn));
    EXPECT_TRUE(!many.set(PadSlot{PadSlot::Kind::Usb, 0x56780000u}, worn));
    EXPECT_TRUE(many.set(PadSlot{PadSlot::Kind::Usb, 0x12340000u}, Healthy()));  // replacing is fine
    EXPECT_TRUE(many.set(PadSlot{PadSlot::Kind::Wii, 3}, worn));
}

// The launch's table: each controller where the game looks for it.
static void TestTable() {
    rtpadcal_table table;
    PadCalibrations none;
    EXPECT_TRUE(!riftwii::pad_calibration_table(none, table));
    EXPECT_EQ(table.console[0].used, 0u);

    PadCalibrations all;
    PadCalibration worn;
    worn.main = {70, 70, 70, 70};
    all.set(PadSlot{PadSlot::Kind::Wii, 1}, worn);
    all.set(PadSlot{PadSlot::Kind::Adapter, 3}, worn);
    all.set(PadSlot{PadSlot::Kind::Usb, 0x057E2009u}, worn);
    EXPECT_TRUE(riftwii::pad_calibration_table(all, table));
    EXPECT_EQ(table.console[0].used, 0u);
    EXPECT_EQ(table.console[1].used, 1u);
    EXPECT_EQ(table.adapter[3].used, 1u);
    EXPECT_TRUE(rtpadcal_usb(&table, 0x057E2009u) == &table.usb[0]);
    EXPECT_TRUE(rtpadcal_usb(&table, 0x054C0268u) == nullptr);
    EXPECT_TRUE(rtpadcal_usb(&table, 0) == nullptr);  // an empty row is no pad's
}

// The in-game code works on a PADStatus's bytes 2 to 7 and on gcad_pad's
// fields from stick_x: the same six bytes in the same order.
static void TestLayouts() {
    EXPECT_EQ(offsetof(gcad_pad, stick_y), offsetof(gcad_pad, stick_x) + 1);
    EXPECT_EQ(offsetof(gcad_pad, substick_x), offsetof(gcad_pad, stick_x) + 2);
    EXPECT_EQ(offsetof(gcad_pad, substick_y), offsetof(gcad_pad, stick_x) + 3);
    EXPECT_EQ(offsetof(gcad_pad, trigger_l), offsetof(gcad_pad, stick_x) + 4);
    EXPECT_EQ(offsetof(gcad_pad, trigger_r), offsetof(gcad_pad, stick_x) + 5);
    // Fixed-width fields only: the menu's and the game's layouts agree.
    EXPECT_EQ(sizeof(rtpadcal_pad), 36u);
    EXPECT_EQ(sizeof(rtpadcal_table), 8u * 36u + 16u + 4u * 36u);
}

int main() {
    TestHealthyUnchanged();
    TestWornReachesFullRange();
    TestClampedToRealRange();
    TestDeadZone();
    TestTriggers();
    TestMeasuring();
    TestSlots();
    TestFile();
    TestTable();
    TestLayouts();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "padcal: all tests passed" << std::endl;
    return 0;
}
