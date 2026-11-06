// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_busy.hpp"

#include <algorithm>
#include <ogc/cache.h>

#include "skin.hpp"
#include "transition.hpp"
#include "video.h"

namespace riftwii::wii {
namespace {

void BindBusyDot() {
    // Coverage of a radius-three circle, sampled on a 16 x 16 grid per
    // texel. A transparent border lets filtering soften its edge even when
    // widescreen puts the six-pixel dot between framebuffer columns.
    static constexpr u8 coverage[8][8] = {
        {0, 0, 0, 0, 0, 0, 0, 0},
        {0, 6, 146, 241, 241, 146, 6, 0},
        {0, 146, 255, 255, 255, 255, 146, 0},
        {0, 241, 255, 255, 255, 255, 241, 0},
        {0, 241, 255, 255, 255, 255, 241, 0},
        {0, 146, 255, 255, 255, 255, 146, 0},
        {0, 6, 146, 241, 241, 146, 6, 0},
        {0, 0, 0, 0, 0, 0, 0, 0}
    };
    alignas(32) static u8 pixels[8 * 8 * 4];
    static bool painted = false;
    if (!painted) {
        // GX RGBA8 stores each 4 x 4 tile as an AR plane, then a GB plane.
        // Only the GUI thread draws rings; this fixed 256-byte texture is
        // painted once and can be tinted with a new theme without repainting.
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                const int tile = ((y / 4) * 2 + x / 4) * 64;
                const int texel = ((y % 4) * 4 + x % 4) * 2;
                pixels[tile + texel] = coverage[y][x];
                pixels[tile + texel + 1] = 255;
                pixels[tile + texel + 32] = 255;
                pixels[tile + texel + 33] = 255;
            }
        }
        DCFlushRange(pixels, sizeof(pixels));
        GX_InvalidateTexAll();
        painted = true;
    }
    GXTexObj texture;
    GX_InitTexObj(&texture, pixels, 8, 8, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&texture, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_LoadTexObj(&texture, GX_TEXMAP0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    Mtx view;
    guMtxIdentity(view);
    guMtxTransApply(view, view, 0, 0, -50);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
}

}  // namespace

void DrawPill(float x, float y, float width, int height, GXColor colour) {
    if (width <= 0 || colour.a == 0) return;
    // Small rounded strips need no texture, allocation or libm calls.
    const float radius = std::min(width / 2.0f, height / 2.0f);
    for (int row = 0; row < height; ++row) {
        const float edge = std::min(row + 0.5f, height - row - 0.5f);
        const float t = std::max(0.0f, 1.0f - edge / radius);
        const float inset = radius * t * t;
        Menu_DrawRectangle(x + inset, y + row, width - 2 * inset, 1, colour, 1);
    }
}

void DrawBusyRing(float x, float y, float elapsedMs, int alpha) {
    // A 14 radius: at 10 it read as a speck beside 16-point text on a TV.
    constexpr int offsets[8][2] = {{0, -14}, {10, -10}, {14, 0}, {10, 10},
                                  {0, 14}, {-10, 10}, {-14, 0}, {-10, -10}};
    const float head = elapsedMs / 125.0f;
    const int turns = static_cast<int>(head / 8.0f);
    const float position = head - turns * 8.0f;
    const float fade = transition::Ease(std::min(1.0f, elapsedMs / 150.0f));
    BindBusyDot();
    GX_Begin(GX_QUADS, GX_VTXFMT0, 8 * 4);
    for (int dot = 0; dot < 8; ++dot) {
        float age = position - dot;
        if (age < 0) age += 8.0f;
        // The next dot rises gently as the tail approaches it.
        const float brightness = age > 7.0f ? 0.18f + 0.82f * (age - 7.0f) : 1.0f - 0.82f * age / 7.0f;
        const int opacity = static_cast<int>(alpha * fade * brightness);
        const GXColor colour = skin::WithAlpha(skin::kAccent, opacity);
        const float left = x + offsets[dot][0] - 4;
        const float top = y + offsets[dot][1] - 4;
        GX_Position3f32(left, top, 0);
        GX_Color4u8(colour.r, colour.g, colour.b, colour.a);
        GX_TexCoord2f32(0, 0);
        GX_Position3f32(left + 8, top, 0);
        GX_Color4u8(colour.r, colour.g, colour.b, colour.a);
        GX_TexCoord2f32(1, 0);
        GX_Position3f32(left + 8, top + 8, 0);
        GX_Color4u8(colour.r, colour.g, colour.b, colour.a);
        GX_TexCoord2f32(1, 1);
        GX_Position3f32(left, top + 8, 0);
        GX_Color4u8(colour.r, colour.g, colour.b, colour.a);
        GX_TexCoord2f32(0, 1);
    }
    GX_End();
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
}

void BusyRing::Show(bool active) {
    if (active && !active_) elapsed_ = 0;
    active_ = active;
}

void BusyRing::Draw() {
    if (!active_ || !IsVisible()) return;
    elapsed_ += transition::FrameMilliseconds();
    // Keep sub-frame precision even if a network job stays up for hours.
    if (elapsed_ >= 1150.0f) elapsed_ -= 1000.0f;
    DrawBusyRing(GetLeft(), GetTop(), elapsed_, GetAlpha());
}

}  // namespace riftwii::wii
