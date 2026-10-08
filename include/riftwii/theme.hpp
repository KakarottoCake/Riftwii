// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Menu themes (docs/THEMES.md). A theme is a folder,
// sd:/riftwii/themes/<folder>/, holding theme.ini and, optionally, PNG
// pictures that replace the ones RiftWii paints (theme_images()) and a
// music.ogg. theme.ini is "key = value" lines in [sections], "#" or ";"
// comments:
//
//   [theme]   name = Midnight          author = someone
//   [colors]  accent = #2FB6E9         shadow = #28283C22   (one key per
//             ThemeColors field below, by its name)
//   [shape]   corners = 1.0            (0 square .. 2 twice as round)
//   [backdrop] stripes = yes           (yes/no)
//
// Anything missing keeps the default (RiftWii's own light look). Anything
// wrong is skipped with a note, never an error: a broken theme still
// gives a working menu.
namespace riftwii {

struct ThemeColor {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const ThemeColor& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
};

// Every colour the menu uses, by the key theme.ini gives it.
struct ThemeColors {
    // Text.
    ThemeColor ink{46, 46, 54, 255};            // titles and labels
    ThemeColor ink_soft{74, 74, 84, 255};       // values, secondary text
    ThemeColor ink_dim{106, 106, 116, 255};     // hints, footers
    ThemeColor clock{116, 116, 126, 255};       // Home's clock
    ThemeColor accent{47, 182, 233, 255};       // highlights, outlines, the bar's curve
    ThemeColor accent_ink{14, 100, 136, 255};   // text in the accent's colour (switched-on values)
    ThemeColor text_on_accent{255, 255, 255, 255};
    ThemeColor warn{176, 58, 46, 255};          // warnings, errors
    // Painted parts.
    ThemeColor card{255, 255, 255, 255};        // tiles, buttons, panels
    ThemeColor card_edge{207, 207, 214, 255};
    ThemeColor card_edge_strong{196, 196, 206, 255};  // round buttons' rims
    ThemeColor shadow{40, 40, 60, 34};
    ThemeColor glow{47, 182, 233, 80};          // around the highlighted part
    ThemeColor glyph{85, 85, 95, 255};          // icons and arrows
    ThemeColor chip_on{227, 245, 252, 255};     // an option's value box, changed from the default
    ThemeColor chip_off{244, 244, 246, 255};
    ThemeColor chip_off_edge{208, 208, 216, 255};
    ThemeColor switch_off{212, 212, 219, 255};
    ThemeColor bar{247, 247, 249, 255};         // Home's bottom bar, the HOME Menu's top bar
    ThemeColor backdrop{236, 236, 239, 255};    // behind every screen
    ThemeColor backdrop_stripe{227, 227, 232, 255};
    ThemeColor banner_stripe{255, 255, 255, 20};  // over a game page's banner
    // Mixed into each game's own colour (a game page's banner, plain spines
    // on the shelf) by its alpha: 00 leaves the games' colours as they are.
    ThemeColor banner_tint{0, 0, 0, 0};
    ThemeColor divider{232, 232, 238, 255};     // between list rows
    ThemeColor scroll_track{230, 230, 236, 255};
    ThemeColor scroll_thumb{168, 168, 180, 255};
    ThemeColor badge{236, 236, 241, 255};       // a tile's DISC/USB/SD label
    ThemeColor shelf{196, 160, 120, 255};       // Home's shelf: the plank's top
    ThemeColor shelf_edge{150, 112, 76, 255};   // and its front edge
    // The four players' pointers (outline colours).
    ThemeColor pointer1{59, 143, 214, 255};
    ThemeColor pointer2{214, 69, 69, 255};
    ThemeColor pointer3{63, 163, 77, 255};
    ThemeColor pointer4{217, 162, 27, 255};
};

struct Theme {
    std::string name;     // [theme] name; empty: the folder's name is shown
    std::string author;
    ThemeColors colors;
    float corners = 1.0f;  // [shape] corners: radii are multiplied by it, 0 to 2
    bool stripes = true;   // [backdrop] stripes
    bool gloss = false;    // [shape] gloss: a shine across the top of buttons and tiles
    // [shape] bar: "dip" (the Wii Menu's: the bar sinks in the middle and
    // the clock sits in the dip) or "bump" (it rises there, the clock above).
    bool bar_bump = false;
};

// The default look (no theme), as theme.ini would give it.
Theme default_theme();

// "#RRGGBB", "#RRGGBBAA", "RRGGBB" or "RRGGBBAA", any case.
bool parse_theme_color(const std::string& text, ThemeColor& out);
// "#RRGGBB" (alpha 255) or "#RRGGBBAA", upper case.
std::string format_theme_color(const ThemeColor& c);

// Reads theme.ini's text over the defaults. Each key, value or line it
// skips adds one line to `notes`, naming the line number and why.
Theme parse_theme(const std::string& text, std::vector<std::string>& notes);

// The colour keys in ThemeColors order, for docs and the sample theme.
struct ThemeColorKey {
    const char* key;
    ThemeColor ThemeColors::*field;
};
const std::vector<ThemeColorKey>& theme_color_keys();

// The pictures a theme may replace: <name>.png in the theme's folder,
// exactly w x h pixels (a different size is skipped with a note). Each
// includes the transparent margin RiftWii paints the shadow or glow in.
struct ThemeImage {
    const char* name;
    int w, h;
};
const std::vector<ThemeImage>& theme_images();

}  // namespace riftwii
