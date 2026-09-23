// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

// A small software canvas for the menu's artwork. The Wii draws its panels,
// tiles, buttons, icons and pointer from textures made here once at start:
// anti-aliased shapes (signed distances, one pixel of edge softness) with
// straight alpha, so the GPU only blits them. Kept free of libogc so the
// host tests cover it.
namespace riftwii {

struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
};
constexpr Rgba rgba(std::uint32_t rgb, std::uint8_t a = 255) {
    return Rgba{static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                static_cast<std::uint8_t>(rgb), a};
}

class Canvas {
public:
    Canvas(int width, int height);  // transparent

    int width() const { return width_; }
    int height() const { return height_; }
    Rgba at(int x, int y) const;
    const std::vector<std::uint8_t>& pixels() const { return px_; }  // RGBA rows

    // Every shape blends over what is there (source-over, straight alpha).
    void fill(Rgba color);
    void rect(float x, float y, float w, float h, Rgba color);
    void rounded_rect(float x, float y, float w, float h, float radius, Rgba color);
    // A border `thickness` wide just inside the rounded rectangle's edge.
    void rounded_border(float x, float y, float w, float h, float radius, float thickness, Rgba color);
    // A soft shadow around a rounded rectangle, fading out over `blur`.
    void shadow(float x, float y, float w, float h, float radius, float blur, Rgba color);
    // Rounded rectangle with a vertical two-colour gradient.
    void rounded_gradient(float x, float y, float w, float h, float radius, Rgba top, Rgba bottom);
    void circle(float cx, float cy, float radius, Rgba color);
    void ring(float cx, float cy, float radius, float thickness, Rgba color);
    // A line with round caps.
    void line(float x0, float y0, float x1, float y1, float thickness, Rgba color);
    // Everything below a curve, `top[x]` being its height at column x
    // (the menu's bottom bar); and the curve itself, `thickness` thick.
    void area_below(const std::vector<float>& top, Rgba color);
    void curve(const std::vector<float>& top, float thickness, Rgba color);

private:
    template <typename Coverage>
    void paint(float x0, float y0, float x1, float y1, Rgba color, Coverage coverage);
    void blend(int x, int y, Rgba color, float coverage);

    int width_, height_;
    std::vector<std::uint8_t> px_;
};

// GX_TF_RGBA8: 4x4 tiles of 64 bytes (the tile's AR pairs, then its GB
// pairs). The canvas must be a multiple of 4 wide and high.
std::vector<std::uint8_t> to_gx_rgba8(const Canvas& canvas);

}  // namespace riftwii
