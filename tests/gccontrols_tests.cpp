// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A GameCube game's Wii Remote controls: the built-in map
// (src/data/gccontrols.cpp), the files that change it, and the blob's
// side of it (runtime/gc/gc_controls.h): the map applied, and the shake;
// and a Wii U Pro Controller's reports (runtime/gc/gc_pro.h).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "gc_pro.h"
#include "riftwii/gccontrols.hpp"

namespace {

int failures = 0;

#define EXPECT_EQ(a, b)                                                                   \
    do {                                                                                  \
        const unsigned long long expect_a = static_cast<unsigned long long>(a);           \
        const unsigned long long expect_b = static_cast<unsigned long long>(b);           \
        if (expect_a != expect_b) {                                                       \
            std::printf("FAILED: %s == %s (0x%llx, 0x%llx) at line %d\n", #a, #b, expect_a, \
                        expect_b, __LINE__);                                              \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

#define EXPECT_TRUE(c)                                                     \
    do {                                                                   \
        if (!(c)) {                                                        \
            std::printf("FAILED: %s at line %d\n", #c, __LINE__);          \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

using riftwii::gc_controls_apply;
using riftwii::gc_controls_default;

constexpr std::uint32_t held(std::uint32_t input) { return 1u << input; }

// What PADStatus gets from `held_inputs`, the way gc_bt.c's fill uses the map.
struct Pad {
    std::uint32_t buttons;
    std::int32_t sx, sy, cx, cy;
};
Pad press(const gc_map& map, std::uint32_t kind, std::uint32_t held_inputs) {
    Pad pad = {0, 0, 0, 0, 0};
    const std::uint32_t targets = gc_map_targets(&map, kind, held_inputs);
    pad.buttons = targets & GC_PAD_BUTTONS;
    gc_map_sticks(targets, &pad.sx, &pad.sy, &pad.cx, &pad.cy);
    return pad;
}

bool has_note_with(const std::vector<std::string>& notes, const std::string& part) {
    for (const std::string& n : notes)
        if (n.find(part) != std::string::npos) return true;
    return false;
}

// The built-in map is the one RiftWii has always had.
void test_default_remote_sideways() {
    gc_map map;
    gc_controls_default(map);
    EXPECT_EQ(press(map, GC_MAP_REMOTE, held(GC_IN_2)).buttons, GC_PAD_A);
    EXPECT_EQ(press(map, GC_MAP_REMOTE, held(GC_IN_1)).buttons, GC_PAD_B);
    EXPECT_EQ(press(map, GC_MAP_REMOTE, held(GC_IN_B)).buttons, GC_PAD_R);
    EXPECT_EQ(press(map, GC_MAP_REMOTE, held(GC_IN_A)).buttons, GC_PAD_X);
    EXPECT_EQ(press(map, GC_MAP_REMOTE, held(GC_IN_HOME)).buttons, GC_PAD_Y);
    EXPECT_EQ(press(map, GC_MAP_REMOTE, held(GC_IN_MINUS)).buttons, GC_PAD_Z);
    EXPECT_EQ(press(map, GC_MAP_REMOTE, held(GC_IN_PLUS)).buttons, GC_PAD_START);
    const Pad up = press(map, GC_MAP_REMOTE, held(GC_IN_UP));
    EXPECT_EQ(up.buttons, GC_PAD_UP);
    EXPECT_EQ(up.sy, GC_STICK_FULL);
    EXPECT_EQ(up.sx, 0);
    const Pad left = press(map, GC_MAP_REMOTE, held(GC_IN_LEFT));
    EXPECT_EQ(left.buttons, GC_PAD_LEFT);
    EXPECT_EQ(left.sx, -GC_STICK_FULL);
    // Both ways of an axis: right and down win, as they did.
    const Pad both = press(map, GC_MAP_REMOTE, held(GC_IN_LEFT) | held(GC_IN_RIGHT) | held(GC_IN_UP) | held(GC_IN_DOWN));
    EXPECT_EQ(both.sx, GC_STICK_FULL);
    EXPECT_EQ(both.sy, -GC_STICK_FULL);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_SHAKE], 0);
    EXPECT_TRUE(!gc_map_wants_shake(&map, GC_MAP_REMOTE));
}

void test_default_nunchuk_and_classic() {
    gc_map map;
    gc_controls_default(map);
    EXPECT_EQ(press(map, GC_MAP_NUNCHUK, held(GC_IN_A) | held(GC_IN_B)).buttons, GC_PAD_A | GC_PAD_B);
    EXPECT_EQ(press(map, GC_MAP_NUNCHUK, held(GC_IN_1)).buttons, GC_PAD_X);
    EXPECT_EQ(press(map, GC_MAP_NUNCHUK, held(GC_IN_2)).buttons, GC_PAD_Y);
    EXPECT_EQ(press(map, GC_MAP_NUNCHUK, held(GC_IN_Z)).buttons, GC_PAD_R);
    EXPECT_EQ(press(map, GC_MAP_NUNCHUK, held(GC_IN_C)).buttons, GC_PAD_L);
    EXPECT_EQ(press(map, GC_MAP_NUNCHUK, held(GC_IN_HOME)).buttons, 0);
    const Pad camera = press(map, GC_MAP_NUNCHUK, held(GC_IN_UP) | held(GC_IN_RIGHT));
    EXPECT_EQ(camera.buttons, 0);
    EXPECT_EQ(camera.cy, GC_STICK_FULL);
    EXPECT_EQ(camera.cx, GC_STICK_FULL);
    EXPECT_EQ(camera.sx, 0);

    EXPECT_EQ(press(map, GC_MAP_CLASSIC, held(GC_IN_ZL)).buttons, GC_PAD_Z);
    EXPECT_EQ(press(map, GC_MAP_CLASSIC, held(GC_IN_ZR)).buttons, GC_PAD_Z);
    EXPECT_EQ(press(map, GC_MAP_CLASSIC, held(GC_IN_X) | held(GC_IN_Y)).buttons, GC_PAD_X | GC_PAD_Y);
    EXPECT_EQ(press(map, GC_MAP_CLASSIC, held(GC_IN_DOWN)).buttons, GC_PAD_DOWN);
    EXPECT_EQ(press(map, GC_MAP_CLASSIC, held(GC_IN_DOWN)).sy, 0);  // the D-pad only: the sticks are its own
    EXPECT_EQ(press(map, GC_MAP_CLASSIC, held(GC_IN_MINUS) | held(GC_IN_HOME)).buttons, 0);
    EXPECT_EQ(gc_map_targets(&map, GC_MAP_KINDS, held(GC_IN_A)), 0);  // no such kind
}

// A stick the map does not push keeps its own position.
void test_sticks_kept() {
    std::int32_t sx = 40, sy = -20, cx = 7, cy = 9;
    gc_map_sticks(GC_OUT_STICK_UP, &sx, &sy, &cx, &cy);
    EXPECT_EQ(sx, 40);
    EXPECT_EQ(sy, GC_STICK_FULL);
    EXPECT_EQ(cx, 7);
    EXPECT_EQ(cy, 9);
}

void test_file_changes_only_its_lines() {
    gc_map map;
    gc_controls_default(map);
    std::vector<std::string> notes;
    const std::string text =
        "# My controls\n"
        "[Remote]\n"
        "2 = Start   # the game's menu\n"
        "Shake = B\n"
        "\n"
        "[nunchuk]\r\n"
        "c = y, cstickup\r\n"
        "HOME = none\r\n"
        "PLUS =\r\n";
    EXPECT_EQ(gc_controls_apply(text, "default.txt", map, notes), 5);
    EXPECT_EQ(notes.size(), 0);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_2], GC_PAD_START);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_SHAKE], GC_PAD_B);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_1], GC_PAD_B);  // untouched
    EXPECT_EQ(map.to[GC_MAP_NUNCHUK][GC_IN_C], GC_PAD_Y | GC_OUT_CSTICK_UP);
    EXPECT_EQ(map.to[GC_MAP_NUNCHUK][GC_IN_HOME], 0);
    EXPECT_EQ(map.to[GC_MAP_NUNCHUK][GC_IN_PLUS], 0);  // nothing after '=' clears it
    EXPECT_EQ(map.to[GC_MAP_NUNCHUK][GC_IN_Z], GC_PAD_R);
    EXPECT_TRUE(gc_map_wants_shake(&map, GC_MAP_REMOTE));
    EXPECT_TRUE(!gc_map_wants_shake(&map, GC_MAP_NUNCHUK));
    EXPECT_EQ(press(map, GC_MAP_REMOTE, held(GC_IN_SHAKE)).buttons, GC_PAD_B);

    // A game's file after it: its lines win, the rest stays.
    notes.clear();
    EXPECT_EQ(gc_controls_apply("[remote]\n2 = X\n", "GLMP01.txt", map, notes), 1);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_2], GC_PAD_X);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_SHAKE], GC_PAD_B);
}

// The sample in docs/GUIDE.md (GMSE01.txt): it parses without a note, and ZR
// presses Y while ZL stays Z.
void test_guide_sample() {
    gc_map map;
    gc_controls_default(map);
    std::vector<std::string> notes;
    const std::string text =
        "# sd:/riftwii/gc/controls/GMSE01.txt\n"
        "[classic]\n"
        "ZR = Y            # ZR opens the map (the GameCube's Y button)\n"
        "ZL = Z            # ZL stays a plain Z press, as it is without a file\n";
    EXPECT_EQ(gc_controls_apply(text, "GMSE01.txt", map, notes), 2);
    EXPECT_EQ(notes.size(), 0);
    EXPECT_EQ(press(map, GC_MAP_CLASSIC, held(GC_IN_ZR)).buttons, GC_PAD_Y);
    EXPECT_EQ(press(map, GC_MAP_CLASSIC, held(GC_IN_ZL)).buttons, GC_PAD_Z);
}

void test_unknown_names_left_out() {
    gc_map map;
    gc_controls_default(map);
    std::vector<std::string> notes;
    const std::string text =
        "2 = A\n"                     // 1: before any heading
        "[remote]\n"
        "Q = A\n"                     // 3: no such button
        "C = A\n"                     // 4: the remote alone has no C
        "1 = Jump\n"                  // 5: no such target: the line changes nothing
        "A = Y Turbo\n"               // 6: one known, one not: Y kept
        "B A\n"                       // 7: no '='
        "[classic]\n"
        "shake = A\n"                 // 9: no shake on a Classic Controller
        "[guitar]\n"                  // 10: no such heading
        "A = B\n"                     // 11: under it: skipped, no note of its own
        "[classic]\n"
        "ZL = R\n";                   // 13: fine again
    EXPECT_EQ(gc_controls_apply(text, "GLMP01.txt", map, notes), 2);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_1], GC_PAD_B);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_A], GC_PAD_Y);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_C], 0);
    EXPECT_EQ(map.to[GC_MAP_CLASSIC][GC_IN_A], GC_PAD_A);
    EXPECT_EQ(map.to[GC_MAP_CLASSIC][GC_IN_ZL], GC_PAD_R);
    EXPECT_EQ(map.to[GC_MAP_CLASSIC][GC_IN_SHAKE], 0);
    EXPECT_EQ(notes.size(), 8);
    EXPECT_TRUE(has_note_with(notes, "GLMP01.txt line 1: the line comes before any heading"));
    EXPECT_TRUE(has_note_with(notes, "line 3: \"Q\" is not a button RiftWii knows"));
    EXPECT_TRUE(has_note_with(notes, "line 4: A Wii Remote alone has no \"C\""));
    EXPECT_TRUE(has_note_with(notes, "line 5: \"Jump\" is not a GameCube button"));
    EXPECT_TRUE(has_note_with(notes, "line 6: \"Turbo\""));
    EXPECT_TRUE(has_note_with(notes, "line 7: the line has no '='"));
    EXPECT_TRUE(has_note_with(notes, "line 9: A Classic Controller has no \"shake\""));
    EXPECT_TRUE(has_note_with(notes, "line 10: [guitar] is not a heading"));
}

void test_bounds() {
    gc_map map;
    gc_controls_default(map);
    std::vector<std::string> notes;

    // Too large: none of it.
    std::string huge = "[remote]\n2 = Start\n";
    huge.append(riftwii::kMaxGcControlsFile, '#');
    EXPECT_EQ(gc_controls_apply(huge, "default.txt", map, notes), 0);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_2], GC_PAD_A);
    EXPECT_EQ(notes.size(), 1);

    // A line too long is skipped; the next one is read.
    notes.clear();
    const std::string long_line = "[remote]\n2 = " + std::string(riftwii::kMaxGcControlsLine, 'A') + "\n1 = Start\n";
    EXPECT_EQ(gc_controls_apply(long_line, "default.txt", map, notes), 1);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_1], GC_PAD_START);
    EXPECT_TRUE(has_note_with(notes, "line 2: the line is longer than"));

    // More names after '=' than kept.
    notes.clear();
    EXPECT_EQ(gc_controls_apply("[remote]\nA = A B X Y Z L R Start Up Down\n", "default.txt", map, notes), 1);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_A], GC_PAD_A | GC_PAD_B | GC_PAD_X | GC_PAD_Y | GC_PAD_Z | GC_PAD_L |
                                                  GC_PAD_R | GC_PAD_START);
    EXPECT_EQ(notes.size(), 1);

    // Past the line limit nothing more is read.
    notes.clear();
    std::string many = "[remote]\n";
    for (std::size_t i = 0; i < riftwii::kMaxGcControlsLines; ++i) many += "\n";
    many += "B = Start\n";
    gc_controls_apply(many, "default.txt", map, notes);
    EXPECT_EQ(map.to[GC_MAP_REMOTE][GC_IN_B], GC_PAD_R);
    EXPECT_TRUE(has_note_with(notes, "more than"));

    // A file of nothing but mistakes: a bounded number of notes, then one
    // that says so; and junk bytes are not copied into the log.
    notes.clear();
    std::string junk = "[remote]\n";
    for (int i = 0; i < 100; ++i) junk += "\x01\xFF" + std::string(60, 'q') + " = A\n";
    gc_controls_apply(junk, "default.txt", map, notes);
    EXPECT_EQ(notes.size(), riftwii::kMaxGcControlsNotes + 1);
    for (const std::string& n : notes)
        for (char c : n) EXPECT_TRUE(c >= 0x20 && c < 0x7F);
    EXPECT_TRUE(has_note_with(notes, "only the first"));
}

void test_paths() {
    const std::vector<std::string> paths = riftwii::gc_controls_paths("GLMP01");
    EXPECT_EQ(paths.size(), 2);
    EXPECT_TRUE(paths[0] == "sd:/riftwii/gc/controls/default.txt");
    EXPECT_TRUE(paths[1] == "sd:/riftwii/gc/controls/GLMP01.txt");
    // An ID that could climb out of the folder gets no file of its own.
    EXPECT_EQ(riftwii::gc_controls_paths("../../a").size(), 1);
    EXPECT_EQ(riftwii::gc_controls_paths("GLM/01").size(), 1);
    EXPECT_EQ(riftwii::gc_controls_paths("").size(), 1);
}

void test_describe() {
    EXPECT_TRUE(riftwii::gc_controls_describe(0) == "nothing");
    EXPECT_TRUE(riftwii::gc_controls_describe(GC_PAD_UP | GC_OUT_STICK_UP) == "Up StickUp");
    EXPECT_TRUE(riftwii::gc_controls_describe(GC_PAD_START | GC_PAD_A) == "A Start");
}

void test_shake_jolt() {
    const std::uint8_t still[3] = {0x80, 0x80, 0x9A};
    const std::uint8_t tilted[3] = {0x88, 0x80, 0x98};  // a slow turn: 10 from one report to the next
    const std::uint8_t flicked[3] = {0xA0, 0x80, 0x9A}; // 32 on one axis: not yet
    const std::uint8_t shaken[3] = {0xA0, 0x90, 0x9A};  // 48 over two
    EXPECT_TRUE(!gc_shake_jolt(still, still));
    EXPECT_TRUE(!gc_shake_jolt(still, tilted));
    EXPECT_TRUE(!gc_shake_jolt(still, flicked));
    EXPECT_TRUE(gc_shake_jolt(still, shaken));
    EXPECT_TRUE(gc_shake_jolt(shaken, still));  // either way
}

// Shaking on: the button is pressed GC_SHAKE_PRESS PADReads, let go at
// least GC_SHAKE_REST, pressed again. One jolt: one press.
void test_shake_presses() {
    std::uint8_t timer = 0, jolted = 0;
    std::string pattern;
    for (int frame = 0; frame < 30; ++frame) {
        jolted = 1;
        pattern += gc_shake_step(&timer, &jolted) ? 'X' : '.';
        EXPECT_EQ(jolted, 0);
    }
    EXPECT_TRUE(pattern == "XXXXXX.......XXXXXX.......XXXX");

    timer = 0;
    jolted = 1;
    pattern.clear();
    for (int frame = 0; frame < 20; ++frame) pattern += gc_shake_step(&timer, &jolted) ? 'X' : '.';
    EXPECT_TRUE(pattern == "XXXXXX..............");

    // No jolt: never pressed.
    timer = 0;
    jolted = 0;
    EXPECT_TRUE(!gc_shake_step(&timer, &jolted));
}

// A Classic's analogue trigger goes with its button: mapped away, it no
// longer half-presses the GameCube's trigger as well.
void test_trigger_follows_button() {
    gc_map map;
    gc_controls_default(map);
    EXPECT_TRUE(gc_map_keeps_trigger(&map, GC_IN_L, GC_PAD_L));
    EXPECT_TRUE(gc_map_keeps_trigger(&map, GC_IN_R, GC_PAD_R));
    std::vector<std::string> notes;
    EXPECT_EQ(gc_controls_apply("[classic]\nR = Y\n", "GMSE01.txt", map, notes), 1);
    EXPECT_TRUE(!gc_map_keeps_trigger(&map, GC_IN_R, GC_PAD_R));
    EXPECT_TRUE(gc_map_keeps_trigger(&map, GC_IN_L, GC_PAD_L));
    EXPECT_EQ(press(map, GC_MAP_CLASSIC, held(GC_IN_R)).buttons, GC_PAD_Y);
    // Mapped to the other trigger's button, it is not R's any more either.
    EXPECT_EQ(gc_controls_apply("[classic]\nL = R\n", "GMSE01.txt", map, notes), 1);
    EXPECT_TRUE(!gc_map_keeps_trigger(&map, GC_IN_L, GC_PAD_L));
}

// A Classic Controller's two button bytes (active low) as the map's inputs.
void test_classic_buttons() {
    EXPECT_EQ(gc_classic_held(0xFF, 0xFF), 0);
    EXPECT_EQ(gc_classic_held(0xFF, 0xEF), held(GC_IN_A));
    EXPECT_EQ(gc_classic_held(0xFF, 0xFE), held(GC_IN_UP));
    EXPECT_EQ(gc_classic_held(0xFF, 0x7B), held(GC_IN_ZL) | held(GC_IN_ZR));
    EXPECT_EQ(gc_classic_held(0xF7, 0xFF), held(GC_IN_HOME));
    EXPECT_EQ(gc_classic_held(0xDD, 0xFF), held(GC_IN_L) | held(GC_IN_R));
    EXPECT_EQ(gc_classic_held(0x3F, 0xFF), held(GC_IN_RIGHT) | held(GC_IN_DOWN));
    // Bit 0 of the fifth byte is no button (always 1 on a Classic Controller).
    EXPECT_EQ(gc_classic_held(0xFE, 0xFF), 0);
}

// One of a Pro Controller's 12-bit stick axes, little-endian.
void set_axis(std::uint8_t* at, std::uint32_t raw) {
    at[0] = static_cast<std::uint8_t>(raw);
    at[1] = static_cast<std::uint8_t>(raw >> 8);
}

// A Wii U Pro Controller's eleven bytes (report 0x34) as the GameCube side
// reads them, and through the Classic Controller's table of the map.
void test_pro_decode() {
    EXPECT_TRUE(gc_pro_is_id(0x01, 0x20));
    EXPECT_TRUE(!gc_pro_is_id(0x01, 0x01));  // a Classic Controller
    EXPECT_TRUE(!gc_pro_is_id(0x00, 0x00));  // a Nunchuk

    std::uint8_t pro[GC_PRO_BYTES];
    gc_pro_rest(pro);
    EXPECT_EQ(pro[0], 0x00);
    EXPECT_EQ(pro[1], 0x08);
    gc_pro_input in = {1, 1, 1, 1, 1};
    gc_pro_decode(pro, &in);
    EXPECT_EQ(in.held, 0);
    EXPECT_EQ(in.stick_x, 0);
    EXPECT_EQ(in.stick_y, 0);
    EXPECT_EQ(in.cstick_x, 0);
    EXPECT_EQ(in.cstick_y, 0);
    EXPECT_TRUE(!gc_pro_home_held(pro));

    // Left stick right and up, right stick left and down, all the way.
    set_axis(pro + 0, GC_PRO_STICK_CENTRE + GC_PRO_STICK_REACH);
    set_axis(pro + 4, GC_PRO_STICK_CENTRE + GC_PRO_STICK_REACH);
    set_axis(pro + 2, GC_PRO_STICK_CENTRE - GC_PRO_STICK_REACH);
    set_axis(pro + 6, GC_PRO_STICK_CENTRE - GC_PRO_STICK_REACH);
    gc_pro_decode(pro, &in);
    EXPECT_EQ(in.stick_x, GC_STICK_FULL);
    EXPECT_EQ(in.stick_y, GC_STICK_FULL);
    EXPECT_EQ(in.cstick_x, -GC_STICK_FULL);
    EXPECT_EQ(in.cstick_y, -GC_STICK_FULL);
    // Half way, and past the reach (clamped), and the top nibble of the
    // high byte (not the stick's) left out.
    set_axis(pro + 0, GC_PRO_STICK_CENTRE + GC_PRO_STICK_REACH / 2);
    set_axis(pro + 2, 0x000);
    set_axis(pro + 4, 0xFFF);
    pro[7] = static_cast<std::uint8_t>(0xF0 | (GC_PRO_STICK_CENTRE >> 8));
    pro[6] = 0x00;
    gc_pro_decode(pro, &in);
    EXPECT_EQ(in.stick_x, GC_STICK_FULL / 2);
    EXPECT_EQ(static_cast<std::uint32_t>(in.cstick_x), static_cast<std::uint32_t>(-127));
    EXPECT_EQ(in.stick_y, 127);
    EXPECT_EQ(in.cstick_y, 0);

    // The buttons, in a Classic Controller's places: A and ZR, then HOME.
    gc_pro_rest(pro);
    pro[9] = 0xFF & ~0x10 & ~0x04;
    gc_pro_decode(pro, &in);
    EXPECT_EQ(in.held, held(GC_IN_A) | held(GC_IN_ZR));
    gc_map map;
    gc_controls_default(map);
    EXPECT_EQ(press(map, GC_MAP_CLASSIC, in.held).buttons, GC_PAD_A | GC_PAD_Z);
    pro[8] = 0xFF & ~0x08;
    EXPECT_TRUE(gc_pro_home_held(pro));
    // + and R: Start and R, as on a Classic Controller.
    gc_pro_rest(pro);
    pro[8] = 0xFF & ~0x04 & ~0x02;
    gc_pro_decode(pro, &in);
    EXPECT_EQ(press(map, GC_MAP_CLASSIC, in.held).buttons, GC_PAD_START | GC_PAD_R);
}

}  // namespace

int main() {
    test_classic_buttons();
    test_pro_decode();
    test_default_remote_sideways();
    test_default_nunchuk_and_classic();
    test_sticks_kept();
    test_file_changes_only_its_lines();
    test_guide_sample();
    test_unknown_names_left_out();
    test_bounds();
    test_paths();
    test_describe();
    test_shake_jolt();
    test_shake_presses();
    test_trigger_follows_button();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("gccontrols: all passed\n");
    return 0;
}
