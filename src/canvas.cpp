// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/canvas.hpp"

#include <algorithm>
#include <cmath>

namespace riftwii {
namespace {

float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

// Signed distance from (px, py) to a rounded rectangle: negative inside.
float rounded_rect_distance(float px, float py, float x, float y, float w, float h, float radius) {
    const float hw = w * 0.5f, hh = h * 0.5f;
    radius = std::min(radius, std::min(hw, hh));
    const float qx = std::fabs(px - (x + hw)) - (hw - radius);
    const float qy = std::fabs(py - (y + hh)) - (hh - radius);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

}  // namespace

Canvas::Canvas(int width, int height)
    : width_(std::max(width, 0)), height_(std::max(height, 0)),
      px_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4, 0) {}

Rgba Canvas::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return Rgba{};
    const std::uint8_t* p = &px_[(static_cast<std::size_t>(y) * width_ + x) * 4];
    return Rgba{p[0], p[1], p[2], p[3]};
}

void Canvas::blend(int x, int y, Rgba color, float coverage) {
    const float sa = clamp01(coverage) * (color.a / 255.0f);
    if (sa <= 0.0f) return;
    std::uint8_t* p = &px_[(static_cast<std::size_t>(y) * width_ + x) * 4];
    const float da = p[3] / 255.0f;
    const float oa = sa + da * (1.0f - sa);
    if (oa <= 0.0f) return;
    const auto mix = [&](std::uint8_t s, std::uint8_t d) {
        const float v = (s * sa + d * da * (1.0f - sa)) / oa;
        return static_cast<std::uint8_t>(std::lround(std::min(v, 255.0f)));
    };
    p[0] = mix(color.r, p[0]);
    p[1] = mix(color.g, p[1]);
    p[2] = mix(color.b, p[2]);
    p[3] = static_cast<std::uint8_t>(std::lround(oa * 255.0f));
}

template <typename Coverage>
void Canvas::paint(float x0, float y0, float x1, float y1, Rgba color, Coverage coverage) {
    const int ix0 = std::max(0, static_cast<int>(std::floor(x0)));
    const int iy0 = std::max(0, static_cast<int>(std::floor(y0)));
    const int ix1 = std::min(width_, static_cast<int>(std::ceil(x1)));
    const int iy1 = std::min(height_, static_cast<int>(std::ceil(y1)));
    for (int y = iy0; y < iy1; ++y) {
        for (int x = ix0; x < ix1; ++x) {
            const float c = coverage(x + 0.5f, y + 0.5f);
            if (c > 0.0f) blend(x, y, color, c);
        }
    }
}

void Canvas::fill(Rgba color) {
    paint(0, 0, static_cast<float>(width_), static_cast<float>(height_), color, [](float, float) { return 1.0f; });
}

void Canvas::rect(float x, float y, float w, float h, Rgba color) {
    paint(x, y, x + w, y + h, color, [&](float px, float py) {
        return clamp01(std::min(px - x + 0.5f, x + w - px + 0.5f)) * clamp01(std::min(py - y + 0.5f, y + h - py + 0.5f));
    });
}

void Canvas::rounded_rect(float x, float y, float w, float h, float radius, Rgba color) {
    paint(x - 1, y - 1, x + w + 1, y + h + 1, color, [&](float px, float py) {
        return clamp01(0.5f - rounded_rect_distance(px, py, x, y, w, h, radius));
    });
}

void Canvas::rounded_border(float x, float y, float w, float h, float radius, float thickness, Rgba color) {
    paint(x - 1, y - 1, x + w + 1, y + h + 1, color, [&](float px, float py) {
        const float d = rounded_rect_distance(px, py, x, y, w, h, radius);
        return clamp01(0.5f - d) * clamp01(d + thickness + 0.5f);
    });
}

void Canvas::shadow(float x, float y, float w, float h, float radius, float blur, Rgba color) {
    blur = std::max(blur, 1.0f);
    paint(x - blur - 1, y - blur - 1, x + w + blur + 1, y + h + blur + 1, color, [&](float px, float py) {
        const float d = rounded_rect_distance(px, py, x, y, w, h, radius);
        if (d <= 0.0f) return 1.0f;
        const float t = clamp01(1.0f - d / blur);
        return t * t;
    });
}

void Canvas::rounded_gradient(float x, float y, float w, float h, float radius, Rgba top, Rgba bottom) {
    const int iy0 = std::max(0, static_cast<int>(std::floor(y - 1)));
    const int iy1 = std::min(height_, static_cast<int>(std::ceil(y + h + 1)));
    for (int row = iy0; row < iy1; ++row) {
        const float t = h > 0 ? clamp01((row + 0.5f - y) / h) : 0.0f;
        const auto lerp = [t](std::uint8_t a, std::uint8_t b) {
            return static_cast<std::uint8_t>(std::lround(a + (b - a) * t));
        };
        const Rgba c{lerp(top.r, bottom.r), lerp(top.g, bottom.g), lerp(top.b, bottom.b), lerp(top.a, bottom.a)};
        paint(x - 1, static_cast<float>(row), x + w + 1, static_cast<float>(row + 1), c, [&](float px, float py) {
            return clamp01(0.5f - rounded_rect_distance(px, py, x, y, w, h, radius));
        });
    }
}

void Canvas::circle(float cx, float cy, float radius, Rgba color) {
    paint(cx - radius - 1, cy - radius - 1, cx + radius + 1, cy + radius + 1, color, [&](float px, float py) {
        return clamp01(radius + 0.5f - std::hypot(px - cx, py - cy));
    });
}

void Canvas::ring(float cx, float cy, float radius, float thickness, Rgba color) {
    paint(cx - radius - 1, cy - radius - 1, cx + radius + 1, cy + radius + 1, color, [&](float px, float py) {
        const float d = std::hypot(px - cx, py - cy) - radius;
        return clamp01(0.5f - d) * clamp01(d + thickness + 0.5f);
    });
}

void Canvas::line(float x0, float y0, float x1, float y1, float thickness, Rgba color) {
    const float r = thickness * 0.5f;
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    paint(std::min(x0, x1) - r - 1, std::min(y0, y1) - r - 1, std::max(x0, x1) + r + 1, std::max(y0, y1) + r + 1,
          color, [&](float px, float py) {
              const float t = len2 > 0 ? clamp01(((px - x0) * dx + (py - y0) * dy) / len2) : 0.0f;
              const float d = std::hypot(px - (x0 + t * dx), py - (y0 + t * dy)) - r;
              return clamp01(0.5f - d);
          });
}

void Canvas::area_below(const std::vector<float>& top, Rgba color) {
    for (int x = 0; x < width_ && x < static_cast<int>(top.size()); ++x) {
        for (int y = 0; y < height_; ++y) {
            const float c = clamp01(y + 0.5f - top[x] + 0.5f);
            if (c > 0.0f) blend(x, y, color, c);
        }
    }
}

void Canvas::curve(const std::vector<float>& top, float thickness, Rgba color) {
    // Distance to the polyline through the column centres, near each column.
    const int n = std::min(width_, static_cast<int>(top.size()));
    const float r = thickness * 0.5f;
    for (int x = 0; x < n; ++x) {
        for (int y = 0; y < height_; ++y) {
            const float px = x + 0.5f, py = y + 0.5f;
            float best = 1e9f;
            for (int k = std::max(0, x - 3); k < std::min(n - 1, x + 3); ++k) {
                const float ax = k + 0.5f, ay = top[k], bx = k + 1.5f, by = top[k + 1];
                const float dx = bx - ax, dy = by - ay;
                const float t = clamp01(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy));
                best = std::min(best, std::hypot(px - (ax + t * dx), py - (ay + t * dy)));
            }
            const float c = clamp01(r + 0.5f - best);
            if (c > 0.0f) blend(x, y, color, c);
        }
    }
}

std::vector<std::uint8_t> to_gx_rgba8(const Canvas& canvas) {
    const int w = canvas.width(), h = canvas.height();
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4, 0);
    if (w % 4 != 0 || h % 4 != 0) return {};
    const std::vector<std::uint8_t>& px = canvas.pixels();
    std::size_t o = 0;
    for (int ty = 0; ty < h; ty += 4) {
        for (int tx = 0; tx < w; tx += 4) {
            for (int pass = 0; pass < 2; ++pass) {
                for (int y = ty; y < ty + 4; ++y) {
                    for (int x = tx; x < tx + 4; ++x) {
                        const std::uint8_t* p = &px[(static_cast<std::size_t>(y) * w + x) * 4];
                        if (pass == 0) {
                            out[o++] = p[3];
                            out[o++] = p[0];
                        } else {
                            out[o++] = p[1];
                            out[o++] = p[2];
                        }
                    }
                }
            }
        }
    }
    return out;
}

}  // namespace riftwii
