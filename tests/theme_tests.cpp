// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/inputicons.hpp"
#include "riftwii/skinpaint.hpp"
#include "riftwii/theme.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static bool colors_equal(const ThemeColors& a, const ThemeColors& b) {
    for (const auto& entry : theme_color_keys()) {
        if (!(a.*(entry.field) == b.*(entry.field))) {
            return false;
        }
    }
    return true;
}

static bool themes_equal(const Theme& a, const Theme& b) {
    return a.name == b.name &&
           a.author == b.author &&
           a.corners == b.corners &&
           a.stripes == b.stripes &&
           colors_equal(a.colors, b.colors);
}

static bool note_mentions_line(const std::string& note, int line) {
    const std::string prefix = "theme.ini line " + std::to_string(line) + ":";
    return note.find(prefix) == 0;
}

static void test_default_theme() {
    const Theme def = default_theme();
    EXPECT_EQ(def.name, "");
    EXPECT_EQ(def.author, "");
    EXPECT_EQ(def.corners, 1.0f);
    EXPECT_TRUE(def.stripes);

    // Empty text parse produces default theme and 0 notes.
    std::vector<std::string> notes;
    const Theme parsed_empty = parse_theme("", notes);
    EXPECT_EQ(notes.size(), 0U);
    EXPECT_TRUE(themes_equal(def, parsed_empty));

    // Comments only and BOM only.
    notes.clear();
    const Theme parsed_bom = parse_theme("\xEF\xBB\xBF# comment\r\n; another\r\n\r\n", notes);
    EXPECT_EQ(notes.size(), 0U);
    EXPECT_TRUE(themes_equal(def, parsed_bom));

    // The bar dips unless a theme says bump.
    EXPECT_TRUE(!def.bar_bump);
    notes.clear();
    EXPECT_TRUE(parse_theme("[shape]\nbar = bump\n", notes).bar_bump);
    EXPECT_EQ(notes.size(), 0U);
    // The HOME Menu is the Wii's unless a theme says ios6.
    EXPECT_TRUE(!def.home_ios6);
    EXPECT_TRUE(parse_theme("[shape]\nhome_menu = iOS6\n", notes).home_ios6);
    EXPECT_EQ(notes.size(), 0U);
    EXPECT_TRUE(!parse_theme("[shape]\nhome_menu = mac\n", notes).home_ios6);
    EXPECT_EQ(notes.size(), 1U);
    notes.clear();

    // The games' colours are left alone unless a theme tints them.
    EXPECT_EQ(def.colors.banner_tint.a, 0);
    notes.clear();
    const Theme tinted = parse_theme("[colors]\nbanner_tint = #8A5A2E73\n", notes);
    EXPECT_EQ(notes.size(), 0U);
    EXPECT_TRUE(tinted.colors.banner_tint == (ThemeColor{0x8A, 0x5A, 0x2E, 0x73}));
}

static void test_parse_theme_color() {
    ThemeColor out;

    // Standard 6-digit hex with #.
    EXPECT_TRUE(parse_theme_color("#2FB6E9", out));
    EXPECT_EQ(out.r, 0x2F);
    EXPECT_EQ(out.g, 0xB6);
    EXPECT_EQ(out.b, 0xE9);
    EXPECT_EQ(out.a, 255);

    // Lowercase 6-digit hex without #.
    EXPECT_TRUE(parse_theme_color("2fb6e9", out));
    EXPECT_EQ(out.r, 0x2F);
    EXPECT_EQ(out.g, 0xB6);
    EXPECT_EQ(out.b, 0xE9);
    EXPECT_EQ(out.a, 255);

    // 8-digit hex with alpha.
    EXPECT_TRUE(parse_theme_color("#28283C22", out));
    EXPECT_EQ(out.r, 0x28);
    EXPECT_EQ(out.g, 0x28);
    EXPECT_EQ(out.b, 0x3C);
    EXPECT_EQ(out.a, 0x22);

    // 8-digit hex without #.
    EXPECT_TRUE(parse_theme_color("28283c22", out));
    EXPECT_EQ(out.r, 0x28);
    EXPECT_EQ(out.g, 0x28);
    EXPECT_EQ(out.b, 0x3C);
    EXPECT_EQ(out.a, 0x22);

    // Rejection cases leave out unchanged.
    const ThemeColor sentinel{99, 99, 99, 99};
    out = sentinel;

    EXPECT_FALSE(parse_theme_color("#12345", out));
    EXPECT_TRUE(out == sentinel);

    EXPECT_FALSE(parse_theme_color("#1234567", out));
    EXPECT_TRUE(out == sentinel);

    EXPECT_FALSE(parse_theme_color("#GG0000", out));
    EXPECT_TRUE(out == sentinel);

    EXPECT_FALSE(parse_theme_color("", out));
    EXPECT_TRUE(out == sentinel);

    EXPECT_FALSE(parse_theme_color("# 2FB6E9", out));
    EXPECT_TRUE(out == sentinel);
}

static void test_format_theme_color() {
    // Form 1: alpha == 255 -> #RRGGBB.
    const ThemeColor c1{0x2F, 0xB6, 0xE9, 255};
    const std::string s1 = format_theme_color(c1);
    EXPECT_EQ(s1, "#2FB6E9");

    ThemeColor parsed1;
    EXPECT_TRUE(parse_theme_color(s1, parsed1));
    EXPECT_TRUE(parsed1 == c1);

    // Form 2: alpha != 255 -> #RRGGBBAA.
    const ThemeColor c2{0x28, 0x28, 0x3C, 0x22};
    const std::string s2 = format_theme_color(c2);
    EXPECT_EQ(s2, "#28283C22");

    ThemeColor parsed2;
    EXPECT_TRUE(parse_theme_color(s2, parsed2));
    EXPECT_TRUE(parsed2 == c2);

    // Alpha 0.
    const ThemeColor c3{0x01, 0x02, 0x03, 0};
    const std::string s3 = format_theme_color(c3);
    EXPECT_EQ(s3, "#01020300");
    ThemeColor parsed3;
    EXPECT_TRUE(parse_theme_color(s3, parsed3));
    EXPECT_TRUE(parsed3 == c3);
}

static void test_full_valid_file() {
    const std::string content =
        "\xEF\xBB\xBF# Theme configuration\r\n"
        "; Commentary\r\n"
        "\r\n"
        "[theme]\r\n"
        "name = \"Midnight Sky\"\r\n"
        "author = RiftWii Team\r\n"
        "\r\n"
        "[colors]\r\n"
        "accent = #2FB6E9\r\n"
        "shadow = #28283C22\r\n"
        "ink = 123456\r\n"
        "card = #FFFFFF\r\n"
        "\r\n"
        "[shape]\r\n"
        "corners = 1.5\r\n"
        "\r\n"
        "[backdrop]\r\n"
        "stripes = no\r\n";

    std::vector<std::string> notes;
    const Theme theme = parse_theme(content, notes);

    EXPECT_EQ(notes.size(), 0U);
    EXPECT_EQ(theme.name, "Midnight Sky");
    EXPECT_EQ(theme.author, "RiftWii Team");
    EXPECT_EQ(theme.corners, 1.5f);
    EXPECT_FALSE(theme.stripes);

    EXPECT_TRUE(theme.colors.accent == (ThemeColor{0x2F, 0xB6, 0xE9, 255}));
    EXPECT_TRUE(theme.colors.shadow == (ThemeColor{0x28, 0x28, 0x3C, 0x22}));
    EXPECT_TRUE(theme.colors.ink == (ThemeColor{0x12, 0x34, 0x56, 255}));
    EXPECT_TRUE(theme.colors.card == (ThemeColor{0xFF, 0xFF, 0xFF, 255}));
    // Unchanged colors remain defaults.
    EXPECT_TRUE(theme.colors.clock == default_theme().colors.clock);
}

static void test_skipped_cases() {
    // 1. bad colour
    {
        const std::string ini = "[colors]\naccent = #12345\n";
        std::vector<std::string> notes;
        const Theme t = parse_theme(ini, notes);
        EXPECT_EQ(notes.size(), 1U);
        if (notes.size() == 1U) {
            EXPECT_TRUE(note_mentions_line(notes[0], 2));
        }
        EXPECT_TRUE(t.colors.accent == default_theme().colors.accent);
    }

    // 2. unknown colour key
    {
        const std::string ini = "[colors]\nunknown_col = #112233\n";
        std::vector<std::string> notes;
        parse_theme(ini, notes);
        EXPECT_EQ(notes.size(), 1U);
        if (notes.size() == 1U) {
            EXPECT_TRUE(note_mentions_line(notes[0], 2));
        }
    }

    // 3. unknown section (one note even with 3 keys in it)
    {
        const std::string ini =
            "[custom_section]\n"
            "key1 = val1\n"
            "key2 = val2\n"
            "key3 = val3\n";
        std::vector<std::string> notes;
        parse_theme(ini, notes);
        EXPECT_EQ(notes.size(), 1U);
        if (notes.size() == 1U) {
            EXPECT_TRUE(note_mentions_line(notes[0], 1));
        }
    }

    // 4. key before any section
    {
        const std::string ini =
            "name = BeforeAnySection\n"
            "[theme]\n"
            "name = AfterSection\n";
        std::vector<std::string> notes;
        const Theme t = parse_theme(ini, notes);
        EXPECT_EQ(notes.size(), 1U);
        if (notes.size() == 1U) {
            EXPECT_TRUE(note_mentions_line(notes[0], 1));
        }
        EXPECT_EQ(t.name, "AfterSection");
    }

    // 5. line without '='
    {
        const std::string ini =
            "[theme]\n"
            "this line has no equals sign\n";
        std::vector<std::string> notes;
        parse_theme(ini, notes);
        EXPECT_EQ(notes.size(), 1U);
        if (notes.size() == 1U) {
            EXPECT_TRUE(note_mentions_line(notes[0], 2));
        }
    }

    // 6. corners out of range (clamped: 5 -> 2, and value 2 kept)
    {
        const std::string ini_clamp_high = "[shape]\ncorners = 5\n";
        std::vector<std::string> notes;
        const Theme t = parse_theme(ini_clamp_high, notes);
        EXPECT_EQ(notes.size(), 1U);
        if (notes.size() == 1U) {
            EXPECT_TRUE(note_mentions_line(notes[0], 2));
        }
        EXPECT_EQ(t.corners, 2.0f);

        // Value 2 kept with zero notes.
        const std::string ini_exact_two = "[shape]\ncorners = 2\n";
        notes.clear();
        const Theme t2 = parse_theme(ini_exact_two, notes);
        EXPECT_EQ(notes.size(), 0U);
        EXPECT_EQ(t2.corners, 2.0f);

        // Clamped low: -1 -> 0
        const std::string ini_clamp_low = "[shape]\ncorners = -1\n";
        notes.clear();
        const Theme t_low = parse_theme(ini_clamp_low, notes);
        EXPECT_EQ(notes.size(), 1U);
        if (notes.size() == 1U) {
            EXPECT_TRUE(note_mentions_line(notes[0], 2));
        }
        EXPECT_EQ(t_low.corners, 0.0f);
    }

    // 7. corners not a number
    {
        const std::string ini = "[shape]\ncorners = notanumber\n";
        std::vector<std::string> notes;
        const Theme t = parse_theme(ini, notes);
        EXPECT_EQ(notes.size(), 1U);
        if (notes.size() == 1U) {
            EXPECT_TRUE(note_mentions_line(notes[0], 2));
        }
        EXPECT_EQ(t.corners, 1.0f);
    }

    // 8. stripes = maybe
    {
        const std::string ini = "[backdrop]\nstripes = maybe\n";
        std::vector<std::string> notes;
        const Theme t = parse_theme(ini, notes);
        EXPECT_EQ(notes.size(), 1U);
        if (notes.size() == 1U) {
            EXPECT_TRUE(note_mentions_line(notes[0], 2));
        }
        EXPECT_TRUE(t.stripes);
    }

    // 9. name longer than 64 characters (cut)
    {
        const std::string long_name =
            "12345678901234567890123456789012345678901234567890123456789012345";  // 65 chars
        const std::string ini = "[theme]\nname = " + long_name + "\n";
        std::vector<std::string> notes;
        const Theme t = parse_theme(ini, notes);
        EXPECT_EQ(notes.size(), 1U);
        if (notes.size() == 1U) {
            EXPECT_TRUE(note_mentions_line(notes[0], 2));
        }
        EXPECT_EQ(t.name.size(), 64U);
        EXPECT_EQ(t.name, long_name.substr(0, 64));
    }

    // 10. author longer than 64 characters (cut)
    {
        const std::string long_author =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^";  // 68 chars
        const std::string ini = "[theme]\nauthor = " + long_author + "\n";
        std::vector<std::string> notes;
        const Theme t = parse_theme(ini, notes);
        EXPECT_EQ(notes.size(), 1U);
        if (notes.size() == 1U) {
            EXPECT_TRUE(note_mentions_line(notes[0], 2));
        }
        EXPECT_EQ(t.author.size(), 64U);
        EXPECT_EQ(t.author, long_author.substr(0, 64));
    }
}

static void test_theme_color_keys() {
    const auto& keys = theme_color_keys();
    EXPECT_EQ(keys.size(), 33U);

    // Unique keys.
    for (std::size_t i = 0; i < keys.size(); ++i) {
        for (std::size_t j = i + 1; j < keys.size(); ++j) {
            EXPECT_TRUE(std::string(keys[i].key) != std::string(keys[j].key));
        }
    }

    // Writing through each member pointer changes that field only.
    for (std::size_t i = 0; i < keys.size(); ++i) {
        ThemeColors colors = default_theme().colors;
        const ThemeColors baseline = colors;
        const ThemeColor distinct{
            static_cast<std::uint8_t>(i + 1),
            static_cast<std::uint8_t>(i + 2),
            static_cast<std::uint8_t>(i + 3),
            static_cast<std::uint8_t>(i + 4)
        };
        colors.*(keys[i].field) = distinct;

        for (std::size_t j = 0; j < keys.size(); ++j) {
            if (i == j) {
                EXPECT_TRUE(colors.*(keys[j].field) == distinct);
            } else {
                EXPECT_TRUE(colors.*(keys[j].field) == baseline.*(keys[j].field));
            }
        }
    }
}

static void test_theme_images() {
    const auto& images = theme_images();
    EXPECT_EQ(images.size(), 80U);

    // Unique names.
    for (std::size_t i = 0; i < images.size(); ++i) {
        for (std::size_t j = i + 1; j < images.size(); ++j) {
            EXPECT_TRUE(std::string(images[i].name) != std::string(images[j].name));
        }
    }

    // Every w and h a multiple of 4, positive.
    for (const auto& img : images) {
        EXPECT_TRUE(img.w > 0);
        EXPECT_TRUE(img.h > 0);
        EXPECT_EQ(img.w % 4, 0);
        EXPECT_EQ(img.h % 4, 0);
    }

    // Check first and last entries.
    EXPECT_EQ(std::string(images[0].name), "background");
    EXPECT_EQ(images[0].w, 640);
    EXPECT_EQ(images[0].h, 480);

    EXPECT_EQ(std::string(images[63].name), "pointer4");
    EXPECT_EQ(images[63].w, 96);
    EXPECT_EQ(images[63].h, 96);
}

static void test_case_insensitivity_and_boolean_formats() {
    const std::string content =
        "[THEME]\r\n"
        "NAME = UppercaseName\r\n"
        "AUTHOR = UppercaseAuthor\r\n"
        "[SHAPE]\r\n"
        "CORNERS = 0.75\r\n"
        "[BACKDROP]\r\n"
        "STRIPES = TRUE\r\n";

    std::vector<std::string> notes;
    const Theme t = parse_theme(content, notes);
    EXPECT_EQ(notes.size(), 0U);
    EXPECT_EQ(t.name, "UppercaseName");
    EXPECT_EQ(t.author, "UppercaseAuthor");
    EXPECT_EQ(t.corners, 0.75f);
    EXPECT_TRUE(t.stripes);

    // Test other boolean false forms: false, off, 0
    const char* false_forms[] = {"false", "off", "0", "FALSE", "OFF"};
    for (const char* form : false_forms) {
        notes.clear();
        const std::string s = std::string("[backdrop]\nstripes = ") + form + "\n";
        const Theme tf = parse_theme(s, notes);
        EXPECT_EQ(notes.size(), 0U);
        EXPECT_FALSE(tf.stripes);
    }

    // Test other boolean true forms: yes, on, 1
    const char* true_forms[] = {"yes", "on", "1", "YES", "ON"};
    for (const char* form : true_forms) {
        notes.clear();
        const std::string s = std::string("[backdrop]\nstripes = ") + form + "\n";
        const Theme tt = parse_theme(s, notes);
        EXPECT_EQ(notes.size(), 0U);
        EXPECT_TRUE(tt.stripes);
    }
}

// Every picture a theme may replace is painted at the size theme_images()
// lists, in the theme's colours; nothing else is.
static void test_painted_images() {
    const Theme def = default_theme();
    for (const ThemeImage& im : theme_images()) {
        Canvas c(0, 0);
        EXPECT_TRUE(paint_theme_image(im.name, def, c));
        EXPECT_EQ(c.width(), im.w);
        EXPECT_EQ(c.height(), im.h);
    }
    Canvas none(0, 0);
    EXPECT_FALSE(paint_theme_image("tile2", def, none));
    EXPECT_FALSE(paint_theme_image("", def, none));

    // Each layout needs its own recognisable silhouette, with a clear
    // margin when it is enlarged on a card or shrunk onto a button.
    const char* const layouts[] = {
        "layout_covers", "layout_covers3d", "layout_carousel",
        "layout_shelf", "layout_channels", "layout_names"
    };
    std::vector<std::vector<std::uint8_t>> layoutPixels;
    Theme layoutTheme = def;
    layoutTheme.colors.glyph = ThemeColor{210, 30, 70, 255};
    for (const char* name : layouts) {
        Canvas icon(0, 0);
        EXPECT_TRUE(paint_theme_image(name, layoutTheme, icon));
        EXPECT_EQ(icon.at(0, 0).a, 0);
        EXPECT_EQ(icon.at(27, 27).a, 0);
        bool hasGlyph = false;
        for (int y = 0; y < icon.height(); ++y) {
            for (int x = 0; x < icon.width(); ++x) {
                const Rgba pixel = icon.at(x, y);
                if (pixel.a == 255 && pixel.r == 210 && pixel.g == 30 && pixel.b == 70) hasGlyph = true;
            }
        }
        EXPECT_TRUE(hasGlyph);
        for (const auto& previous : layoutPixels) EXPECT_TRUE(previous != icon.pixels());
        layoutPixels.push_back(icon.pixels());
    }

    Theme red = def;
    red.colors.chip_on = ThemeColor{200, 10, 20, 255};
    red.colors.backdrop = ThemeColor{1, 2, 3, 255};
    red.stripes = false;
    Canvas chip(0, 0), back(0, 0);
    EXPECT_TRUE(paint_theme_image("chip_on", red, chip));
    // Shaded top to bottom about the theme's colour: its middle is (nearly) the colour.
    EXPECT_TRUE(std::abs(int(chip.at(106, 18).r) - 200) <= 4);
    EXPECT_TRUE(std::abs(int(chip.at(106, 18).g) - 10) <= 1);
    EXPECT_TRUE(paint_theme_image("background", red, back));
    // The pieces painted on the Wii as needed: their sizes.
    Canvas piece(0, 0);
    for (int k = 0; k < 4; ++k) {
        paint_key(def, k, piece);
        EXPECT_EQ(piece.width(), 40);
        EXPECT_EQ(piece.height(), 40);
    }
    paint_card9(def, piece);
    EXPECT_EQ(piece.width(), 64);
    paint_capsule9(piece);
    EXPECT_EQ(piece.width(), 48);
    paint_notice_icon(def, true, piece);
    EXPECT_EQ(piece.width(), 28);
    paint_home_danger(def, false, piece);
    EXPECT_EQ(piece.width(), 264);
    paint_ios_bar_button(def, true, piece);
    EXPECT_EQ(piece.width(), 128);
    paint_linen(piece);
    EXPECT_EQ(piece.width(), 64);
    EXPECT_TRUE(paint_theme_image("home_button_over", def, piece));
    EXPECT_EQ(piece.width(), 264);
    EXPECT_EQ(int(back.at(5, 2).r), 1);
    EXPECT_EQ(int(back.at(5, 2).b), 3);

    // Square corners fill the tile's corner; the default rounds it off.
    Theme square = def;
    square.corners = 0.0f;
    Canvas round_tile(0, 0), square_tile(0, 0);
    EXPECT_TRUE(paint_theme_image("tile", def, round_tile));
    EXPECT_TRUE(paint_theme_image("tile", square, square_tile));
    EXPECT_TRUE(square_tile.at(8, 8).a == 255);
    EXPECT_TRUE(round_tile.at(8, 8).a < square_tile.at(8, 8).a);

    // Wrapped notes need an opaque backplate even near the ends of their
    // first and last lines, where a tall capsule used to expose the tiles.
    Canvas note(0, 0);
    paint_hint_box(def, 520, 184, note);
    for (int y : {12, 172}) {
        for (int x : {20, 500}) {
            EXPECT_EQ(int(note.at(kHintBoxMargin + x, kHintBoxMargin + y).a), 255);
        }
    }

    // The clock wall is painted natively at 16:9 (856 wide), so the menu
    // never has to mirror the 640 one's sides: mirrored, its NOTHINGs read
    // backwards and the figure stood in the middle of Settings.
    Theme clocks = red;
    clocks.clock_dials = true;
    Canvas dials(0, 0);
    Canvas dials_wide(0, 0);
    EXPECT_TRUE(paint_theme_image("background", clocks, dials));
    EXPECT_TRUE(paint_theme_image("background_wide", clocks, dials_wide));
    EXPECT_EQ(dials.width(), 640);
    EXPECT_EQ(dials_wide.width(), 856);
    EXPECT_EQ(dials_wide.height(), 480);

    // A 16:9 game page lays copies of banner_stripes side by side, so the
    // picture's first column must carry on from its last one. Along a
    // stripe x + y is constant: what column 640 would hold at row y is what
    // column 639 holds at row y + 1. At a 28-pixel spacing they met 24
    // pixels out of step, a seam on every widescreen game page.
    Canvas stripes(0, 0);
    EXPECT_TRUE(paint_theme_image("banner_stripes", def, stripes));
    int seams = 0;
    for (int y = 0; y + 1 < stripes.height(); ++y) {
        const int first = stripes.at(0, y).a;
        const int next = stripes.at(stripes.width() - 1, y + 1).a;
        if (std::abs(first - next) > 8) ++seams;
    }
    EXPECT_EQ(seams, 0);
}

// The stick and D-pad pictures RiftWii draws itself (the GameCube controls
// popup's badges): named like Kenney's, inside the same area as his.
static void test_drawn_input_icons() {
    EXPECT_EQ(input_icon_for_button("Stick"), std::string("stick"));
    EXPECT_EQ(input_icon_for_button("D-pad"), std::string("dpad"));
    const Rgba ink{10, 20, 30, 255};

    Canvas stick(0, 0);
    EXPECT_TRUE(paint_input_icon("stick", 32, ink, stick));
    EXPECT_EQ(stick.width(), 32);
    EXPECT_EQ(int(stick.at(16, 16).a), 255);  // the stick's top
    EXPECT_EQ(int(stick.at(24, 16).a), 0);    // the groove round it
    EXPECT_EQ(int(stick.at(26, 16).a), 255);  // the rim
    EXPECT_EQ(int(stick.at(0, 0).a), 0);
    EXPECT_EQ(int(stick.at(31, 16).a), 0);    // Kenney's margin
    EXPECT_EQ(int(stick.at(16, 16).g), 20);

    Canvas dpad(0, 0);
    EXPECT_TRUE(paint_input_icon("dpad", 32, ink, dpad));
    EXPECT_EQ(int(dpad.at(16, 16).a), 0);    // the hole at its middle
    EXPECT_EQ(int(dpad.at(16, 6).a), 255);   // the up arm
    EXPECT_EQ(int(dpad.at(26, 15).a), 255);  // the right arm
    EXPECT_EQ(int(dpad.at(6, 6).a), 0);      // between two arms
}

// The channel frames are drawn nine-sliced kChannelFrameCorner in from
// each edge (wii/menu/skin.cpp's DrawNine): the slices between the corners
// are stretched to the tile's size, so they must hold straight edges only.
// Any of the rim's curve, glow or shadow there would be drawn wider than the
// icon's clip. Checked at every corners value up to the roundest (2: a
// theme's most, and Settings' Rounder), lit and plain, with and without the
// card within.
static void test_channel_frame_slices() {
    EXPECT_EQ(kChannelFrameMargin, 8);
    EXPECT_EQ(kChannelFrameCorner, 24);
    EXPECT_TRUE(kChannelRadius == 6.0f);
    EXPECT_TRUE(2 * kChannelFrameCorner < 80);
    const auto same = [](Rgba a, Rgba b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; };
    const float corner_values[] = {0.0f, 0.5f, 1.0f, 2.0f};
    for (float corners : corner_values) {
        Theme theme;
        theme.corners = corners;
        for (int kind = 0; kind < 4; ++kind) {
            const bool over = (kind & 1) != 0, fill = (kind & 2) != 0;
            Canvas frame(0, 0);
            paint_channel_frame9(theme, over, fill, frame);
            EXPECT_EQ(frame.width(), 80);
            EXPECT_EQ(frame.height(), 80);
            const int first = kChannelFrameCorner, last = 80 - kChannelFrameCorner;
            int curved = 0;
            for (int along = first + 1; along < last; ++along) {
                for (int across = 0; across < 80; ++across) {
                    if (!same(frame.at(along, across), frame.at(first, across))) ++curved;  // top and bottom edges
                    if (!same(frame.at(across, along), frame.at(across, first))) ++curved;  // left and right edges
                }
            }
            if (curved != 0)
                std::cerr << "corners " << corners << ", kind " << kind << ": " << curved
                          << " pixels differ in the stretched slices" << std::endl;
            EXPECT_EQ(curved, 0);
        }
    }
}

static void test_long_name_cut() {
    // A 90-byte name of 3-byte characters (E6 BC A2 is U+6F22): the
    // 64-byte cut keeps 21 whole ones, never half of the next.
    std::string kana;
    for (int i = 0; i < 30; ++i) {
        kana += static_cast<char>(0xE6);
        kana += static_cast<char>(0xBC);
        kana += static_cast<char>(0xA2);
    }
    std::vector<std::string> notes;
    const Theme theme =
        parse_theme("[theme]\nname = " + kana + "\nauthor = " + kana + "\n", notes);
    EXPECT_EQ(theme.name.size(), 63U);
    EXPECT_EQ(theme.author.size(), 63U);
    for (std::size_t i = 0; i < theme.name.size(); i += 3) {
        EXPECT_TRUE(static_cast<unsigned char>(theme.name[i]) == 0xE6u);
        EXPECT_TRUE(static_cast<unsigned char>(theme.name[i + 1]) == 0xBCu);
        EXPECT_TRUE(static_cast<unsigned char>(theme.name[i + 2]) == 0xA2u);
    }
    EXPECT_EQ(notes.size(), 2U);
    // Plain ASCII names still fill all 64 bytes.
    std::vector<std::string> plain_notes;
    const Theme plain = parse_theme("[theme]\nname = " + std::string(100, 'n') + "\n", plain_notes);
    EXPECT_EQ(plain.name.size(), 64U);
}
int main() {
    test_default_theme();
    test_parse_theme_color();
    test_format_theme_color();
    test_full_valid_file();
    test_skipped_cases();
    test_theme_color_keys();
    test_theme_images();
    test_painted_images();
    test_drawn_input_icons();
    test_channel_frame_slices();
    test_case_insensitivity_and_boolean_formats();
    test_long_name_cut();

    if (g_failures == 0) {
        std::cout << "ALL THEME TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
