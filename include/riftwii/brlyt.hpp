// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Banner layouts (arc/blyt/*.brlyt), from the public format page
// https://mkwiiki.org/wiki/BRLYT_(File_Format): a tree of panes (pictures,
// text boxes, windows, bounding boxes and empty "null" panes that only
// move their children), materials that say how each picture is coloured,
// and the names of the textures they use. Big-endian throughout.
//
// layout_quads turns a layout into flat quads, in drawing order, with
// their corners already moved, turned and scaled through every parent:
// the Wii draws them with GX and the host tool rasterises them for its
// previews, so both draw the same thing.
namespace riftwii {

struct LytColor {
    std::uint8_t r = 255, g = 255, b = 255, a = 255;
};

struct LytTexMap {
    std::uint16_t texture = 0;  // index in Layout::textures
    std::uint8_t wrap_s = 0, wrap_t = 0;  // 0 clamp, 1 repeat, 2 mirror
    std::uint8_t min_filter = 0, mag_filter = 0;  // 0 linear
};

struct LytTexSrt {
    float tx = 0, ty = 0, rotate = 0, sx = 1, sy = 1;
};

struct LytMaterial {
    std::string name;
    // What a texel's black and white become (the format page calls them
    // fore and back; real files hold black first: 0,0,0,0 then 255s).
    std::int16_t black[4] = {0, 0, 0, 0};
    std::int16_t white[4] = {255, 255, 255, 255};
    std::int16_t color3[4] = {255, 255, 255, 255};
    LytColor tev_k[4];
    std::vector<LytTexMap> maps;
    std::vector<LytTexSrt> srts;
    std::vector<std::array<std::uint8_t, 4>> texgens;  // matrix type, source, matrix
    bool has_channel = false;
    std::uint8_t color_source = 1, alpha_source = 1;  // 0 the material colour, 1 the vertex colours
    bool has_material_color = false;
    LytColor material_color;
    std::vector<std::array<std::uint8_t, 16>> tev;  // as stored (drawn by wii/bannerplay.cpp)
    bool has_swap = false;
    std::array<std::uint8_t, 4> swap{};  // the four swap tables, a byte each: a|b|g|r, two bits each
    std::uint8_t ind_stages = 0;  // indirect texture stages (not drawn)
    bool has_alpha_compare = false;
    std::array<std::uint8_t, 4> alpha_compare{};
    bool has_blend = false;
    std::array<std::uint8_t, 4> blend{};  // function, source, destination, logic op
};

enum class PaneKind : std::uint8_t { Null, Picture, Text, Window, Bound };

struct LytPane {
    PaneKind kind = PaneKind::Null;
    std::string name;
    std::uint8_t flags = 1;  // bit 0 visible, bit 1 children take its alpha, bit 2 keeps its shape on 16:9
    std::uint8_t origin = 4;  // 0..8: left/centre/right across, top/centre/bottom down
    std::uint8_t alpha = 255;
    float tx = 0, ty = 0, tz = 0;
    float rx = 0, ry = 0, rz = 0;  // degrees
    float sx = 1, sy = 1;
    float width = 0, height = 0;
    // Pictures and windows' content:
    LytColor vertex[4];  // top left, top right, bottom left, bottom right
    int material = -1;
    std::vector<std::array<float, 8>> uvs;  // TL, TR, BL, BR (u, v) pairs
    int parent = -1;
    std::vector<int> children;
    bool visible() const { return flags & 1; }
};

struct LytGroup {
    std::string name;
    std::vector<std::string> panes;
};

struct Layout {
    bool centered = true;
    float width = 0, height = 0;
    std::vector<std::string> textures;  // TPL names, as in arc/timg
    std::vector<std::string> fonts;
    std::vector<LytMaterial> materials;
    std::vector<LytPane> panes;  // in file order; panes[0] is the root
    std::vector<LytGroup> groups;
    int find_pane(const std::string& name) const;
};

bool parse_brlyt(const std::uint8_t* data, std::size_t size, Layout& out, std::string& error);

// Banners made for several languages keep each one's text in a group
// named after it (JPN, ENG, GER, FRA, SPA, ITA, NED, ...). Shows the group
// for `language` (one of those codes), or else English's, or else the
// first, and hides the other languages' panes. Layouts without language
// groups are left as they are.
void show_language(Layout& layout, const std::string& language);

// The same choice worked out once, for a layout drawn every frame: the
// panes to show and to hide (indices), applied without searching again.
struct LanguagePanes {
    std::vector<int> show, hide;
};
LanguagePanes language_panes(const Layout& layout, const std::string& language);
void apply_language(Layout& layout, const LanguagePanes& panes);

// One picture to draw: its corners in layout units (x right, y up, the
// layout's centre at 0, 0), their colours with every alpha applied, and
// its material and UV sets.
struct LytQuad {
    int pane = -1;
    float x[4] = {}, y[4] = {};  // TL, TR, BL, BR
    LytColor color[4];
    int material = -1;
    const std::vector<std::array<float, 8>>* uvs = nullptr;
};

std::vector<LytQuad> layout_quads(const Layout& layout);

// On a 16:9 TV the Wii Menu stretches a layout across the wider screen,
// and a pane with flag bit 2 (and everything under it) is narrowed back so
// it keeps its shape: logos, characters, rings. `wide_x` is that narrowing
// (3/4 for 16:9), 1 for a 4:3 picture.

// The same into `out`, reusing it and `scratch` from frame to frame, so a
// layout drawn every frame allocates nothing once they have grown.
struct LytScratch {
    std::vector<std::array<float, 12>> world;
    std::vector<float> alpha;
    std::vector<std::uint8_t> shown;
    std::vector<std::uint8_t> narrowed;  // under a pane narrowed for 16:9 already
};
void layout_quads(const Layout& layout, std::vector<LytQuad>& out, LytScratch& scratch, float wide_x = 1.0f);

// A material's colour for one texel (straight alpha, 0..255 each) and the
// colour the quad brings at that point (its vertex colour), the way
// RiftWii draws it: the texture's black-to-white mapped onto the
// material's black and white colours, times the rasterised colour (the
// vertex colours, or the material colour when its channel control says
// so). No texture: the white colour. A material with a second texture
// uses it as an alpha mask (banners pair a colour picture, "_C", with an
// intensity one, "_A"): its texel's alpha (an intensity texture's own
// level) scales the result's.
LytColor shade_texel(const LytMaterial& material, const LytColor* texel, LytColor raster,
                     const LytColor* mask = nullptr);

}  // namespace riftwii
