// SPDX-FileCopyrightText: 2026 RiftWii contributors
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

// A value in [0, 255.5) to the nearest byte, halves up: what std::lround
// gives for these, without its library call (the menu's art is millions
// of these at start).
std::uint8_t round_byte(float v) { return static_cast<std::uint8_t>(static_cast<int>(v + 0.5f)); }

}  // namespace

// The pixels whose centres are surely deeper than `depth` inside the
// rounded rectangle (distance below `depth`, which is negative or zero),
// with a pixel to spare: there a shape's coverage is known without the
// distance. Along a straight side the distance is the distance to that
// side, so the wide box reaches to within a pixel or so of the left and
// right sides between the corners, and the tall box likewise of the top
// and bottom: only the corners and the edges are worked out.
Canvas::Inner Canvas::inside_rounded_rect(float x, float y, float w, float h, float radius, float depth) {
    Inner in;
    const float hw = w * 0.5f, hh = h * 0.5f;
    radius = std::min(radius, std::min(hw, hh));
    if (radius < 0.0f) return in;
    const float cx = x + hw, cy = y + hh;
    // Pixel i's centre is i + 0.5: the pixels closer than `a` to `c`.
    const auto span = [](float c, float a, int& from, int& to) {
        from = static_cast<int>(std::floor(c - a - 0.5f)) + 1;
        to = static_cast<int>(std::ceil(c + a - 0.5f));
    };
    // Between the top and bottom corners, and never deeper in y than
    // allowed; and likewise turned round.
    const float wideX = hw + depth - 1.0f, wideY = std::min(hh - radius, hh + depth) - 1.0f;
    const float tallX = std::min(hw - radius, hw + depth) - 1.0f, tallY = hh + depth - 1.0f;
    if (wideX > 0.0f && wideY > 0.0f) {
        span(cx, wideX, in.x0, in.x1);
        span(cy, wideY, in.y0, in.y1);
        if (in.x0 >= in.x1 || in.y0 >= in.y1) in.x0 = in.x1 = in.y0 = in.y1 = 0;
    }
    if (tallX > 0.0f && tallY > 0.0f) {
        span(cx, tallX, in.vx0, in.vx1);
        span(cy, tallY, in.vy0, in.vy1);
        if (in.vx0 >= in.vx1 || in.vy0 >= in.vy1) in.vx0 = in.vx1 = in.vy0 = in.vy1 = 0;
    }
    return in;
}

Canvas::Canvas(int width, int height)
    : width_(std::max(width, 0)), height_(std::max(height, 0)),
      px_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4, 0) {}

Rgba Canvas::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return Rgba{};
    const std::uint8_t* p = &px_[(static_cast<std::size_t>(y) * width_ + x) * 4];
    return Rgba{p[0], p[1], p[2], p[3]};
}

void Canvas::put(int x, int y, Rgba color) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    std::uint8_t* p = &px_[(static_cast<std::size_t>(y) * width_ + x) * 4];
    p[0] = color.r;
    p[1] = color.g;
    p[2] = color.b;
    p[3] = color.a;
}

void Canvas::blend(int x, int y, Rgba color, float coverage) {
    const float sa = clamp01(coverage) * (color.a / 255.0f);
    if (sa <= 0.0f) return;
    std::uint8_t* p = &px_[(static_cast<std::size_t>(y) * width_ + x) * 4];
    // Over nothing, or fully opaque: the source colour itself (what the
    // sums below come to in both cases).
    if (p[3] == 0 || sa >= 1.0f) {
        p[0] = color.r;
        p[1] = color.g;
        p[2] = color.b;
        p[3] = p[3] == 0 ? round_byte(sa * 255.0f) : 255;
        return;
    }
    const float da = p[3] / 255.0f;
    const float oa = sa + da * (1.0f - sa);
    if (oa <= 0.0f) return;
    const auto mix = [&](std::uint8_t s, std::uint8_t d) {
        const float v = (s * sa + d * da * (1.0f - sa)) / oa;
        return round_byte(std::min(v, 255.0f));
    };
    p[0] = mix(color.r, p[0]);
    p[1] = mix(color.g, p[1]);
    p[2] = mix(color.b, p[2]);
    p[3] = round_byte(oa * 255.0f);
}

template <typename Coverage>
void Canvas::paint(float x0, float y0, float x1, float y1, Rgba color, Coverage coverage, const Inner& inner) {
    const int ix0 = std::max(0, static_cast<int>(std::floor(x0)));
    const int iy0 = std::max(0, static_cast<int>(std::floor(y0)));
    const int ix1 = std::min(width_, static_cast<int>(std::ceil(x1)));
    const int iy1 = std::min(height_, static_cast<int>(std::ceil(y1)));
    const auto run = [&](int y, int from, int to) {
        for (int x = from; x < to; ++x) {
            const float c = coverage(x + 0.5f, y + 0.5f);
            if (c > 0.0f) blend(x, y, color, c);
        }
    };
    // Inside `inner` the coverage is the same everywhere: no distances.
    for (int y = iy0; y < iy1; ++y) {
        int in0 = 0, in1 = 0;
        if (y >= inner.y0 && y < inner.y1) {
            in0 = std::max(ix0, inner.x0);
            in1 = std::min(ix1, inner.x1);
        } else if (y >= inner.vy0 && y < inner.vy1) {
            in0 = std::max(ix0, inner.vx0);
            in1 = std::min(ix1, inner.vx1);
        }
        if (in0 >= in1) {
            run(y, ix0, ix1);
            continue;
        }
        run(y, ix0, in0);
        if (inner.coverage >= 1.0f && color.a == 255) {
            // Opaque over everything: the colour itself, as blend() gives.
            std::uint8_t* p = &px_[(static_cast<std::size_t>(y) * width_ + in0) * 4];
            for (int x = in0; x < in1; ++x, p += 4) {
                p[0] = color.r;
                p[1] = color.g;
                p[2] = color.b;
                p[3] = 255;
            }
        } else if (inner.coverage > 0.0f) {
            for (int x = in0; x < in1; ++x) blend(x, y, color, inner.coverage);
        }
        run(y, in1, ix1);
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
    Inner inner = inside_rounded_rect(x, y, w, h, radius, -0.5f);  // covered fully
    inner.coverage = 1.0f;
    paint(x - 1, y - 1, x + w + 1, y + h + 1, color, [&](float px, float py) {
        return clamp01(0.5f - rounded_rect_distance(px, py, x, y, w, h, radius));
    }, inner);
}

void Canvas::rounded_border(float x, float y, float w, float h, float radius, float thickness, Rgba color) {
    const Inner inner = inside_rounded_rect(x, y, w, h, radius, -thickness - 0.5f);  // inside the border
    paint(x - 1, y - 1, x + w + 1, y + h + 1, color, [&](float px, float py) {
        const float d = rounded_rect_distance(px, py, x, y, w, h, radius);
        return clamp01(0.5f - d) * clamp01(d + thickness + 0.5f);
    }, inner);
}

void Canvas::shadow(float x, float y, float w, float h, float radius, float blur, Rgba color, float hollow) {
    blur = std::max(blur, 1.0f);
    Inner inner = inside_rounded_rect(x, y, w, h, radius, hollow > 0.0f ? -hollow : 0.0f);
    inner.coverage = hollow > 0.0f ? 0.0f : 1.0f;
    paint(x - blur - 1, y - blur - 1, x + w + blur + 1, y + h + blur + 1, color, [&](float px, float py) {
        const float d = rounded_rect_distance(px, py, x, y, w, h, radius);
        if (d <= 0.0f) return 1.0f;
        const float t = clamp01(1.0f - d / blur);
        return t * t;
    }, inner);
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

void Canvas::polygon(const std::vector<std::pair<float, float>>& points, Rgba color) {
    if (points.size() < 3) return;
    float bx0 = points[0].first, bx1 = bx0, by0 = points[0].second, by1 = by0;
    for (const auto& p : points) {
        bx0 = std::min(bx0, p.first);
        bx1 = std::max(bx1, p.first);
        by0 = std::min(by0, p.second);
        by1 = std::max(by1, p.second);
    }
    const auto inside = [&](float x, float y) {
        bool in = false;
        for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
            const float xi = points[i].first, yi = points[i].second;
            const float xj = points[j].first, yj = points[j].second;
            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
        }
        return in;
    };
    const auto coverage = [&](float px, float py) {
        int hits = 0;
        for (int sy = 0; sy < 4; ++sy)
            for (int sx = 0; sx < 4; ++sx) hits += inside(px - 0.375f + sx * 0.25f, py - 0.375f + sy * 0.25f);
        return hits / 16.0f;
    };
    paint(bx0 - 1, by0 - 1, bx1 + 1, by1 + 1, color, coverage);
}

void Canvas::line(float x0, float y0, float x1, float y1, float thickness, Rgba color) {
    const float r = thickness * 0.5f;
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    const auto coverage = [&](float px, float py) {
        const float t = len2 > 0 ? clamp01(((px - x0) * dx + (py - y0) * dy) / len2) : 0.0f;
        const float d = std::hypot(px - (x0 + t * dx), py - (y0 + t * dy)) - r;
        return clamp01(0.5f - d);
    };
    const float bx0 = std::min(x0, x1) - r - 1, bx1 = std::max(x0, x1) + r + 1;
    const float by0 = std::min(y0, y1) - r - 1, by1 = std::max(y0, y1) + r + 1;
    if (std::fabs(dy) < 1.0f) {
        paint(bx0, by0, bx1, by1, color, coverage);
        return;
    }
    // A slanted line covers only a short stretch of each row: the pixels
    // closer than r + 0.5 to the line through both ends (its caps too), with
    // two pixels to spare. The distance is worked out as before inside it.
    const float reach = (r + 0.5f) * std::sqrt(len2) / std::fabs(dy) + 2.0f;
    const int iy0 = std::max(0, static_cast<int>(std::floor(by0)));
    const int iy1 = std::min(height_, static_cast<int>(std::ceil(by1)));
    for (int y = iy0; y < iy1; ++y) {
        const float cx = x0 + (y + 0.5f - y0) * dx / dy;  // where the line crosses the row
        paint(std::max(bx0, cx - reach - 0.5f), static_cast<float>(y), std::min(bx1, cx + reach + 0.5f),
              static_cast<float>(y + 1), color, coverage);
    }
}

void Canvas::diagonal_stripes(float period, float thickness, Rgba color) {
    const float r = thickness * 0.5f;
    const float root2 = std::sqrt(2.0f);
    for (int y = 0; y < height_; ++y) {
        // x + y + 1 (the pixel's centre, summed) past the last line, stepped
        // along the row.
        float u = std::fmod(y + 1.0f, period);
        for (int x = 0; x < width_; ++x, u += 1.0f) {
            if (u >= period) u -= period;
            // The pixel's centre from the nearest line x + y = n * period.
            const float d = std::min(u, period - u) / root2 - r;
            if (d < 0.5f) blend(x, y, color, clamp01(0.5f - d));
        }
    }
}

void Canvas::area_below(const std::vector<float>& top, Rgba color, float depth) {
    for (int x = 0; x < width_ && x < static_cast<int>(top.size()); ++x) {
        // Nothing is covered above top[x] - 1.
        const float first = std::floor(top[x] - 1.0f);
        const float last = depth > 0.0f ? std::ceil(top[x] + depth) : static_cast<float>(height_);
        const int end = last < static_cast<float>(height_) ? static_cast<int>(last) : height_;
        for (int y = first > 0.0f ? static_cast<int>(first) : 0; y < end; ++y) {
            const float c = clamp01(y + 0.5f - top[x] + 0.5f);
            if (c >= 1.0f && color.a == 255) {
                // Opaque over everything: the colour itself, as blend() gives.
                std::uint8_t* p = &px_[(static_cast<std::size_t>(y) * width_ + x) * 4];
                p[0] = color.r;
                p[1] = color.g;
                p[2] = color.b;
                p[3] = 255;
            } else if (c > 0.0f) {
                blend(x, y, color, c);
            }
        }
    }
}

void Canvas::curve(const std::vector<float>& top, float thickness, Rgba color) {
    // Distance to the polyline through the column centres, near each column.
    const int n = std::min(width_, static_cast<int>(top.size()));
    const float r = thickness * 0.5f;
    for (int x = 0; x < n; ++x) {
        // Only rows near the segments' own heights can be reached.
        float lo = 1e9f, hi = -1e9f;
        for (int k = std::max(0, x - 3); k < std::min(n - 1, x + 3); ++k) {
            lo = std::min(lo, std::min(top[k], top[k + 1]));
            hi = std::max(hi, std::max(top[k], top[k + 1]));
        }
        if (lo > hi) continue;  // no segment: nothing to draw
        const float first = std::floor(lo - r - 2.0f), last = std::ceil(hi + r + 2.0f);
        const int y0 = first > 0.0f ? static_cast<int>(first) : 0;
        const int y1 = last < static_cast<float>(height_) ? static_cast<int>(last) : height_;
        for (int y = y0; y < y1; ++y) {
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
    if (!to_gx_rgba8(canvas.pixels().data(), w, h, out.data())) return {};
    return out;
}

bool to_gx_rgba8(const std::uint8_t* px, int w, int h, std::uint8_t* out) {
    if (w <= 0 || h <= 0 || w % 4 != 0 || h % 4 != 0) return false;
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
    return true;
}

}  // namespace riftwii
