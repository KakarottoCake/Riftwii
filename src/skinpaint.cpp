// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/skinpaint.hpp"

#include <cmath>
#include <cstring>
#include <vector>

namespace riftwii {
namespace {

Rgba ToRgba(const ThemeColor& c) { return Rgba{c.r, c.g, c.b, c.a}; }

// The theme's colours as the painters use them.
struct Painter {
    explicit Painter(const Theme& t)
        : card(ToRgba(t.colors.card)), edge(ToRgba(t.colors.card_edge)), edge_strong(ToRgba(t.colors.card_edge_strong)),
          shadow(ToRgba(t.colors.shadow)), accent(ToRgba(t.colors.accent)), glow(ToRgba(t.colors.glow)),
          glyph(ToRgba(t.colors.glyph)), chip_on(ToRgba(t.colors.chip_on)), chip_off(ToRgba(t.colors.chip_off)),
          chip_off_edge(ToRgba(t.colors.chip_off_edge)), switch_off(ToRgba(t.colors.switch_off)),
          bar(ToRgba(t.colors.bar)), banner_stripe(ToRgba(t.colors.banner_stripe)),
          backdrop(ToRgba(t.colors.backdrop)), backdrop_stripe(ToRgba(t.colors.backdrop_stripe)),
          corners(t.corners), stripes(t.stripes) {
        const ThemeColor* p[4] = {&t.colors.pointer1, &t.colors.pointer2, &t.colors.pointer3, &t.colors.pointer4};
        for (int i = 0; i < 4; ++i) pointer[i] = ToRgba(*p[i]);
    }

    Rgba card, edge, edge_strong, shadow, accent, glow, glyph, chip_on, chip_off, chip_off_edge, switch_off, bar,
        banner_stripe, backdrop, backdrop_stripe, pointer[4];
    float corners;
    bool stripes;

    // A corner radius as the theme rounds it.
    float R(float radius) const { return radius * corners; }

    // A card: shadow, body, border; `over` adds the accent outline and glow.
    Canvas Card(int w, int h, int margin, float radius, bool over, bool primary = false) const {
        // GX textures come in 4x4 tiles: pad right and bottom to fit.
        Canvas c((w + 2 * margin + 3) & ~3, (h + 2 * margin + 3) & ~3);
        const float x = static_cast<float>(margin), y = static_cast<float>(margin);
        // The card covers the shadows' insides: only their edges are worked out.
        radius = R(radius);
        if (over) c.shadow(x - 1, y - 1, w + 2.0f, h + 2.0f, radius + 1, margin - 1.0f, glow, 2.0f);
        c.shadow(x, y + 2, static_cast<float>(w), static_cast<float>(h), radius, 4, shadow, 3.0f);
        c.rounded_rect(x, y, static_cast<float>(w), static_cast<float>(h), radius, card);
        if (over || primary) {
            c.rounded_border(x, y, static_cast<float>(w), static_cast<float>(h), radius, primary ? 3.0f : 2.5f, accent);
        } else {
            c.rounded_border(x, y, static_cast<float>(w), static_cast<float>(h), radius, 1.5f, edge);
        }
        return c;
    }

    Canvas Round(bool over) const {
        Canvas c(80, 80);
        if (over) c.circle(40, 40, 39, glow);
        c.circle(40, 41.5f, 38, shadow);
        c.circle(40, 40, 37, card);
        c.ring(40, 40, 37, over ? 2.5f : 2.0f, over ? accent : edge_strong);
        return c;
    }

    Canvas Chip(bool on) const {
        Canvas c(212, 36);
        c.rounded_rect(2, 3, 208, 30, R(15), on ? chip_on : chip_off);
        c.rounded_border(2, 3, 208, 30, R(15), 2, on ? accent : chip_off_edge);
        return c;
    }

    Canvas Arrow(bool left, bool over) const {
        Canvas c(48, 48);
        if (over) c.circle(24, 24, 20.5f, glow);
        c.circle(24, 25, 18.5f, shadow);
        c.circle(24, 24, 18, card);
        c.ring(24, 24, 18, 2, over ? accent : edge_strong);
        const float s = left ? -1.0f : 1.0f;
        c.line(24 - 3 * s, 17, 24 + 4 * s, 24, 3.5f, glyph);
        c.line(24 + 4 * s, 24, 24 - 3 * s, 31, 3.5f, glyph);
        return c;
    }

    // A list's scroll arrow: 34 across, in a 44 canvas, pointing up or down.
    Canvas ScrollArrow(bool up, bool over) const {
        Canvas c(44, 44);
        if (over) c.circle(21, 21, 20.5f, glow);
        c.circle(21, 22.5f, 17.5f, shadow);
        c.circle(21, 21, 17, over ? chip_on : card);
        c.ring(21, 21, 17, 2, over ? accent : edge_strong);
        const float s = up ? -1.0f : 1.0f;
        c.line(14, 21 - 2.5f * s, 21, 21 + 3.5f * s, 3.2f, over ? accent : glyph);
        c.line(21, 21 + 3.5f * s, 28, 21 - 2.5f * s, 3.2f, over ? accent : glyph);
        return c;
    }

    // A list row's arrow button: 34 across, in a 44 canvas.
    Canvas Step(bool back, bool over) const {
        Canvas c(44, 44);
        if (over) c.circle(21, 21, 20.5f, glow);
        c.circle(21, 22.5f, 17.5f, shadow);
        c.circle(21, 21, 17, over ? chip_on : card);
        c.ring(21, 21, 17, 2, over ? accent : edge_strong);
        const float s = back ? -1.0f : 1.0f;
        c.line(21 - 2.5f * s, 14, 21 + 3.5f * s, 21, 3.2f, over ? accent : glyph);
        c.line(21 + 3.5f * s, 21, 21 - 2.5f * s, 28, 3.2f, over ? accent : glyph);
        return c;
    }

    // An On/Off switch: 60x30 at (3, 4).
    Canvas Switch(bool on) const {
        Canvas c(68, 40);
        c.rounded_rect(3, 4, 60, 30, R(15), on ? accent : switch_off);
        const float knob = on ? 48.0f : 18.0f;
        c.circle(knob, 20.5f, 12.5f, rgba(0x000000, 40));
        c.circle(knob, 19, 12, rgba(0xFFFFFF));
        return c;
    }

    Canvas Drives() const {
        Canvas c(28, 28);
        c.rounded_border(3, 4, 22, 8, 2.5f, 1.8f, glyph);
        c.rounded_border(3, 16, 22, 8, 2.5f, 1.8f, glyph);
        c.circle(8, 8, 1.3f, glyph);
        c.circle(8, 20, 1.3f, glyph);
        return c;
    }

    // A disc seen from above: silver, a track ring, the clear hub and its hole.
    Canvas Disc() const {
        Canvas c(40, 40);
        c.circle(20, 20, 18.5f, rgba(0xDCDCE4));
        c.ring(20, 20, 18.5f, 1.6f, glyph);
        c.ring(20, 20, 12.5f, 1.0f, rgba(0xB4B4C0));
        c.circle(20, 20, 6.5f, rgba(0xFFFFFF));
        c.ring(20, 20, 6.5f, 1.4f, glyph);
        c.ring(20, 20, 2.6f, 1.2f, glyph);
        return c;
    }

    Canvas Gear() const {
        Canvas c(28, 28);
        for (int i = 0; i < 8; ++i) {
            const float a = i * 3.14159265f / 4.0f;
            c.line(14 + 7.5f * std::cos(a), 14 + 7.5f * std::sin(a), 14 + 11.0f * std::cos(a),
                   14 + 11.0f * std::sin(a), 4.0f, glyph);
        }
        c.ring(14, 14, 8.5f, 3.0f, glyph);
        c.ring(14, 14, 3.5f, 1.8f, glyph);
        return c;
    }

    Canvas Search() const {
        Canvas c(28, 28);
        c.ring(12, 12, 7.5f, 2.8f, glyph);
        c.line(17.5f, 17.5f, 24.0f, 24.0f, 3.6f, glyph);
        return c;
    }

    // A white pointing hand with the player's colour as its outline; the
    // fingertip is the picture's centre, so the Wii Remote's roll turns the
    // hand about the point it aims at.
    static Canvas Hand(Rgba outline) {
        Canvas c(96, 96);
        struct Capsule { float x0, y0, x1, y1, w; };
        const Capsule parts[] = {
            {48, 54, 48, 70, 12},   // index finger
            {59, 67, 59, 75, 10},   // knuckles
            {67, 70, 67, 77, 9},
            {41, 79, 33, 71, 11},   // thumb
        };
        const auto shade = rgba(0x000000, 60);
        for (const Capsule& p : parts) c.line(p.x0 + 1.5f, p.y0 + 2.5f, p.x1 + 1.5f, p.y1 + 2.5f, p.w + 4, shade);
        c.rounded_rect(39.5f, 65.5f, 37, 30, 11, shade);
        for (const Capsule& p : parts) c.line(p.x0, p.y0, p.x1, p.y1, p.w + 4, outline);
        c.rounded_rect(36, 62, 37, 30, 11, outline);
        for (const Capsule& p : parts) c.line(p.x0, p.y0, p.x1, p.y1, p.w, rgba(0xFFFFFF));
        c.rounded_rect(38, 64, 33, 26, 9, rgba(0xFFFFFF));
        return c;
    }

    Canvas Bar() const {
        Canvas c(640, 124);
        std::vector<float> top(640);
        for (int x = 0; x < 640; ++x) {
            const float t = (x + 0.5f - 176.0f) / 288.0f;
            const float bump = (t > 0.0f && t < 1.0f) ? (1.0f - std::cos(t * 2.0f * 3.14159265f)) * 0.5f : 0.0f;
            top[x] = 46.0f - 32.0f * bump;
        }
        std::vector<float> shade(top);
        for (float& v : shade) v -= 3.0f;
        c.area_below(shade, rgba(0x000000, 18), 5.0f);  // the bar covers the rest
        c.area_below(top, bar);
        c.curve(top, 2.5f, accent);
        return c;
    }

    Canvas Stripes() const {
        Canvas c(640, 192);
        c.diagonal_stripes(28, 12, banner_stripe);
        return c;
    }

    Canvas RowFocus() const {
        Canvas c(548, 44);
        c.rounded_rect(1, 1, 546, 42, R(12), Rgba{accent.r, accent.g, accent.b, 30});
        c.rounded_border(1, 1, 546, 42, R(12), 1.5f, Rgba{accent.r, accent.g, accent.b, 150});
        return c;
    }

    // What the menu draws behind every screen without a background.png:
    // the backdrop colour, with a 2-pixel stripe every 4 rows.
    Canvas Background() const {
        Canvas c(640, 480);
        c.fill(backdrop);
        if (stripes) {
            for (int y = 2; y < 480; y += 4) c.rect(0, static_cast<float>(y), 640, 2, backdrop_stripe);
        }
        return c;
    }
};

}  // namespace

void paint_hint_box(const Theme& theme, int w, int h, Canvas& out) {
    out = Painter(theme).Card(w, h, kHintBoxMargin, h / 2.0f, false);
}

void paint_art_frame(const Theme& theme, int w, int h, Canvas& out) {
    out = Painter(theme).Card(w, h, kHintBoxMargin, 10, false);
}

bool paint_theme_image(const std::string& name, const Theme& theme, Canvas& out) {
    const Painter p(theme);
    struct Entry {
        const char* name;
        Canvas (*paint)(const Painter&);
    };
    static const Entry kEntries[] = {
        {"background", [](const Painter& q) { return q.Background(); }},
        {"tile", [](const Painter& q) { return q.Card(124, 84, 7, 14, false); }},
        {"tile_over", [](const Painter& q) { return q.Card(124, 84, 7, 14, true); }},
        {"cover_tile", [](const Painter& q) { return q.Card(80, 112, 7, 8, false); }},
        {"cover_tile_over", [](const Painter& q) { return q.Card(80, 112, 7, 8, true); }},
        {"round_button", [](const Painter& q) { return q.Round(false); }},
        {"round_button_over", [](const Painter& q) { return q.Round(true); }},
        {"pill", [](const Painter& q) { return q.Card(244, 52, 4, 26, false); }},
        {"pill_over", [](const Painter& q) { return q.Card(244, 52, 4, 26, true); }},
        {"pill_primary", [](const Painter& q) { return q.Card(244, 52, 4, 26, false, true); }},
        {"pill_primary_over", [](const Painter& q) { return q.Card(244, 52, 4, 26, true, true); }},
        {"home_button", [](const Painter& q) { return q.Card(248, 72, 8, 20, false); }},
        {"home_button_over", [](const Painter& q) { return q.Card(248, 72, 8, 20, true); }},
        {"chip_off", [](const Painter& q) { return q.Chip(false); }},
        {"chip_on", [](const Painter& q) { return q.Chip(true); }},
        {"row_focus", [](const Painter& q) { return q.RowFocus(); }},
        {"step_back", [](const Painter& q) { return q.Step(true, false); }},
        {"step_back_over", [](const Painter& q) { return q.Step(true, true); }},
        {"step_forward", [](const Painter& q) { return q.Step(false, false); }},
        {"step_forward_over", [](const Painter& q) { return q.Step(false, true); }},
        {"switch_on", [](const Painter& q) { return q.Switch(true); }},
        {"switch_off", [](const Painter& q) { return q.Switch(false); }},
        {"panel_game", [](const Painter& q) { return q.Card(572, 232, 4, 16, false); }},
        {"panel_settings", [](const Painter& q) { return q.Card(572, 276, 4, 16, false); }},
        {"bar", [](const Painter& q) { return q.Bar(); }},
        {"banner_stripes", [](const Painter& q) { return q.Stripes(); }},
        {"arrow_left", [](const Painter& q) { return q.Arrow(true, false); }},
        {"arrow_left_over", [](const Painter& q) { return q.Arrow(true, true); }},
        {"arrow_right", [](const Painter& q) { return q.Arrow(false, false); }},
        {"arrow_right_over", [](const Painter& q) { return q.Arrow(false, true); }},
        {"scroll_up", [](const Painter& q) { return q.ScrollArrow(true, false); }},
        {"scroll_up_over", [](const Painter& q) { return q.ScrollArrow(true, true); }},
        {"scroll_down", [](const Painter& q) { return q.ScrollArrow(false, false); }},
        {"scroll_down_over", [](const Painter& q) { return q.ScrollArrow(false, true); }},
        {"icon_drives", [](const Painter& q) { return q.Drives(); }},
        {"icon_gear", [](const Painter& q) { return q.Gear(); }},
        {"icon_search", [](const Painter& q) { return q.Search(); }},
        {"icon_disc", [](const Painter& q) { return q.Disc(); }},
        {"pointer1", [](const Painter& q) { return Painter::Hand(q.pointer[0]); }},
        {"pointer2", [](const Painter& q) { return Painter::Hand(q.pointer[1]); }},
        {"pointer3", [](const Painter& q) { return Painter::Hand(q.pointer[2]); }},
        {"pointer4", [](const Painter& q) { return Painter::Hand(q.pointer[3]); }},
    };
    for (const Entry& e : kEntries) {
        if (name != e.name) continue;
        out = e.paint(p);
        return true;
    }
    return false;
}

}  // namespace riftwii
