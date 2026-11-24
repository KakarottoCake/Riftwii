// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/storeicons.hpp"

#include <cmath>
#include <utility>
#include <vector>

namespace riftwii {
namespace {

// Strokes laid out on the menu icons' 28-unit grid, drawn `k` times as
// big: the app store's 40x40 icons keep the 28x28 icons' line weights.
struct Pen {
    Canvas& c;
    float k;
    Rgba ink;
    void line(float x0, float y0, float x1, float y1, float t) const {
        c.line(x0 * k, y0 * k, x1 * k, y1 * k, t * k, ink);
    }
    void ring(float x, float y, float r, float t) const { c.ring(x * k, y * k, r * k, t * k, ink); }
    void circle(float x, float y, float r) const { c.circle(x * k, y * k, r * k, ink); }
    void rect(float x, float y, float w, float h) const { c.rect(x * k, y * k, w * k, h * k, ink); }
    void frame(float x, float y, float w, float h, float radius, float t) const {
        c.rounded_border(x * k, y * k, w * k, h * k, radius * k, t * k, ink);
    }
    void shape(std::vector<std::pair<float, float>> points) const {
        for (auto& p : points) {
            p.first *= k;
            p.second *= k;
        }
        c.polygon(points, ink);
    }
    // Part of the circle at x y, from angle a0 to a1 (radians, clockwise
    // from three o'clock: the picture's y grows downwards).
    void arc(float x, float y, float r, float a0, float a1, float t) const {
        constexpr int kSteps = 24;
        for (int i = 0; i < kSteps; ++i) {
            const float b0 = a0 + (a1 - a0) * i / kSteps;
            const float b1 = a0 + (a1 - a0) * (i + 1) / kSteps;
            line(x + r * std::cos(b0), y + r * std::sin(b0), x + r * std::cos(b1), y + r * std::sin(b1), t);
        }
    }
    // A four-pointed sparkle.
    void sparkle(float x, float y, float r, float inner) const {
        std::vector<std::pair<float, float>> points;
        for (int i = 0; i < 8; ++i) {
            const float a = -3.14159265f / 2 + i * 3.14159265f / 4;
            const float d = i % 2 == 0 ? r : inner;
            points.emplace_back(x + d * std::cos(a), y + d * std::sin(a));
        }
        shape(points);
    }
};

// The categories, in appcatalog.cpp's order (store_games to store_more):
// 40x40, on the 28-unit grid of the menu's icons.
Canvas Category(int which, Rgba glyph) {
    Canvas c(40, 40);
    const Pen p{c, 40.0f / 28.0f, glyph};
    constexpr float kPi = 3.14159265f;
    switch (which) {
        case 0:  // Games: a game pad
            p.frame(3, 8.5f, 22, 12.5f, 6, 2.0f);
            p.rect(6.6f, 13.9f, 6.2f, 1.9f);
            p.rect(8.75f, 11.75f, 1.9f, 6.2f);
            p.circle(18.6f, 13.4f, 1.5f);
            p.circle(21.2f, 16.2f, 1.5f);
            break;
        case 1:  // Emulators: an old console's cartridge
            p.frame(6, 3, 16, 22, 2, 2.0f);
            p.frame(9, 6.5f, 10, 8.5f, 1, 1.6f);
            for (float x : {9.5f, 12.5f, 15.5f, 18.5f}) p.line(x, 19, x, 22.5f, 1.5f);
            break;
        case 2:  // System tools: a chip
            p.frame(8, 8, 12, 12, 1.5f, 2.0f);
            p.rect(11.5f, 11.5f, 5, 5);
            for (float o : {10.5f, 14.0f, 17.5f}) {
                p.line(o, 3.5f, o, 8, 1.6f);
                p.line(o, 20, o, 24.5f, 1.6f);
                p.line(3.5f, o, 8, o, 1.6f);
                p.line(20, o, 24.5f, o, 1.6f);
            }
            break;
        case 3: {  // Game tools: a wrench, its jaw open to the upper right
            p.line(5.5f, 22.5f, 15, 13, 3.6f);
            const float open = -kPi / 4;
            p.arc(18.5f, 9.5f, 5.2f, open + 0.9f, open + 2 * kPi - 0.9f, 2.8f);
            break;
        }
        case 4:  // Wii Menu: four channels
            for (float y : {5.0f, 15.0f}) {
                for (float x : {3.5f, 15.0f}) p.frame(x, y, 9.5f, 8, 2.2f, 1.8f);
            }
            break;
        case 5:  // Controllers: a Wii Remote, its D-pad and A at the top
            p.frame(9.5f, 2.5f, 9, 23, 3.5f, 2.0f);
            p.rect(12.3f, 6.6f, 3.4f, 1.4f);
            p.rect(13.3f, 5.6f, 1.4f, 3.4f);
            p.circle(14, 12.5f, 1.6f);
            p.circle(14, 18.5f, 0.9f);
            p.circle(14, 21.5f, 0.9f);
            break;
        case 6:  // Creative: a pencil
            p.line(10, 18, 21, 7, 4.6f);
            p.shape({{4.5f, 23.5f}, {5.8f, 17.8f}, {10.2f, 22.2f}});
            break;
        case 7:  // Demos and toys: sparkles
            p.sparkle(11.5f, 15.5f, 9.5f, 2.6f);
            p.sparkle(21.5f, 7, 4.5f, 1.3f);
            p.sparkle(21.5f, 21, 3.2f, 1.0f);
            break;
        case 8:  // Media: a screen playing
            p.frame(3, 6, 22, 16, 3, 2.0f);
            p.shape({{11.5f, 10}, {11.5f, 18}, {18.5f, 14}});
            break;
        case 9:  // Internet: a globe
            p.ring(14, 14, 10.5f, 2.0f);
            for (int i = 0; i < 24; ++i) {
                const float a0 = i * kPi / 12, a1 = (i + 1) * kPi / 12;
                p.line(14 + 4.5f * std::cos(a0), 14 + 10.5f * std::sin(a0), 14 + 4.5f * std::cos(a1),
                       14 + 10.5f * std::sin(a1), 1.6f);
            }
            p.line(3.5f, 14, 24.5f, 14, 1.6f);
            p.line(6, 8.5f, 22, 8.5f, 1.4f);
            p.line(6, 19.5f, 22, 19.5f, 1.4f);
            break;
        case 10:  // Online services: a wireless signal
            p.circle(14, 21.5f, 2.2f);
            for (float r : {6.0f, 11.0f, 16.0f}) p.arc(14, 21.5f, r, -3 * kPi / 4, -kPi / 4, 2.3f);
            break;
        case 11:  // Loaders: a disc
            p.ring(14, 14, 10.5f, 2.0f);
            p.ring(14, 14, 3.2f, 1.8f);
            p.arc(14, 14, 7, -2.6f, -1.6f, 1.4f);
            break;
        case 13: {  // The Rift Store itself: a shopping bag torn by a rift, as in its logo
            const float body[][2] = {{6.5f, 10}, {21.5f, 10}, {23, 25}, {5, 25}, {6.5f, 10}};
            for (int i = 0; i + 1 < 5; ++i) p.line(body[i][0], body[i][1], body[i + 1][0], body[i + 1][1], 2.0f);
            p.arc(10.5f, 10, 2.6f, -kPi, 0, 1.8f);
            p.arc(17.5f, 10, 2.6f, -kPi, 0, 1.8f);
            const float tear[][2] = {{14.6f, 10.5f}, {12.9f, 14.2f}, {15.3f, 17.6f}, {12.7f, 21.2f}, {14.3f, 24.6f}};
            for (int i = 0; i + 1 < 5; ++i) p.line(tear[i][0], tear[i][1], tear[i + 1][0], tear[i + 1][1], 1.7f);
            break;
        }
        default:  // More: three dots
            for (float x : {7.0f, 14.0f, 21.0f}) p.circle(x, 14, 2.6f);
            break;
    }
    return c;
}

// The facts on an app's page: its download's size, its date, the
// consoles it works on and its license. 28x28.
Canvas Fact(int which, Rgba glyph) {
    Canvas c(28, 28);
    const Pen p{c, 1.0f, glyph};
    switch (which) {
        case 0:  // Size: a download into a tray
            p.line(14, 4, 14, 16.5f, 2.4f);
            p.line(8.8f, 11.5f, 14, 16.8f, 2.4f);
            p.line(19.2f, 11.5f, 14, 16.8f, 2.4f);
            p.line(5, 18, 5, 23, 2.2f);
            p.line(5, 23, 23, 23, 2.2f);
            p.line(23, 23, 23, 18, 2.2f);
            break;
        case 1:  // Date: a calendar
            p.frame(4, 6, 20, 18, 2.5f, 2.0f);
            p.rect(4, 6, 20, 5);
            p.line(9.5f, 3.5f, 9.5f, 8, 2.2f);
            p.line(18.5f, 3.5f, 18.5f, 8, 2.2f);
            for (float x : {8.0f, 12.5f, 17.0f}) p.rect(x, 14.5f, 3, 2.6f);
            for (float x : {8.0f, 12.5f}) p.rect(x, 19, 3, 2.6f);
            break;
        case 2:  // Works on: a Wii standing up
            p.frame(10, 3, 8, 20, 2, 2.0f);
            p.line(14, 6.5f, 14, 13, 1.5f);
            p.line(8, 25, 20, 25, 2.2f);
            break;
        default: {  // License: a page with a folded corner
            const float page[][2] = {{7, 3}, {17, 3}, {22, 8}, {22, 25}, {7, 25}, {7, 3}};
            for (int i = 0; i + 1 < 6; ++i) p.line(page[i][0], page[i][1], page[i + 1][0], page[i + 1][1], 2.0f);
            p.line(17, 3, 17, 8, 1.6f);
            p.line(17, 8, 22, 8, 1.6f);
            p.line(10.5f, 13, 18.5f, 13, 1.6f);
            p.line(10.5f, 17, 18.5f, 17, 1.6f);
            p.line(10.5f, 21, 15.5f, 21, 1.6f);
            break;
        }
    }
    return c;
}

}  // namespace

bool paint_store_icon(const std::string& name, Rgba ink, Canvas& out) {
    static const char* const kCategories[] = {"games", "emulators", "system", "gametools", "wiimenu", "controllers",
                                              "creative", "toys", "media", "internet", "online", "loaders", "more"};
    static const char* const kFacts[] = {"size", "date", "console", "license"};
    if (name.compare(0, 6, "store_") != 0) return false;
    const std::string key = name.substr(6);
    for (int i = 0; i < 13; ++i) {
        if (key == kCategories[i]) {
            out = Category(i, ink);
            return true;
        }
    }
    if (key == "logo") {
        out = Category(13, ink);
        return true;
    }
    for (int i = 0; i < 4; ++i) {
        if (key == kFacts[i]) {
            out = Fact(i, ink);
            return true;
        }
    }
    return false;
}

}  // namespace riftwii
