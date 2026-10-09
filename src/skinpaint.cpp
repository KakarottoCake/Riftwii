// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/skinpaint.hpp"

#include <cmath>
#include <algorithm>
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
          shelf(ToRgba(t.colors.shelf)), shelf_edge(ToRgba(t.colors.shelf_edge)), corners(t.corners),
          stripes(t.stripes), gloss(t.gloss), bump(t.bar_bump), ios6(t.home_ios6), clock(ToRgba(t.colors.clock)) {
        const ThemeColor* p[4] = {&t.colors.pointer1, &t.colors.pointer2, &t.colors.pointer3, &t.colors.pointer4};
        for (int i = 0; i < 4; ++i) pointer[i] = ToRgba(*p[i]);
    }

    Rgba card, edge, edge_strong, shadow, accent, glow, glyph, chip_on, chip_off, chip_off_edge, switch_off, bar,
        banner_stripe, backdrop, backdrop_stripe, pointer[4], shelf, shelf_edge;
    float corners;
    bool stripes, gloss, bump, ios6;
    Rgba clock;

    // A glossy theme's shine: the top half of a shape, white fading down.
    void Shine(Canvas& c, float x, float y, float w, float h, float radius) const {
        if (!gloss || w < 4 || h < 4) return;
        c.rounded_gradient(x + 1, y + 1, w - 2, h / 2, std::max(0.0f, radius - 1), Rgba{255, 255, 255, 120},
                           Rgba{255, 255, 255, 30});
    }

    // A corner radius as the theme rounds it.
    float R(float radius) const { return radius * corners; }

    // `v` lightened (k > 1) or darkened (k < 1), with alpha `a`.
    static Rgba Shade(Rgba v, float k, int a = -1) {
        const auto ch = [k](std::uint8_t q) { return static_cast<std::uint8_t>(std::min(255.0f, q * k)); };
        return Rgba{ch(v.r), ch(v.g), ch(v.b), a < 0 ? v.a : static_cast<std::uint8_t>(a)};
    }

    // A button, as the Wii Menu's: the card's colour shaded down, a gloss
    // over its top half, a fine edge with a lit line inside it; lit, the
    // accent's ring and glow. A primary one keeps a thinner ring.
    Canvas Button(int w, int h, int margin, float radius, bool over, bool primary = false) const {
        return Button(w, h, margin, radius, over, primary, card);
    }
    Canvas Button(int w, int h, int margin, float radius, bool over, bool primary, Rgba fill) const {
        Canvas c((w + 2 * margin + 3) & ~3, (h + 2 * margin + 3) & ~3);
        const float x = static_cast<float>(margin), y = static_cast<float>(margin);
        const float fw = static_cast<float>(w), fh = static_cast<float>(h);
        radius = R(radius);
        if (over) c.shadow(x - 1, y - 1, fw + 2, fh + 2, radius + 1, margin - 1.0f, glow, 2.0f);
        c.shadow(x, y + 2, fw, fh, radius, 4, shadow, 3.0f);
        c.rounded_gradient(x, y, fw, fh, radius, Shade(fill, 1.0f), Shade(fill, 0.89f));
        // The gloss: light at the top, gone by the middle.
        c.rounded_gradient(x + 3, y + 2, fw - 6, fh * 0.48f, std::max(0.0f, radius - 3), Rgba{255, 255, 255, 150},
                           Rgba{255, 255, 255, 10});
        if (over) {
            c.rounded_border(x, y, fw, fh, radius, 3.0f, accent);
        } else if (primary) {
            c.rounded_border(x, y, fw, fh, radius, 2.5f, accent);
        } else {
            c.rounded_border(x, y, fw, fh, radius, 1.5f, edge_strong);
            c.rounded_border(x + 1.5f, y + 1.5f, fw - 3, fh - 3, std::max(0.0f, radius - 1.5f), 1.0f,
                             Rgba{255, 255, 255, 170});
        }
        return c;
    }

    // The HOME Menu's buttons, as the Wii's: round ends, a pale tint of the
    // accent, a broad gloss, a darker tint for the edge.
    Canvas HomeButton(bool over) const {
        if (ios6) return IosButton(over, false);
        const auto mix = [](Rgba a, Rgba b, float k) {
            const auto m = [k](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(x + (y - x) * k); };
            return Rgba{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255};
        };
        const int w = 248, h = 72, margin = 8;
        Canvas c((w + 2 * margin + 3) & ~3, (h + 2 * margin + 3) & ~3);
        const float x = margin, y = margin, r = h / 2.0f;
        // Pale whatever the theme, as the Wii's (its bars are black on every theme).
        const Rgba fill = mix(Rgba{255, 255, 255, 255}, accent, over ? 0.32f : 0.22f);
        if (over) c.shadow(x - 1, y - 1, w + 2.0f, h + 2.0f, r + 1, margin - 1.0f, glow, 2.0f);
        c.shadow(x + 2, y + 4, static_cast<float>(w), static_cast<float>(h), r, 5, Rgba{0, 0, 0, 120}, 3.0f);
        c.rounded_gradient(x, y, static_cast<float>(w), static_cast<float>(h), r, Shade(fill, 1.04f), Shade(fill, 0.90f));
        c.rounded_gradient(x + 10, y + 4, w - 20.0f, h * 0.42f, r - 8, Rgba{255, 255, 255, 170}, Rgba{255, 255, 255, 30});
        c.rounded_border(x, y, static_cast<float>(w), static_cast<float>(h), r, over ? 3.0f : 1.5f,
                         over ? accent : mix(accent, Rgba{0, 0, 0, 255}, 0.25f));
        return c;
    }

    // An iOS 6 action sheet's button, 248 x 72 with an 8 margin: a rounded
    // body shaded down, the glossy top half lighter with a hard edge at
    // the middle, a dark rim with a light line inside it. `danger` is the
    // red one (Power off). Lit: the theme's glow and accent.
    Canvas IosButton(bool over, bool danger) const {
        const int w = 248, h = 72, margin = 8;
        Canvas c((w + 2 * margin + 3) & ~3, (h + 2 * margin + 3) & ~3);
        const float x = margin, y = margin, fw = w, fh = h, r = R(11);
        const Rgba top = danger ? Rgba{232, 98, 90, 255} : Shade(card, 1.0f);
        const Rgba bottom = danger ? Rgba{168, 24, 30, 255} : Shade(card, 0.80f);
        if (over) c.shadow(x - 1, y - 1, fw + 2, fh + 2, r + 1, margin - 1.0f, glow, 2.0f);
        c.shadow(x, y + 2, fw, fh, r, 4, Rgba{0, 0, 0, 130}, 3.0f);
        c.rounded_gradient(x, y, fw, fh, r, top, bottom);
        c.rounded_gradient(x + 1, y + 1, fw - 2, fh * 0.5f, std::max(0.0f, r - 1), Rgba{255, 255, 255, 120},
                           Rgba{255, 255, 255, 50});
        c.rounded_border(x + 1, y + 1, fw - 2, fh - 2, std::max(0.0f, r - 1), 1.0f, Rgba{255, 255, 255, 90});
        c.rounded_border(x, y, fw, fh, r, over ? 2.5f : 1.2f, over ? accent : Rgba{0, 0, 0, 150});
        return c;
    }

    // An iOS 6 bar button ("Done"), 120 x 40 with a 4 margin: the accent
    // shaded down, glossy, a dark rim; lit, lighter with the glow.
    Canvas IosBarButton(bool over) const {
        Canvas c(128, 48);
        const float x = 4, y = 4, w = 120, h = 40, r = 6;
        if (over) c.shadow(x - 1, y - 1, w + 2, h + 2, r + 1, 3, glow, 2.0f);
        c.shadow(x, y + 1, w, h, r, 2, Rgba{255, 255, 255, 60}, 3.0f);
        c.rounded_gradient(x, y, w, h, r, Shade(accent, over ? 1.6f : 1.35f), Shade(accent, over ? 1.05f : 0.85f));
        c.rounded_gradient(x + 1, y + 1, w - 2, h * 0.5f, r - 1, Rgba{255, 255, 255, 80}, Rgba{255, 255, 255, 25});
        c.rounded_border(x, y, w, h, r, 1.2f, Rgba{0, 0, 0, 170});
        return c;
    }

    // A panel (popups, Settings, a game's page): a wide soft shadow, the
    // card shaded a touch down, a fine edge and a lit line inside it.
    Canvas Sheet(int w, int h, int margin, float radius) const {
        Canvas c((w + 2 * margin + 3) & ~3, (h + 2 * margin + 3) & ~3);
        const float x = static_cast<float>(margin), y = static_cast<float>(margin);
        const float fw = static_cast<float>(w), fh = static_cast<float>(h);
        radius = R(radius);
        c.shadow(x, y + 2, fw, fh, radius, static_cast<float>(margin), shadow, 3.0f);
        c.rounded_gradient(x, y, fw, fh, radius, Shade(card, 1.0f), Shade(card, 0.965f));
        c.rounded_border(x, y, fw, fh, radius, 1.5f, edge);
        c.rounded_border(x + 1.5f, y + 1.5f, fw - 3, fh - 3, std::max(0.0f, radius - 1.5f), 1.0f,
                         Rgba{255, 255, 255, 160});
        return c;
    }

    // A channel's frame, as the Wii Menu's: a shadow, the card, a fine grey
    // edge with a light line inside it; lit, the accent's border and glow.
    Canvas Tile(int w, int h, int margin, float radius, bool over) const {
        Canvas c((w + 2 * margin + 3) & ~3, (h + 2 * margin + 3) & ~3);
        const float x = static_cast<float>(margin), y = static_cast<float>(margin);
        radius = R(radius);
        if (over) c.shadow(x - 1, y - 1, w + 2.0f, h + 2.0f, radius + 1, margin - 1.0f, glow, 2.0f);
        c.shadow(x, y + 2, static_cast<float>(w), static_cast<float>(h), radius, 3, shadow, 3.0f);
        c.rounded_rect(x, y, static_cast<float>(w), static_cast<float>(h), radius, card);
        Shine(c, x, y, static_cast<float>(w), static_cast<float>(h), radius);
        if (over) {
            c.rounded_border(x, y, static_cast<float>(w), static_cast<float>(h), radius, 3.0f, accent);
        } else {
            c.rounded_border(x, y, static_cast<float>(w), static_cast<float>(h), radius, 2.0f, edge);
            c.rounded_border(x + 2, y + 2, w - 4.0f, h - 4.0f, std::max(0.0f, radius - 2), 1.0f, Rgba{255, 255, 255, 140});
        }
        return c;
    }

    // An empty channel, as the Wii Menu shows one: sunk in, the backdrop's
    // colour a shade darker, fine lines across it.
    Canvas TileEmpty(int w, int h, int margin, float radius) const {
        Canvas c((w + 2 * margin + 3) & ~3, (h + 2 * margin + 3) & ~3);
        const float x = static_cast<float>(margin), y = static_cast<float>(margin);
        radius = R(radius);
        const auto shade = [](Rgba v, float k, std::uint8_t a) {
            const auto ch = [k](std::uint8_t q) { return static_cast<std::uint8_t>(std::min(255.0f, q * k)); };
            return Rgba{ch(v.r), ch(v.g), ch(v.b), a};
        };
        c.rounded_gradient(x, y, static_cast<float>(w), static_cast<float>(h), radius, shade(backdrop, 0.97f, 255),
                           shade(backdrop, 1.02f, 255));
        for (int yy = margin + 3; yy < margin + h - 3; yy += 3)
            c.rect(x + 3, static_cast<float>(yy), w - 6.0f, 1, shade(backdrop, 0.90f, 70));
        c.rounded_border(x, y, static_cast<float>(w), static_cast<float>(h), radius, 1.5f, shade(edge, 1.0f, 200));
        c.rounded_border(x + 1.5f, y + 1.5f, w - 3.0f, h - 3.0f, std::max(0.0f, radius - 1.5f), 1.5f, Rgba{0, 0, 0, 18});
        return c;
    }

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
        Shine(c, x, y, static_cast<float>(w), static_cast<float>(h), radius);
        if (over || primary) {
            c.rounded_border(x, y, static_cast<float>(w), static_cast<float>(h), radius, primary ? 3.0f : 2.5f, accent);
        } else {
            c.rounded_border(x, y, static_cast<float>(w), static_cast<float>(h), radius, 1.5f, edge);
        }
        return c;
    }

    Canvas Round(bool over) const {
        Canvas c(80, 80);
        const auto shade = [](Rgba v, float k) {
            const auto ch = [k](std::uint8_t x) { return static_cast<std::uint8_t>(std::min(255.0f, x * k)); };
            return Rgba{ch(v.r), ch(v.g), ch(v.b), v.a};
        };
        if (over) c.circle(40, 40, 39, glow);
        c.circle(40, 41.5f, 38, shadow);
        // The rim: the card's colour shaded top to bottom, a fine edge round it.
        c.rounded_gradient(3, 3, 74, 74, 37, shade(card, 1.0f), shade(card, 0.86f));
        c.ring(40, 40, 37, 1.2f, edge_strong);
        // The accent's ring, and the face inside it, light at the top.
        c.ring(40, 40, 31.5f, over ? 4.0f : 3.2f, accent);
        c.rounded_gradient(11.5f, 11.5f, 57, 57, 28.5f, shade(card, 1.04f), shade(card, 0.90f));
        c.rounded_gradient(15, 13, 50, 24, 12, Rgba{255, 255, 255, 110}, Rgba{255, 255, 255, 0});
        return c;
    }

    Canvas Chip(bool on) const {
        Canvas c(212, 36);
        const Rgba fill = on ? chip_on : chip_off;
        c.rounded_gradient(2, 3, 208, 30, R(15), Shade(fill, 1.04f), Shade(fill, 0.96f));
        Shine(c, 2, 3, 208, 30, R(15));
        c.rounded_border(2, 3, 208, 30, R(15), 2, on ? accent : chip_off_edge);
        return c;
    }

    Canvas Arrow(bool left, bool over) const {
        Canvas c(48, 48);
        const float s = left ? -1.0f : 1.0f;
        const float tipX = 24 + 11 * s, backX = 24 - 9 * s;
        // Filled by lines from the back edge to the tip, then edged.
        const Rgba fill = over ? Rgba{accent.r, accent.g, accent.b, 150} : Rgba{255, 255, 255, 220};
        for (float yy = 9.5f; yy <= 38.5f; yy += 1.0f) c.line(backX, yy, tipX, 24, 1.6f, fill);
        if (over) {
            c.line(backX, 7, tipX, 24, 7.0f, glow);
            c.line(tipX, 24, backX, 41, 7.0f, glow);
            c.line(backX, 7, backX, 41, 7.0f, glow);
        }
        c.line(backX, 8, tipX, 24, 3.2f, accent);
        c.line(tipX, 24, backX, 40, 3.2f, accent);
        c.line(backX, 8, backX, 40, 3.2f, accent);
        return c;
    }

    // A list's scroll arrow: 34 across, in a 44 canvas, pointing up or down.
    Canvas ScrollArrow(bool up, bool over) const {
        Canvas c(44, 44);
        if (over) c.circle(21, 21, 20.5f, glow);
        c.circle(21, 22.5f, 17.5f, shadow);
        c.rounded_gradient(4, 4, 34, 34, 17, Shade(over ? chip_on : card, 1.0f), Shade(over ? chip_on : card, 0.89f));
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
        c.rounded_gradient(4, 4, 34, 34, 17, Shade(over ? chip_on : card, 1.0f), Shade(over ? chip_on : card, 0.89f));
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

    // A five-pointed star: its outline, or filled gold for a favourite.
    Canvas Star(bool on) const {
        Canvas c(28, 28);
        std::vector<std::pair<float, float>> points;
        for (int i = 0; i < 10; ++i) {
            const float a = -3.14159265f / 2 + i * 3.14159265f / 5;
            const float r = i % 2 == 0 ? 12.0f : 5.0f;
            points.emplace_back(14 + r * std::cos(a), 15 + r * std::sin(a));
        }
        if (on) c.polygon(points, rgba(0xF2B705));
        for (std::size_t i = 0; i < points.size(); ++i) {
            const auto& a = points[i];
            const auto& b = points[(i + 1) % points.size()];
            c.line(a.first, a.second, b.first, b.second, on ? 1.6f : 2.2f, glyph);
        }
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

    // `w` across. The Wii Menu's: high at the sides, sinking in the middle
    // (58% of the width) where the clock sits; a body shaded down from the
    // line, faint lines across it, the accent's line along the top with a
    // light edge under it. A bump theme's: rising in the middle instead.
    Canvas Bar(int w = 640) const {
        if (bump) return BumpBar(w);
        Canvas c(w, 124);
        std::vector<float> top(static_cast<std::size_t>(w));
        const float dip = w * 0.58f, dipLeft = w / 2.0f - dip / 2.0f;
        for (int x = 0; x < w; ++x) {
            const float t = (x + 0.5f - dipLeft) / dip;
            // Sloping in over a fifth of it each side, flat between (the clock's room).
            const auto ease = [](float v) { v = v < 0 ? 0 : v > 1 ? 1 : v; return v * v * (3 - 2 * v); };
            const float d = (t > 0.0f && t < 1.0f) ? std::min(ease(t / 0.3f), ease((1.0f - t) / 0.3f)) : 0.0f;
            top[x] = 11.0f + 51.0f * d;
        }
        std::vector<float> shade(top);
        for (float& v : shade) v -= 4.0f;
        c.area_below(shade, rgba(0x000000, 22), 6.0f);
        // The body: the bar's colour at the line, a little darker lower down.
        c.area_below(top, bar);
        for (int y = 0; y < 124; ++y) {
            const float k = 1.0f - 0.10f * (y / 123.0f);
            const auto ch = [k](std::uint8_t v) { return static_cast<std::uint8_t>(v * k); };
            const Rgba row = {ch(bar.r), ch(bar.g), ch(bar.b), 255};
            for (int x = 0; x < w; ++x) {
                if (y < top[x] + 1.0f) continue;
                Rgba p = row;
                if (stripes && (y & 3) >= 2) {  // the Wii's faint lines
                    p.r = static_cast<std::uint8_t>(p.r * 0.97f);
                    p.g = static_cast<std::uint8_t>(p.g * 0.97f);
                    p.b = static_cast<std::uint8_t>(p.b * 0.97f);
                }
                c.put(x, y, p);
            }
        }
        // A light edge under the line, then the line.
        std::vector<float> under(top);
        for (float& v : under) v += 3.0f;
        c.curve(under, 2.0f, rgba(0xFFFFFF, 150));
        c.curve(top, 3.5f, accent);
        return c;
    }

    Canvas BumpBar(int w) const {
        Canvas c(w, 124);
        std::vector<float> top(static_cast<std::size_t>(w));
        const float bumpLeft = w / 2.0f - 144.0f;
        for (int x = 0; x < w; ++x) {
            const float t = (x + 0.5f - bumpLeft) / 288.0f;
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

    // The clock's figures, as the Wii Menu's: seven segments each, 0 to 9
    // then the colon, in 28 x 44 cells side by side.
    Canvas ClockDigits() const {
        Canvas c(308, 44);
        static const unsigned char kSegments[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
        // a b c d e f g: top, top right, bottom right, bottom, bottom left, top left, middle.
        struct Seg { float x0, y0, x1, y1; };
        const Seg segs[7] = {{8, 4, 20, 4},  {24, 8, 24, 18}, {24, 26, 24, 36}, {8, 40, 20, 40},
                             {4, 26, 4, 36}, {4, 8, 4, 18},   {8, 22, 20, 22}};
        const Rgba shade = Rgba{0, 0, 0, 28}, light = Rgba{255, 255, 255, 110};
        for (int d = 0; d < 10; ++d) {
            const float ox = d * 28.0f;
            for (int s = 0; s < 7; ++s) {
                if (!(kSegments[d] & (1 << s))) continue;
                const Seg& g = segs[s];
                c.line(ox + g.x0 + 0.5f, g.y0 + 1.5f, ox + g.x1 + 0.5f, g.y1 + 1.5f, 5.5f, shade);
            }
            for (int s = 0; s < 7; ++s) {
                if (!(kSegments[d] & (1 << s))) continue;
                const Seg& g = segs[s];
                c.line(ox + g.x0, g.y0, ox + g.x1, g.y1, 5.0f, clock);
                // A lit edge along each segment, as the Wii's are.
                c.line(ox + g.x0, g.y0 - 0.8f, ox + g.x1, g.y1 - 0.8f, 1.2f, light);
            }
        }
        const float cx = 10 * 28.0f + 14.0f;
        c.circle(cx + 0.5f, 16.5f, 3.2f, shade);
        c.circle(cx + 0.5f, 30.5f, 3.2f, shade);
        c.circle(cx, 15, 3.0f, clock);
        c.circle(cx, 29, 3.0f, clock);
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

    // Home's shelf: its top (rows 0-47, the far edge first) and its front
    // edge (48-63), wood grain running along it; repeats across.
    Canvas Shelf() const {
        Canvas c(256, 64);
        for (int y = 0; y < 64; ++y) {
            const bool edgeRow = y >= 48;
            const Rgba base = edgeRow ? shelf_edge : shelf;
            for (int x = 0; x < 256; ++x) {
                // Waving grain; its period divides 256, so the picture tiles.
                const float fx = x * (2.0f * 3.14159265f / 256.0f);
                const float wave = std::sin(y * 1.3f + 1.8f * std::sin(fx * 2.0f + y * 0.15f) + 0.9f * std::sin(fx * 5.0f));
                float k = 0.93f + 0.07f * wave;
                if (!edgeRow && y < 3) k *= 0.85f + 0.05f * y;  // the far edge in shade
                if (y == 48) k = 1.18f;                         // the edge's lit lip
                const auto ch = [&](std::uint8_t v) {
                    const float f = v * k;
                    return static_cast<std::uint8_t>(f > 255.0f ? 255.0f : f);
                };
                c.put(x, y, Rgba{ch(base.r), ch(base.g), ch(base.b), 255});
            }
        }
        return c;
    }

    // What the menu draws behind every screen without a background.png:
    // the backdrop colour, with a 2-pixel stripe every 4 rows.
    Canvas Background(int w = 640) const {
        Canvas c(w, 480);
        c.fill(backdrop);
        if (stripes) {
            for (int y = 2; y < 480; y += 4) c.rect(0, static_cast<float>(y), static_cast<float>(w), 2, backdrop_stripe);
        }
        return c;
    }
};

}  // namespace

void paint_hint_box(const Theme& theme, int w, int h, Canvas& out) {
    out = Painter(theme).Card(w, h, kHintBoxMargin, h / 2.0f, false);
}

void paint_clock_digits(const Theme& theme, Canvas& out) { out = Painter(theme).ClockDigits(); }

void paint_key(const Theme& theme, int kind, Canvas& out) {
    const Painter p(theme);
    const bool primary = kind >= 2, over = (kind & 1) != 0;
    out = p.Button(32, 32, 4, 7, over, false, primary ? p.accent : p.card);
}

void paint_card9(const Theme& theme, Canvas& out) { out = Painter(theme).Sheet(48, 48, 8, 14); }

void paint_home_danger(const Theme& theme, bool over, Canvas& out) { out = Painter(theme).IosButton(over, true); }

void paint_ios_bar_button(const Theme& theme, bool over, Canvas& out) { out = Painter(theme).IosBarButton(over); }

void paint_linen(Canvas& out) {
    // Dark linen, as iOS 6 laid behind its Notification Center: threads
    // across and down, each a shade apart, and a fine weave. Every row's
    // and column's shade comes round again after 64, so the copies join.
    const auto hash = [](std::uint32_t v) {
        v ^= v >> 16;
        v *= 0x7feb352du;
        v ^= v >> 15;
        v *= 0x846ca68bu;
        v ^= v >> 16;
        return v;
    };
    const auto unit = [&](std::uint32_t v) { return static_cast<float>(hash(v) % 1000u) / 1000.0f - 0.5f; };
    out = Canvas(64, 64);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            float v = 48 + unit(static_cast<std::uint32_t>(y * 7 + 1)) * 16 + unit(static_cast<std::uint32_t>(x * 13 + 500)) * 10 +
                      unit(static_cast<std::uint32_t>(x * 131 + y * 977 + 3)) * 8 + (((x + y) & 1) ? 2.0f : -2.0f);
            v = std::max(0.0f, std::min(255.0f, v));
            const auto g = static_cast<std::uint8_t>(v);
            out.put(x, y, Rgba{g, g, static_cast<std::uint8_t>(std::min(255, g + 3)), 238});
        }
    }
}

void paint_capsule9(Canvas& out) {
    out = Canvas(48, 48);
    out.rounded_rect(4, 4, 40, 40, 18, rgba(0x000000));
    out.rounded_border(4, 4, 40, 40, 18, 2.0f, rgba(0xC8C8C8));
}

void paint_notice_icon(const Theme& theme, bool error, Canvas& out) {
    const Painter p(theme);
    const Rgba ring = error ? ToRgba(theme.colors.warn) : p.accent, ink = rgba(0xFFFFFF);
    out = Canvas(28, 28);
    out.circle(14, 15, 13, Rgba{0, 0, 0, 40});
    out.circle(14, 14, 13, ring);
    out.rounded_gradient(4, 2, 20, 11, 5.5f, Rgba{255, 255, 255, 90}, Rgba{255, 255, 255, 0});
    if (error) {
        out.line(14, 7, 14, 16, 3.4f, ink);
        out.circle(14, 21, 2.0f, ink);
    } else {
        out.circle(14, 7.5f, 2.0f, ink);
        out.line(14, 12, 14, 21, 3.4f, ink);
    }
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
        {"tile", [](const Painter& q) { return q.Tile(124, 84, 7, 14, false); }},
        {"tile_over", [](const Painter& q) { return q.Tile(124, 84, 7, 14, true); }},
        {"tile_empty", [](const Painter& q) { return q.TileEmpty(124, 84, 7, 14); }},
        {"cover_tile", [](const Painter& q) { return q.Card(80, 112, 7, 8, false); }},
        {"cover_tile_over", [](const Painter& q) { return q.Card(80, 112, 7, 8, true); }},
        {"round_button", [](const Painter& q) { return q.Round(false); }},
        {"round_button_over", [](const Painter& q) { return q.Round(true); }},
        {"pill", [](const Painter& q) { return q.Button(244, 52, 4, 26, false); }},
        {"pill_over", [](const Painter& q) { return q.Button(244, 52, 4, 26, true); }},
        {"pill_primary", [](const Painter& q) { return q.Button(244, 52, 4, 26, false, true); }},
        {"pill_primary_over", [](const Painter& q) { return q.Button(244, 52, 4, 26, true, true); }},
        {"home_button", [](const Painter& q) { return q.HomeButton(false); }},
        {"home_button_over", [](const Painter& q) { return q.HomeButton(true); }},
        {"chip_off", [](const Painter& q) { return q.Chip(false); }},
        {"chip_on", [](const Painter& q) { return q.Chip(true); }},
        {"row_focus", [](const Painter& q) { return q.RowFocus(); }},
        {"step_back", [](const Painter& q) { return q.Step(true, false); }},
        {"step_back_over", [](const Painter& q) { return q.Step(true, true); }},
        {"step_forward", [](const Painter& q) { return q.Step(false, false); }},
        {"step_forward_over", [](const Painter& q) { return q.Step(false, true); }},
        {"switch_on", [](const Painter& q) { return q.Switch(true); }},
        {"switch_off", [](const Painter& q) { return q.Switch(false); }},
        {"panel_game", [](const Painter& q) { return q.Sheet(572, 232, 4, 18); }},
        {"panel_settings", [](const Painter& q) { return q.Sheet(572, 276, 4, 18); }},
        {"bar", [](const Painter& q) { return q.Bar(); }},
        {"background_wide", [](const Painter& q) { return q.Background(856); }},
        {"bar_wide", [](const Painter& q) { return q.Bar(856); }},
        {"background_shelf", [](const Painter& q) { return q.Background(640); }},
        {"background_shelf_wide", [](const Painter& q) { return q.Background(856); }},
        {"background_plain", [](const Painter& q) { return q.Background(640); }},
        {"background_plain_wide", [](const Painter& q) { return q.Background(856); }},
        {"background_channels", [](const Painter& q) { return q.Background(640); }},
        {"background_channels_wide", [](const Painter& q) { return q.Background(856); }},
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
        {"icon_star", [](const Painter& q) { return q.Star(false); }},
        {"icon_star_on", [](const Painter& q) { return q.Star(true); }},
        {"icon_disc", [](const Painter& q) { return q.Disc(); }},
        {"pointer1", [](const Painter& q) { return Painter::Hand(q.pointer[0]); }},
        {"pointer2", [](const Painter& q) { return Painter::Hand(q.pointer[1]); }},
        {"pointer3", [](const Painter& q) { return Painter::Hand(q.pointer[2]); }},
        {"pointer4", [](const Painter& q) { return Painter::Hand(q.pointer[3]); }},
        {"shelf", [](const Painter& q) { return q.Shelf(); }},
    };
    for (const Entry& e : kEntries) {
        if (name != e.name) continue;
        out = e.paint(p);
        return true;
    }
    return false;
}

}  // namespace riftwii
