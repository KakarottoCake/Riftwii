// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/inputicons.hpp"

#include <algorithm>
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
    return "";
}

bool paint_input_icon(const std::string& name, int size, Rgba color, Canvas& out) {
    if (size <= 0 || size > 256) return false;
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
