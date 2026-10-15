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
          stripes(t.stripes), grain(t.shelf_grain), dials(t.clock_dials), gloss(t.gloss), bump(t.bar_bump), ios6(t.home_ios6), clock(ToRgba(t.colors.clock)) {
        const ThemeColor* p[4] = {&t.colors.pointer1, &t.colors.pointer2, &t.colors.pointer3, &t.colors.pointer4};
        for (int i = 0; i < 4; ++i) pointer[i] = ToRgba(*p[i]);
    }

    Rgba card, edge, edge_strong, shadow, accent, glow, glyph, chip_on, chip_off, chip_off_edge, switch_off, bar,
        banner_stripe, backdrop, backdrop_stripe, pointer[4], shelf, shelf_edge;
    float corners;
    bool stripes, grain, dials, gloss, bump, ios6;
    Rgba clock;

    // A glossy theme's shine: the top half of a shape, white fading down.
    void Shine(Canvas& c, float x, float y, float w, float h, float radius) const {
        if (!gloss || w < 4 || h < 4) return;
        c.rounded_gradient(x + 1, y + 1, w - 2, h / 2, std::max(0.0f, radius - 1), Rgba{255, 255, 255, 120},
                           Rgba{255, 255, 255, 30});
    }

    // A corner radius as the theme rounds it.
    float R(float radius) const { return radius * corners; }

    // `a` taken `k` of the way to `b`, opaque.
    static Rgba Mix(Rgba a, Rgba b, float k) {
        const auto m = [k](std::uint8_t x, std::uint8_t y) { return static_cast<std::uint8_t>(x + (y - x) * k); };
        return Rgba{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255};
    }

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
        // The gloss: an oval of light over the face's top, inside the face,
        // brightest at the top and gone before the middle, soft all round.
        for (int y = 11; y < 42; ++y)
            for (int x = 11; x < 69; ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float inFace = std::clamp(27.0f - std::hypot(px - 40, py - 40), 0.0f, 1.5f) / 1.5f;
                const float ex = (px - 40) / 25.0f, ey = (py - 26) / 14.0f;
                const float inOval = std::clamp((1.0f - (ex * ex + ey * ey)) * 3.0f, 0.0f, 1.0f);
                const float down = std::clamp(1.0f - (py - 12) / 28.0f, 0.0f, 1.0f);
                const float a = 0.55f * inFace * inOval * down * down;
                if (a <= 0.0f) continue;
                const Rgba d = c.at(x, y);
                const auto mixc = [a](std::uint8_t v) { return static_cast<std::uint8_t>(v + (255 - v) * a); };
                c.put(x, y, Rgba{mixc(d.r), mixc(d.g), mixc(d.b), d.a});
            }
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

    // A disc seen from above, `size` across: silver with a faint rainbow
    // that turns with the angle, two bright sweeps where the light catches
    // it, fine tracks, a darker rim, the clear hub and its hole.
    static Canvas GlossyDisc(int size) {
        Canvas c(size, size);
        const float mid = (size - 1) / 2.0f, rim = size / 2.0f - 0.8f;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const float dx = x - mid, dy = y - mid, r = std::sqrt(dx * dx + dy * dy);
                const float t = r / rim;
                // Coverage at the outer edge and at the hole's.
                const float cover = std::clamp(rim + 0.5f - r, 0.0f, 1.0f) * std::clamp(r - rim * 0.075f + 0.5f, 0.0f, 1.0f);
                if (cover <= 0.0f) continue;
                float rr, gg, bb, a = 255;
                const float angle = std::atan2(dy, dx);
                if (t < 0.31f) {
                    // The clear hub: pale, see-through, a ring round the hole.
                    rr = 236, gg = 240, bb = 246;
                    a = 150;
                    if (t > 0.10f && t < 0.135f) rr = gg = bb = 190, a = 210;
                    if (t > 0.285f) rr = gg = 205, bb = 214, a = 230;
                } else {
                    float base = 196 + 34 * (1 - t) + 4 * std::sin(t * 140.0f);
                    const float sheen = std::pow(std::fabs(std::cos(angle + 0.785f)), 12.0f);
                    // The rainbow: a hue from the angle (twice round) and the radius.
                    const float h = std::fmod(angle / 3.14159265f + t * 0.6f + 2.0f, 1.0f) * 6.0f;
                    const float f = h - std::floor(h);
                    float hr = 0, hg = 0, hb = 0;
                    switch (static_cast<int>(h) % 6) {
                        case 0: hr = 1, hg = f, hb = 0; break;
                        case 1: hr = 1 - f, hg = 1, hb = 0; break;
                        case 2: hr = 0, hg = 1, hb = f; break;
                        case 3: hr = 0, hg = 1 - f, hb = 1; break;
                        case 4: hr = f, hg = 0, hb = 1; break;
                        default: hr = 1, hg = 0, hb = 1 - f; break;
                    }
                    const float tint = 0.16f + 0.30f * sheen;
                    base += 50 * sheen;
                    rr = base * (1 - tint) + 255 * hr * tint;
                    gg = base * (1 - tint) + 255 * hg * tint;
                    bb = base * (1 - tint) + 255 * hb * tint + 6;
                    if (t < 0.345f) rr = gg = bb = 248;            // the hub's edge, lit
                    if (t > 0.965f) rr *= 0.72f, gg *= 0.72f, bb *= 0.76f;  // the rim
                }
                c.put(x, y, Rgba{static_cast<std::uint8_t>(std::clamp(rr, 0.0f, 255.0f)),
                                 static_cast<std::uint8_t>(std::clamp(gg, 0.0f, 255.0f)),
                                 static_cast<std::uint8_t>(std::clamp(bb, 0.0f, 255.0f)),
                                 static_cast<std::uint8_t>(a * cover)});
            }
        return c;
    }

    Canvas Disc() const { return GlossyDisc(40); }

    // The disc drive's box: a pale blue case like a drive's front, the
    // slot across its top, the disc behind the clear sleeve, the sleeve
    // catching the light from the top left.
    Canvas DiscCase() const {
        Canvas c(128, 176);
        c.rounded_gradient(0, 0, 128, 176, 7, rgba(0xEEF4FB), rgba(0xB7CCE6));
        c.rounded_border(0, 0, 128, 176, 7, 2, rgba(0x8EA8C8));
        // The drive: a blue band with its slot.
        c.rounded_gradient(7, 8, 114, 30, 5, rgba(0x4C8FE0), rgba(0x2C63B0));
        c.rounded_rect(18, 21, 92, 5, 2.5f, rgba(0x163A6E));
        c.rect(22, 22, 84, 1, rgba(0xFFFFFF, 70));
        // The disc, half out of the slot's way.
        const Canvas disc = GlossyDisc(108);
        const int ox = 10, oy = 56;
        for (int y = 0; y < disc.height(); ++y)
            for (int x = 0; x < disc.width(); ++x) {
                const Rgba s = disc.at(x, y);
                if (s.a == 0) continue;
                const Rgba d = c.at(ox + x, oy + y);
                const int a = s.a;
                c.put(ox + x, oy + y,
                      Rgba{static_cast<std::uint8_t>((s.r * a + d.r * (255 - a)) / 255),
                           static_cast<std::uint8_t>((s.g * a + d.g * (255 - a)) / 255),
                           static_cast<std::uint8_t>((s.b * a + d.b * (255 - a)) / 255), d.a});
            }
        // The sleeve's sheen: a soft band from the top left.
        c.polygon({{2, 40}, {64, 2}, {104, 2}, {2, 74}}, rgba(0xFFFFFF, 34));
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
            // A little right of and below the middle: so it sits in the
            // middle of a round button (a tester saw it high and left).
            points.emplace_back(15 + r * std::cos(a), 16.5f + r * std::sin(a));
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

    // A tile of the game page and Settings, 64x64 to be nine-sliced
    // (corner 24): the channel frame's card; lit, the accent's ring and
    // glow. A primary one (Play) is tinted with the accent and keeps its
    // ring when it is not lit, so the eye finds it first.
    Canvas TileNine(bool over, bool primary) const {
        if (!primary) return Tile(48, 48, 8, 16, over);
        Canvas c(64, 64);
        const float x = 8, y = 8, w = 48, h = 48, radius = R(16);
        if (over) c.shadow(x - 1, y - 1, w + 2, h + 2, radius + 1, 7.0f, glow, 2.0f);
        c.shadow(x, y + 2, w, h, radius, 3, shadow, 3.0f);
        c.rounded_gradient(x, y, w, h, radius, Mix(card, accent, 0.16f), Mix(card, accent, 0.26f));
        Shine(c, x, y, w, h, radius);
        c.rounded_border(x, y, w, h, radius, over ? 3.0f : 2.5f, accent);
        return c;
    }

    // The round badge a button's name sits on in the prompts along the
    // bottom of a screen (A, B, +, 1, 2), as consoles draw them.
    Canvas PromptButton() const {
        Canvas c(24, 24);
        c.circle(12, 12, 11.0f, glyph);
        return c;
    }

    // The tiles' icons, 28x28 in the glyph colour, as the gear and the
    // star: Play, Mods, Cheats, Saves, About, Help, Picture, Controllers,
    // Online, Home and Look and sound.
    Canvas Play() const {
        Canvas c(28, 28);
        c.polygon({{9, 5}, {23, 14}, {9, 23}}, glyph);
        return c;
    }
    Canvas Layers() const {
        Canvas c(28, 28);
        const auto diamond = [&](float y, bool full) {
            c.line(4, y, 14, y + 5, 2.2f, glyph);
            c.line(14, y + 5, 24, y, 2.2f, glyph);
            if (!full) return;
            c.line(4, y, 14, y - 5, 2.2f, glyph);
            c.line(14, y - 5, 24, y, 2.2f, glyph);
        };
        diamond(9, true);
        diamond(14, false);
        diamond(19, false);
        return c;
    }
    Canvas Bolt() const {
        Canvas c(28, 28);
        c.polygon({{17, 2}, {6, 16}, {13, 16}, {10, 26}, {22, 11}, {15, 11}}, glyph);
        return c;
    }
    Canvas Floppy() const {
        Canvas c(28, 28);
        c.rounded_border(5, 5, 18, 18, 2.5f, 2.0f, glyph);
        c.rect(9, 5, 9, 6, glyph);
        c.rounded_border(9, 15, 10, 8, 1.0f, 1.8f, glyph);
        return c;
    }
    Canvas Info() const {
        Canvas c(28, 28);
        c.ring(14, 14, 10.5f, 2.0f, glyph);
        c.circle(14, 9, 1.7f, glyph);
        c.line(14, 13, 14, 20, 2.6f, glyph);
        return c;
    }
    Canvas Help() const {
        Canvas c(28, 28);
        c.ring(14, 14, 10.5f, 2.0f, glyph);
        const float hook[][2] = {{10.5f, 11}, {11.3f, 8.6f}, {14, 7.4f}, {16.7f, 8.4f}, {17.6f, 10.6f},
                                 {16.4f, 12.8f}, {14, 14.2f}, {14, 16.6f}};
        for (int i = 0; i + 1 < 8; ++i) c.line(hook[i][0], hook[i][1], hook[i + 1][0], hook[i + 1][1], 2.2f, glyph);
        c.circle(14, 20.4f, 1.6f, glyph);
        return c;
    }
    Canvas Tv() const {
        Canvas c(28, 28);
        c.rounded_border(3, 5, 22, 15, 2.5f, 2.0f, glyph);
        c.line(14, 20, 14, 23, 2.0f, glyph);
        c.line(9, 23.5f, 19, 23.5f, 2.2f, glyph);
        return c;
    }
    Canvas Pad() const {
        Canvas c(28, 28);
        c.rounded_border(2, 8, 24, 13, 6.0f, 2.0f, glyph);
        c.line(8, 11.5f, 8, 17.5f, 2.0f, glyph);
        c.line(5, 14.5f, 11, 14.5f, 2.0f, glyph);
        c.circle(18.5f, 12.8f, 1.6f, glyph);
        c.circle(21.5f, 16.2f, 1.6f, glyph);
        return c;
    }
    Canvas Globe() const {
        Canvas c(28, 28);
        c.ring(14, 14, 10.5f, 2.0f, glyph);
        // The meridian: an ellipse a third as wide as it is tall.
        for (int i = 0; i < 24; ++i) {
            const float a0 = i * 3.14159265f / 12, a1 = (i + 1) * 3.14159265f / 12;
            c.line(14 + 4.5f * std::cos(a0), 14 + 10.5f * std::sin(a0), 14 + 4.5f * std::cos(a1),
                   14 + 10.5f * std::sin(a1), 1.6f, glyph);
        }
        c.line(3.5f, 14, 24.5f, 14, 1.6f, glyph);
        c.line(6, 8.5f, 22, 8.5f, 1.4f, glyph);
        c.line(6, 19.5f, 22, 19.5f, 1.4f, glyph);
        return c;
    }
    Canvas House() const {
        Canvas c(28, 28);
        c.line(3, 14, 14, 4.5f, 2.4f, glyph);
        c.line(14, 4.5f, 25, 14, 2.4f, glyph);
        c.rounded_border(7, 12.5f, 14, 12, 1.5f, 2.0f, glyph);
        c.rect(12, 17, 4, 7.5f, glyph);
        return c;
    }
    Canvas Power() const {
        Canvas c(28, 28);
        // The ring, open at the top where the line stands.
        const float gap = 0.55f, sweep = 2 * 3.14159265f - 2 * gap;
        for (int i = 0; i < 30; ++i) {
            const float a0 = -3.14159265f / 2 + gap + i * sweep / 30;
            const float a1 = -3.14159265f / 2 + gap + (i + 1) * sweep / 30;
            c.line(14 + 9.5f * std::cos(a0), 15 + 9.5f * std::sin(a0), 14 + 9.5f * std::cos(a1), 15 + 9.5f * std::sin(a1),
                   2.4f, glyph);
        }
        c.line(14, 3.5f, 14, 13, 2.6f, glyph);
        return c;
    }
    Canvas Note() const {
        Canvas c(28, 28);
        c.circle(10, 20.5f, 3.6f, glyph);
        c.line(13.2f, 20.5f, 13.2f, 4.5f, 2.2f, glyph);
        c.polygon({{13.2f, 4}, {21.5f, 8}, {21.5f, 12}, {13.2f, 8.5f}}, glyph);
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
    // edge (48-63); repeats across. With grain, wood; without, smooth
    // plastic like the Wii's own: the top brightening towards the front,
    // a lit lip, the edge with a thin accent line along it.
    Canvas Shelf() const {
        Canvas c(256, 64);
        if (!grain) {
            for (int y = 0; y < 64; ++y) {
                Rgba px;
                if (y < 48) {
                    const float k = 0.90f + 0.10f * (y / 47.0f);
                    px = Rgba{static_cast<std::uint8_t>(shelf.r * k), static_cast<std::uint8_t>(shelf.g * k),
                              static_cast<std::uint8_t>(shelf.b * k), 255};
                } else if (y == 48) {
                    px = Rgba{255, 255, 255, 255};
                } else if (y >= 52 && y <= 53) {
                    px = Rgba{accent.r, accent.g, accent.b, 255};
                } else {
                    const float k = 1.0f - 0.10f * ((y - 49) / 14.0f);
                    px = Rgba{static_cast<std::uint8_t>(shelf_edge.r * k), static_cast<std::uint8_t>(shelf_edge.g * k),
                              static_cast<std::uint8_t>(shelf_edge.b * k), 255};
                }
                for (int x = 0; x < 256; ++x) c.put(x, y, px);
            }
            return c;
        }
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

    // Words in chunky 5x7 capitals (only the letters the dials need),
    // `cell` pixels to a dot, centred on (cx, cy), turned by `angle`.
    static void BlockText(Canvas& c, const char* text, float cx, float cy, float cell, float angle, Rgba color) {
        struct Glyph {
            char ch;
            const char* rows[7];
        };
        static const Glyph kGlyphs[] = {
            {'N', {"X...X", "XX..X", "X.X.X", "X.X.X", "X..XX", "X...X", "X...X"}},
            {'O', {".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."}},
            {'T', {"XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "..X.."}},
            {'H', {"X...X", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"}},
            {'I', {"XXX", ".X.", ".X.", ".X.", ".X.", ".X.", "XXX"}},
            {'G', {".XXX.", "X...X", "X....", "X.XXX", "X...X", "X...X", ".XXX."}},
        };
        const auto find = [](char ch) -> const Glyph* {
            for (const Glyph& g : kGlyphs)
                if (g.ch == ch) return &g;
            return nullptr;
        };
        float width = 0;
        for (const char* p = text; *p; ++p)
            if (const Glyph* g = find(*p)) width += (std::strlen(g->rows[0]) + 1) * cell;
        width -= cell;
        const float ca = std::cos(angle), sa = std::sin(angle);
        const auto at = [&](float lx, float ly) {
            return std::pair<float, float>{cx + lx * ca - ly * sa, cy + lx * sa + ly * ca};
        };
        float x = -width / 2;
        for (const char* p = text; *p; ++p) {
            const Glyph* g = find(*p);
            if (!g) continue;
            const int gw = static_cast<int>(std::strlen(g->rows[0]));
            for (int r = 0; r < 7; ++r)
                for (int k = 0; k < gw; ++k) {
                    if (g->rows[r][k] != 'X') continue;
                    const float lx = x + k * cell, ly = (r - 3.5f) * cell, e = cell + 0.6f;
                    c.polygon({at(lx, ly), at(lx + e, ly), at(lx + e, ly + e), at(lx, ly + e)}, color);
                }
            x += (gw + 1) * cell;
        }
    }

    // The wall for Settings' "Disable clock": big overlapping clock faces
    // in navy, lavender, sage and blue, each with its ticks, a dark rim
    // and NOTHING where its hours would be. `w` 640 or 856 (the faces
    // stay where they are on a 4:3 menu, the wide one has more of them).
    Canvas Dials(int w) const {
        Canvas c(w, 480);
        const float dx = (w - 640) / 2.0f;
        // A soft navy, darker towards the corners.
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < w; ++x) {
                const float u = (x - w / 2.0f) / (w / 2.0f), v = (y - 240) / 240.0f;
                const float k = 1.0f - 0.28f * std::min(1.0f, (u * u + v * v) * 0.6f);
                c.put(x, y, Rgba{static_cast<std::uint8_t>(29 * k), static_cast<std::uint8_t>(42 * k),
                                 static_cast<std::uint8_t>(82 * k), 255});
            }
        const Rgba inkDark{24, 30, 58, 255};
        struct Face {
            float x, y, r;
            Rgba fill;
            float turn;     // where its first NOTHING sits
            bool hands;
        };
        const Face faces[] = {
            {-60 + dx * 0.0f, 410, 190, Rgba{74, 134, 208, 255}, 2.2f, false},  // blue, bottom left
            {640 + dx * 2.0f + 30, 150, 175, Rgba{207, 227, 238, 255}, 3.4f, false},  // pale blue, right
            {90 - dx * 0.4f, 60, 205, Rgba{126, 154, 138, 255}, 0.9f, false},  // sage, top left
            {360 + dx, -30, 215, Rgba{201, 205, 230, 255}, 1.9f, true},  // lavender, top, with hands
        };
        for (const Face& f : faces) {
            // Its shadow on the navy, then the face, then the rim.
            c.circle(f.x + 6, f.y + 9, f.r + 4, Rgba{0, 0, 0, 70});
            c.circle(f.x, f.y, f.r, f.fill);
            c.ring(f.x, f.y, f.r - 1, 5, inkDark);
            for (int t = 0; t < 60; ++t) {
                const float a = t * (6.2831853f / 60);
                const bool major = t % 5 == 0;
                const float r0 = f.r - (major ? 34 : 20), r1 = f.r - 8;
                c.line(f.x + r0 * std::cos(a), f.y + r0 * std::sin(a), f.x + r1 * std::cos(a), f.y + r1 * std::sin(a),
                       major ? 5.0f : 2.4f, inkDark);
            }
            // NOTHING at three of its hours, reading along the radius.
            for (int n = 0; n < 3; ++n) {
                const float a = f.turn + n * 2.0943951f;
                const float rr = f.r - 92;
                const float cx = f.x + rr * std::cos(a), cy = f.y + rr * std::sin(a);
                // Upright enough to read: turned along the radius, flipped on the left.
                float ang = a;
                if (std::cos(a) < 0) ang += 3.14159265f;
                BlockText(c, "NOTHING", cx, cy, 3.4f, ang, inkDark);
            }
            if (f.hands) {
                const float hx = f.x - 60, hy = f.y + 70;  // its hub, off centre as the faces overlap
                c.line(hx, hy, hx + 110, hy - 42, 7, inkDark);
                c.line(hx, hy, hx - 70, hy - 50, 9, inkDark);
                c.circle(hx, hy, 19, inkDark);
                c.circle(hx - 5, hy - 6, 5, Rgba{255, 255, 255, 60});
            }
        }
        return c;
    }

    // What the menu draws behind every screen without a background.png:
    // the backdrop colour, with a 2-pixel stripe every 4 rows.
    Canvas Background(int w = 640) const {
        if (dials) return Dials(w);
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

void paint_drop_shadow9(const Theme& theme, Canvas& out) {
    // The theme's shadow is a faint edge under cards; a box lifted off the
    // page casts a deeper one.
    Rgba deep = ToRgba(theme.colors.shadow);
    deep.a = static_cast<std::uint8_t>(std::min(150, std::max(110, deep.a * 3)));
    out = Canvas(64, 64);
    out.shadow(kShadowMargin, kShadowMargin, 64 - 2 * kShadowMargin, 64 - 2 * kShadowMargin, 6, kShadowMargin, deep);
}

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
        {"tile9", [](const Painter& q) { return q.TileNine(false, false); }},
        {"tile9_over", [](const Painter& q) { return q.TileNine(true, false); }},
        {"tile9_primary", [](const Painter& q) { return q.TileNine(false, true); }},
        {"tile9_primary_over", [](const Painter& q) { return q.TileNine(true, true); }},
        {"prompt_button", [](const Painter& q) { return q.PromptButton(); }},
        {"icon_play", [](const Painter& q) { return q.Play(); }},
        {"icon_mods", [](const Painter& q) { return q.Layers(); }},
        {"icon_cheats", [](const Painter& q) { return q.Bolt(); }},
        {"icon_saves", [](const Painter& q) { return q.Floppy(); }},
        {"icon_info", [](const Painter& q) { return q.Info(); }},
        {"icon_help", [](const Painter& q) { return q.Help(); }},
        {"icon_picture", [](const Painter& q) { return q.Tv(); }},
        {"icon_controllers", [](const Painter& q) { return q.Pad(); }},
        {"icon_online", [](const Painter& q) { return q.Globe(); }},
        {"icon_home", [](const Painter& q) { return q.House(); }},
        {"icon_sound", [](const Painter& q) { return q.Note(); }},
        {"icon_power", [](const Painter& q) { return q.Power(); }},
        {"disc_case", [](const Painter& q) { return q.DiscCase(); }},
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
