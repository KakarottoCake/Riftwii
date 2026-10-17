// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/inputicons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "inputicons_data.hpp"

namespace riftwii {

std::string input_icon_for_control(const std::string& type) {
    if (type == "wiimote") return "wii_controller";
    if (type == "nunchuk") return "wii_controller_nunchuk";
    if (type == "classiccontroller") return "controller_wii_classic";
    if (type == "gamecube") return "gamecube_controller";
    return "";
}

std::string input_icon_for_button(const std::string& button) {
    if (button == "A") return "wii_button_a";
    if (button == "B") return "wii_button_b";
    if (button == "+") return "wii_button_plus";
    if (button == "-") return "wii_button_minus";
    if (button == "1") return "wii_button_1";
    if (button == "2") return "wii_button_2";
    if (button == "HOME") return "wii_button_home";
    if (button == "C") return "wii_button_c";
    if (button == "Z") return "wii_button_z";
    if (button == "Stick") return "stick";
    if (button == "D-pad") return "dpad";
    return "";
}

// How much of a drawn picture covers the point (u, v), each from -0.5 to
// 0.5 across the picture: 1 inside, 0 outside. RiftWii draws these two
// (Kenney's pictures that come with RiftWii have no stick or D-pad), in
// the same filled style as the button pictures and over the same area:
// Kenney's buttons reach 0.375 from the middle (8 to 55 of 64 pixels).
static bool drawn_icon_covers(const std::string& name, double u, double v) {
    const double reach = 0.375;
    if (name == "stick") {
        // A stick's top seen from above: a disc with a ring cut round its
        // middle, so it does not read as a plain button.
        const double r = std::sqrt(u * u + v * v);
        const bool groove = r > 0.24 && r < 0.30;
        return r <= reach && !groove;
    }
    if (name == "dpad") {
        // A cross with a small hole at its middle, where the pad pivots.
        const double arm = 0.13;
        const bool across = std::fabs(v) <= arm && std::fabs(u) <= reach;
        const bool down = std::fabs(u) <= arm && std::fabs(v) <= reach;
        const bool middle = u * u + v * v < 0.06 * 0.06;
        return (across || down) && !middle;
    }
    return false;
}

static bool is_drawn_icon(const std::string& name) {
    return name == "stick" || name == "dpad";
}

// A drawn picture, each pixel's alpha the share of 4 x 4 samples inside it.
static void paint_drawn_icon(const std::string& name, int size, Rgba color, Canvas& out) {
    const int samples = 4;
    Canvas c(size, size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int inside = 0;
            for (int sy = 0; sy < samples; ++sy) {
                for (int sx = 0; sx < samples; ++sx) {
                    const double u = (x + (sx + 0.5) / samples) / size - 0.5;
                    const double v = (y + (sy + 0.5) / samples) / size - 0.5;
                    if (drawn_icon_covers(name, u, v)) ++inside;
                }
            }
            Rgba p = color;
            p.a = static_cast<std::uint8_t>((color.a * inside + samples * samples / 2) / (samples * samples));
            c.put(x, y, p);
        }
    }
    out = c;
}

bool paint_input_icon(const std::string& name, int size, Rgba color, Canvas& out) {
    if (size <= 0 || size > 256) return false;
    if (is_drawn_icon(name)) {
        paint_drawn_icon(name, size, color, out);
        return true;
    }
    const InputIconData* icon = nullptr;
    for (unsigned i = 0; i < kInputIconCount; ++i)
        if (name == kInputIcons[i].name) icon = &kInputIcons[i];
    if (!icon) return false;
    // Each target pixel's alpha: the source's over the area it covers
    // (area averaging, so a small badge keeps its letter's shape).
    const int src = kInputIconSource;
    const double step = static_cast<double>(src) / size;
    Canvas c(size, size);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double x0 = x * step, x1 = (x + 1) * step, y0 = y * step, y1 = (y + 1) * step;
            double sum = 0, area = 0;
            for (int sy = static_cast<int>(y0); sy < src && sy < y1; ++sy) {
                const double hy = std::min<double>(sy + 1, y1) - std::max<double>(sy, y0);
                for (int sx = static_cast<int>(x0); sx < src && sx < x1; ++sx) {
                    const double wx = std::min<double>(sx + 1, x1) - std::max<double>(sx, x0);
                    sum += icon->alpha[sy * src + sx] * wx * hy;
                    area += wx * hy;
                }
            }
            const double a = area > 0 ? sum / area : 0;
            Rgba p = color;
            p.a = static_cast<std::uint8_t>(std::min(255.0, a * color.a / 255.0 + 0.5));
            c.put(x, y, p);
        }
    }
    out = c;
    return true;
}

}  // namespace riftwii
