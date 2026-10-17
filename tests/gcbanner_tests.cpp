// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// GameCube games in the Channels view: the disc's BNR1/BNR2 banner read
// from a made-up file (its picture and names), a file found on a made-up
// GameCube disc, the banner RiftWii draws in place of an animated one, the
// banner server's addresses, and what the download check refuses.
#include "riftwii/gcbanner.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/bnr.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using Bytes = std::vector<std::uint8_t>;

namespace {

void put16(Bytes& b, std::size_t at, std::uint16_t v) {
    if (b.size() < at + 2) b.resize(at + 2);
    b[at] = static_cast<std::uint8_t>(v >> 8);
    b[at + 1] = static_cast<std::uint8_t>(v);
}
void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    put16(b, at, static_cast<std::uint16_t>(v >> 16));
    put16(b, at + 2, static_cast<std::uint16_t>(v));
}
void put_text(Bytes& b, std::size_t at, const std::string& s) {
    if (b.size() < at + s.size()) b.resize(at + s.size());
    std::memcpy(b.data() + at, s.data(), s.size());
}

// Where texel (x, y) of the 96x32 picture lies: 4x4 tiles, left to right
// then down, two bytes a texel.
std::size_t texel_at(int x, int y) {
    const int tile = (y / 4) * (kGcBannerWidth / 4) + x / 4;
    const int inside = (y % 4) * 4 + x % 4;
    return kGcBannerPictureOffset + static_cast<std::size_t>(tile) * 32 + static_cast<std::size_t>(inside) * 2;
}

// A record's fields for language `index`.
void put_record(Bytes& b, int index, const std::string& short_name, const std::string& long_name,
                const std::string& long_maker) {
    const std::size_t at = kGcBannerTextOffset + static_cast<std::size_t>(index) * kGcBannerTextBytes;
    put_text(b, at, short_name);
    put_text(b, at + 0x40, long_name);
    put_text(b, at + 0x80, long_maker);
}

Bytes make_bnr2() {
    Bytes b(kGcBanner2Size, 0);
    put_text(b, 0, "BNR2");
    put16(b, texel_at(0, 0), 0xFFFF);                  // opaque white
    put16(b, texel_at(4, 0), 0x8000 | (31 << 10));      // opaque red, in the second tile
    put16(b, texel_at(1, 5), 0x0F00);                   // alpha mode: transparent, red
    put16(b, texel_at(95, 31), 0x8000 | 31);            // opaque blue, the last texel
    put_record(b, 0, "Sunshine", "Super Mario\nSunshine", "Nintendo");
    put_record(b, 1, "Sunshine", "Super Mario  Sunshine DE", "Nintendo");
    put_record(b, 2, "Court", "", "");  // French: a short name only
    put_record(b, 3, "", "", "");       // Spanish: nothing at all
    put_record(b, 4, "Pok\xE9", "Pok\xE9mon Colosseum", "");
    return b;
}

// A U8 archive holding `files` ({"dir/name", bytes}, one directory each, in order).
Bytes make_u8(const std::vector<std::pair<std::string, Bytes>>& files) {
    struct Node {
        bool dir;
        std::string name;
        std::size_t file;
        std::uint32_t next;
    };
    std::vector<Node> nodes = {{true, "", 0, 0}};
    std::string last_dir;
    std::size_t dir_index = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const std::string& path = files[i].first;
        const std::size_t slash = path.find('/');
        const std::string dir = path.substr(0, slash);
        if (dir != last_dir) {
            if (dir_index) nodes[dir_index].next = static_cast<std::uint32_t>(nodes.size());
            dir_index = nodes.size();
            nodes.push_back({true, dir, 0, 0});
            last_dir = dir;
        }
        nodes.push_back({false, path.substr(slash + 1), i, 0});
    }
    if (dir_index) nodes[dir_index].next = static_cast<std::uint32_t>(nodes.size());
    nodes[0].next = static_cast<std::uint32_t>(nodes.size());
    std::string strings(1, '\0');
    std::vector<std::uint32_t> name_offsets;
    for (const Node& n : nodes) {
        name_offsets.push_back(n.name.empty() ? 0 : static_cast<std::uint32_t>(strings.size()));
        if (!n.name.empty()) strings += n.name + '\0';
    }
    const std::uint32_t header = static_cast<std::uint32_t>(nodes.size() * 12 + strings.size());
    const std::uint32_t data = (0x20 + header + 0x3F) & ~0x3Fu;
    Bytes out(data, 0);
    put32(out, 0, 0x55AA382D);
    put32(out, 4, 0x20);
    put32(out, 8, header);
    put32(out, 12, data);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const std::size_t at = 0x20 + i * 12;
        put32(out, at, (nodes[i].dir ? 0x01000000u : 0) | name_offsets[i]);
        if (nodes[i].dir) {
            put32(out, at + 4, 0);
            put32(out, at + 8, nodes[i].next);
        } else {
            const Bytes& f = files[nodes[i].file].second;
            put32(out, at + 4, static_cast<std::uint32_t>(out.size()));
            put32(out, at + 8, static_cast<std::uint32_t>(f.size()));
            out.insert(out.end(), f.begin(), f.end());
            while (out.size() % 32) out.push_back(0);
        }
    }
    std::memcpy(out.data() + 0x20 + nodes.size() * 12, strings.data(), strings.size());
    return out;
}

// A Wii-format banner as the server sends one: 0x40 zero bytes, IMET, then
// the U8 archive of meta/*.bin, each an IMD5 header over a (here
// uncompressed) U8 archive.
Bytes make_imet_banner(bool with_banner) {
    const auto bin = [](const std::string& tpl) {
        Bytes b(0x20, 0);
        put_text(b, 0, "IMD5");
        const Bytes inner = make_u8({{"arc/" + tpl, {1, 2, 3, 4}}});
        b.insert(b.end(), inner.begin(), inner.end());
        return b;
    };
    Bytes out(0x600, 0);
    put_text(out, 0x40, "IMET");
    std::vector<std::pair<std::string, Bytes>> meta;
    if (with_banner) meta.push_back({"meta/banner.bin", bin("banner.tpl")});
    meta.push_back({"meta/icon.bin", bin("icon.tpl")});
    const Bytes archive = make_u8(meta);
    out.insert(out.end(), archive.begin(), archive.end());
    return out;
}

class MemorySource final : public ByteSource {
public:
    explicit MemorySource(const Bytes& bytes) : bytes_(bytes) {}
    std::uint64_t size() const override { return bytes_.size(); }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override {
        if (offset > bytes_.size() || length > bytes_.size() - offset) return false;
        std::memcpy(destination, bytes_.data() + offset, length);
        return true;
    }

private:
    const Bytes& bytes_;
};

}  // namespace

static void test_parse_bnr2() {
    const Bytes file = make_bnr2();
    EXPECT_TRUE(is_gc_banner(file.data(), file.size()));
    GcBanner banner;
    std::string error;
    EXPECT_TRUE(parse_gc_banner(file.data(), file.size(), false, banner, error));
    EXPECT_EQ(banner.rgba.size(), std::size_t(96 * 32 * 4));
    EXPECT_EQ(banner.texts.size(), std::size_t(6));
    const auto pixel = [&](int x, int y, int channel) {
        return static_cast<int>(banner.rgba[(static_cast<std::size_t>(y) * 96 + x) * 4 + channel]);
    };
    EXPECT_EQ(pixel(0, 0, 0), 255);
    EXPECT_EQ(pixel(0, 0, 3), 255);
    EXPECT_EQ(pixel(4, 0, 0), 255);
    EXPECT_EQ(pixel(4, 0, 1), 0);
    EXPECT_EQ(pixel(4, 0, 3), 255);
    EXPECT_EQ(pixel(1, 5, 0), 255);
    EXPECT_EQ(pixel(1, 5, 3), 0);
    EXPECT_EQ(pixel(95, 31, 2), 255);
    EXPECT_EQ(pixel(95, 31, 0), 0);
    // The line break is a space; doubled spaces go.
    EXPECT_EQ(banner.texts[0].long_name, std::string("Super Mario Sunshine"));
    EXPECT_EQ(banner.texts[0].long_maker, std::string("Nintendo"));
    EXPECT_EQ(gc_banner_name(banner, kBnrEnglish), std::string("Super Mario Sunshine"));
    EXPECT_EQ(gc_banner_name(banner, kBnrGerman), std::string("Super Mario Sunshine DE"));
    EXPECT_EQ(gc_banner_name(banner, kBnrFrench), std::string("Court"));             // the short name
    EXPECT_EQ(gc_banner_name(banner, kBnrSpanish), std::string("Super Mario Sunshine"));  // English's
    EXPECT_EQ(gc_banner_name(banner, kBnrItalian), std::string("Pok\xC3\xA9mon Colosseum"));  // Windows-1252
    EXPECT_EQ(gc_banner_name(banner, kBnrJapanese), std::string("Super Mario Sunshine"));
    EXPECT_EQ(gc_banner_name(banner, kBnrKorean), std::string("Super Mario Sunshine"));
}

static void test_parse_bnr1() {
    Bytes file(kGcBanner1Size, 0);
    put_text(file, 0, "BNR1");
    put_record(file, 0, "\x83\x7D\x83\x8A\x83\x49", "", "");  // "マリオ" in Shift-JIS
    GcBanner banner;
    std::string error;
    EXPECT_TRUE(parse_gc_banner(file.data(), file.size(), true, banner, error));
    EXPECT_EQ(banner.texts.size(), std::size_t(1));
    EXPECT_EQ(gc_banner_name(banner, kBnrJapanese), std::string("\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA"));
    EXPECT_EQ(gc_banner_name(banner, kBnrGerman), std::string("\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA"));
    // The same bytes read as Windows-1252 are not taken as Japanese.
    EXPECT_TRUE(parse_gc_banner(file.data(), file.size(), false, banner, error));
    EXPECT_TRUE(gc_banner_name(banner, kBnrJapanese) != std::string("\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA"));
}

static void test_bad_bnr() {
    GcBanner banner;
    std::string error;
    // BNR2 cut to BNR1's length, BNR1 cut short, another magic, nothing.
    Bytes file = make_bnr2();
    file.resize(kGcBanner1Size);
    EXPECT_FALSE(is_gc_banner(file.data(), file.size()));
    EXPECT_FALSE(parse_gc_banner(file.data(), file.size(), false, banner, error));
    EXPECT_EQ(error, std::string("the GameCube banner is too short"));
    file[3] = '1';
    file.resize(kGcBanner1Size - 1);
    EXPECT_FALSE(parse_gc_banner(file.data(), file.size(), false, banner, error));
    file.assign(kGcBanner2Size, 0);
    put_text(file, 0, "BNR3");
    EXPECT_FALSE(parse_gc_banner(file.data(), file.size(), false, banner, error));
    EXPECT_FALSE(parse_gc_banner(nullptr, 0, false, banner, error));
    // A Wii banner is not a GameCube one.
    const Bytes wii = make_imet_banner(true);
    EXPECT_FALSE(is_gc_banner(wii.data(), wii.size()));
}

static void test_disc_file() {
    // A disc: header, an FST at 0x2000 (root, opening.bnr, a folder with a
    // file), the banner at 0x8000.
    const Bytes bnr = make_bnr2();
    Bytes disc(0x8000, 0);
    put_text(disc, 0, "GMSE01");
    put32(disc, 0x1C, 0xC2339F3D);
    const std::string strings = std::string("opening.bnr") + '\0' + "data" + '\0' + "x.bin" + '\0';
    Bytes fst(4 * 12, 0);
    put32(fst, 0, 0x01000000);
    put32(fst, 8, 4);
    put32(fst, 12, 0);  // opening.bnr
    put32(fst, 16, 0x8000);
    put32(fst, 20, static_cast<std::uint32_t>(bnr.size()));
    put32(fst, 24, 0x01000000 | 12);  // data/
    put32(fst, 28, 0);
    put32(fst, 32, 4);
    put32(fst, 36, 17);  // data/x.bin, past the image's end
    put32(fst, 40, 0x10000);
    put32(fst, 44, 16);
    fst.insert(fst.end(), strings.begin(), strings.end());
    std::memcpy(disc.data() + 0x2000, fst.data(), fst.size());
    put32(disc, 0x424, 0x2000);
    put32(disc, 0x428, static_cast<std::uint32_t>(fst.size()));
    disc.insert(disc.end(), bnr.begin(), bnr.end());
    const MemorySource source(disc);
    Bytes out;
    std::string error;
    EXPECT_TRUE(read_gc_disc_file(source, "/opening.bnr", out, error));
    EXPECT_TRUE(out == bnr);
    EXPECT_TRUE(read_gc_disc_file(source, "/OPENING.BNR", out, error));
    EXPECT_FALSE(read_gc_disc_file(source, "/missing.bnr", out, error));
    EXPECT_FALSE(read_gc_disc_file(source, "/data", out, error));          // a folder
    EXPECT_FALSE(read_gc_disc_file(source, "/data/x.bin", out, error));    // past the end
    EXPECT_FALSE(read_gc_disc_file(source, "/opening.bnr", out, error, 0x100));  // over the limit
    // An FST said to run past the image.
    Bytes broken = disc;
    put32(broken, 0x428, 0x100000);
    const MemorySource broken_source(broken);
    EXPECT_FALSE(read_gc_disc_file(broken_source, "/opening.bnr", out, error));
    const Bytes tiny(0x100, 0);
    const MemorySource tiny_source(tiny);
    EXPECT_FALSE(read_gc_disc_file(tiny_source, "/opening.bnr", out, error));
}

namespace {

// A 96x32 picture, straight RGBA, every texel `fill`.
Bytes make_rgba(LytColor fill) {
    Bytes rgba(static_cast<std::size_t>(kGcBannerWidth) * kGcBannerHeight * 4);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = fill.r;
        rgba[i + 1] = fill.g;
        rgba[i + 2] = fill.b;
        rgba[i + 3] = fill.a;
    }
    return rgba;
}

void set_texel(Bytes& rgba, int x, int y, LytColor c) {
    const std::size_t at = (static_cast<std::size_t>(y) * kGcBannerWidth + x) * 4;
    rgba[at] = c.r;
    rgba[at + 1] = c.g;
    rgba[at + 2] = c.b;
    rgba[at + 3] = c.a;
}

bool same_colour(LytColor a, LytColor b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

const LytQuad* quad_of(const Layout& layout, const std::vector<LytQuad>& quads, const char* name) {
    for (const LytQuad& q : quads) {
        if (layout.panes[static_cast<std::size_t>(q.pane)].name == name) return &q;
    }
    return nullptr;
}

}  // namespace

static void test_picture_colours() {
    // Black with a teal logo, a red top row and a blue bottom row; the rows
    // next to them half transparent, so only the outermost rows count.
    Bytes rgba = make_rgba({0, 0, 0, 255});
    for (int x = 0; x < kGcBannerWidth; ++x) {
        set_texel(rgba, x, 0, {200, 0, 0, 255});
        set_texel(rgba, x, 1, {0, 0, 0, 0});
        set_texel(rgba, x, kGcBannerHeight - 2, {0, 0, 0, 0});
        set_texel(rgba, x, kGcBannerHeight - 1, {0, 0, 120, 255});
    }
    for (int y = 4; y < 28; ++y) {
        for (int x = 0; x < kGcBannerWidth; ++x) set_texel(rgba, x, y, {0, 110, 100, 255});
    }
    GcPictureColours colours = gc_picture_colours(rgba);
    EXPECT_TRUE(same_colour(colours.top, {200, 0, 0, 255}));
    EXPECT_TRUE(same_colour(colours.bottom, {0, 0, 120, 255}));
    // The teal logo outweighs the black and the thin red and blue rows,
    // brought to the accent's brightness (220 in its brightest channel).
    EXPECT_TRUE(colours.colourful);
    EXPECT_TRUE(colours.accent.g == 220 && colours.accent.b > 170 && colours.accent.r < 60);

    // Purple letters beside a smaller green figure and some skin tones:
    // the hue there is most of, not a grey mixed from all three.
    Bytes logo = make_rgba({20, 20, 30, 255});
    for (int y = 4; y < 28; ++y) {
        for (int x = 0; x < 12; ++x) set_texel(logo, x, y, {41, 172, 0, 255});
        for (int x = 12; x < 20; ++x) set_texel(logo, x, y, {230, 170, 120, 255});
        for (int x = 30; x < 90; ++x) set_texel(logo, x, y, {131, 131, 255, 255});
    }
    colours = gc_picture_colours(logo);
    EXPECT_TRUE(colours.colourful);
    EXPECT_TRUE(colours.accent.b == 220 && colours.accent.r == colours.accent.g && colours.accent.r < 130);

    // A cut-out logo, its outer rows transparent but for a few texels: the
    // rows further in give the edge colour.
    Bytes cut_out = make_rgba({0, 0, 0, 0});
    set_texel(cut_out, 10, 0, {255, 255, 255, 255});
    for (int x = 0; x < kGcBannerWidth; ++x) set_texel(cut_out, x, 3, {0, 0, 90, 255});
    colours = gc_picture_colours(cut_out);
    EXPECT_TRUE(colours.top.b > 80 && colours.top.r < 10);
    EXPECT_TRUE(same_colour(colours.bottom, GcPictureColours().bottom));  // nothing opaque within six rows

    // A grey picture: no accent; its edges are its grey.
    colours = gc_picture_colours(make_rgba({90, 90, 90, 255}));
    EXPECT_FALSE(colours.colourful);
    EXPECT_TRUE(same_colour(colours.top, {90, 90, 90, 255}));
    // All transparent, or no picture at all: the defaults.
    const GcPictureColours defaults;
    colours = gc_picture_colours(make_rgba({255, 0, 0, 0}));
    EXPECT_FALSE(colours.colourful);
    EXPECT_TRUE(same_colour(colours.top, defaults.top) && same_colour(colours.bottom, defaults.bottom));
    colours = gc_picture_colours(Bytes(16, 255));
    EXPECT_FALSE(colours.colourful);
}

static void test_icon_layout() {
    GcPictureColours colours;
    colours.top = {200, 0, 0, 255};
    colours.bottom = {0, 0, 120, 255};
    Layout layout = gc_picture_layout(true, colours);
    EXPECT_EQ(layout.textures.size(), std::size_t(1));
    EXPECT_EQ(layout.textures[0], std::string(kGcPictureTexture));
    EXPECT_EQ(layout.width, 128.0f);
    EXPECT_EQ(layout.height, 96.0f);
    // On a 4:3 channel (and fitted with 1, the same): the picture spans the
    // icon's whole width, 3:1, in its middle, the bands meeting it.
    for (const bool fitted : {false, true}) {
        if (fitted) gc_picture_fit(layout, 1.0f);
        const std::vector<LytQuad> quads = layout_quads(layout);
        const LytQuad* picture = quad_of(layout, quads, "picture");
        const LytQuad* top = quad_of(layout, quads, "band_top");
        const LytQuad* bottom = quad_of(layout, quads, "band_bottom");
        EXPECT_TRUE(picture && top && bottom);
        if (!picture || !top || !bottom) return;
        EXPECT_EQ(quads.back().pane, picture->pane);  // drawn last, over the bands
        EXPECT_EQ(layout.panes[static_cast<std::size_t>(picture->pane)].material, 1);
        EXPECT_EQ(picture->x[0], -64.0f);
        EXPECT_EQ(picture->x[1], 64.0f);
        EXPECT_EQ(picture->y[0], 64.0f / 3);
        EXPECT_EQ(picture->y[2], -64.0f / 3);
        // The bands cover the rest of the icon, past its edges, and run a
        // little under the picture so no gap shows.
        EXPECT_TRUE(top->y[0] >= 48.0f && top->y[2] < picture->y[0]);
        EXPECT_TRUE(bottom->y[2] <= -48.0f && bottom->y[0] > picture->y[2]);
        EXPECT_TRUE(top->x[0] <= -64.0f && top->x[1] >= 64.0f);
        // From the picture's edge colour at the picture to darker at the rim.
        EXPECT_TRUE(same_colour(top->color[2], colours.top));
        EXPECT_TRUE(top->color[0].r < colours.top.r && top->color[0].r > 0);
        EXPECT_TRUE(same_colour(bottom->color[0], colours.bottom));
        EXPECT_TRUE(bottom->color[2].b < colours.bottom.b);
    }

    // A 16:9 menu's channel (layout_quads narrows flagged panes by 0.75 and
    // the menu stretches the whole icon by 4/3): fitted, the picture still
    // spans the channel and keeps its shape, 128 x 57 as the TV shows it.
    Layout wide_layout = gc_picture_layout(true, colours);
    gc_picture_fit(wide_layout, 0.75f);
    std::vector<LytQuad> wide;
    LytScratch scratch;
    layout_quads(wide_layout, wide, scratch, 0.75f);
    const LytQuad* picture = quad_of(wide_layout, wide, "picture");
    const LytQuad* top = quad_of(wide_layout, wide, "band_top");
    const LytQuad* bottom = quad_of(wide_layout, wide, "band_bottom");
    EXPECT_TRUE(picture && top && bottom);
    if (!picture || !top || !bottom) return;
    const float tall = 128.0f / 3 / 0.75f;
    EXPECT_TRUE(std::fabs(picture->x[1] - picture->x[0] - 128.0f) < 0.01f);
    EXPECT_TRUE(std::fabs(picture->y[0] - picture->y[2] - tall) < 0.01f);
    EXPECT_TRUE(top->y[2] < picture->y[0] && top->y[0] >= 48.0f);
    EXPECT_TRUE(bottom->y[0] > picture->y[2] && bottom->y[2] <= -48.0f);
    // Fitting again for another box starts from the layout, not from the last fit.
    gc_picture_fit(wide_layout, 1.0f);
    const std::vector<LytQuad> back = layout_quads(wide_layout);
    EXPECT_EQ(quad_of(wide_layout, back, "picture")->y[0], 64.0f / 3);
    // A box far wider than 16:9: the picture stops at the 88 lines the channel shows.
    gc_picture_fit(wide_layout, 0.25f);
    std::vector<LytQuad> very_wide;
    layout_quads(wide_layout, very_wide, scratch, 0.25f);
    picture = quad_of(wide_layout, very_wide, "picture");
    EXPECT_TRUE(picture && std::fabs(picture->y[0] - picture->y[2] - 88.0f) < 0.01f);
}

static void test_banner_layout() {
    for (const bool colourful : {false, true}) {
        GcPictureColours colours;
        colours.accent = {0, 220, 200, 255};
        colours.colourful = colourful;
        Layout layout = gc_picture_layout(false, colours);
        // The full banner is not an icon: fitting leaves it alone.
        gc_picture_fit(layout, 0.75f);
        const std::vector<LytQuad> quads = layout_quads(layout);
        // The background, the frame's shadow layers and border, the picture last.
        const std::size_t count = 3 + kGcBannerShadowLayers;
        EXPECT_EQ(quads.size(), count);
        if (quads.size() != count) continue;
        const LytQuad& background = quads[0];
        const LytQuad& border = quads[count - 2];
        const LytQuad& picture = quads[count - 1];
        EXPECT_EQ(layout.panes[static_cast<std::size_t>(picture.pane)].material, 1);
        EXPECT_EQ(picture.x[1] - picture.x[0], 480.0f);
        EXPECT_EQ(picture.y[0] - picture.y[2], 160.0f);
        EXPECT_EQ(picture.y[0], 66.0f + 80.0f);
        // The frame stays inside the view and ends above where the name
        // starts, with room for the shadow.
        EXPECT_TRUE(border.x[0] > -304.0f && border.x[1] < 304.0f && border.y[0] < 228.0f);
        EXPECT_TRUE(border.y[2] - 11.0f > -kGcBannerNameTop);
        // The shadow's layers, largest first, fade out past the border.
        for (std::size_t i = 1; i + 2 < count; ++i) {
            EXPECT_TRUE(quads[i].x[0] < border.x[0]);
            EXPECT_TRUE(quads[i].color[0].a < 32);
        }
        // The background covers the whole view: light grey, tinted toward
        // the accent (more at the bottom) when the picture has one.
        EXPECT_TRUE(background.x[0] <= -304.0f && background.x[1] >= 304.0f);
        EXPECT_TRUE(background.y[0] >= 228.0f && background.y[2] <= -228.0f);
        const LytColor top = background.color[0], bottom = background.color[2];
        if (colourful) {
            EXPECT_TRUE(top.r < 248 && top.g > top.r && top.r > 200);
            EXPECT_TRUE(bottom.g > bottom.r + 60);
        } else {
            EXPECT_TRUE(same_colour(top, {248, 249, 251, 255}));
            EXPECT_TRUE(same_colour(bottom, {204, 210, 221, 255}));
        }
        // A 16:9 menu narrows the framed picture back, not the background.
        std::vector<LytQuad> wide;
        LytScratch scratch;
        layout_quads(layout, wide, scratch, 0.75f);
        EXPECT_EQ(wide[count - 1].x[1] - wide[count - 1].x[0], 480.0f * 0.75f);
        EXPECT_EQ(wide[0].x[1] - wide[0].x[0], quads[0].x[1] - quads[0].x[0]);
    }
}

static void test_urls() {
    const std::vector<std::string> urls = gc_banner_urls("GMSE01", true);
    EXPECT_EQ(urls.size(), std::size_t(2));
    if (urls.size() == 2) {
        EXPECT_EQ(urls[0], std::string("http://banner.rc24.xyz/GMSE01.bnr"));
        EXPECT_EQ(urls[1], std::string("http://banner.rc24.xyz/GMS.bnr"));
    }
    // A mod: its own ID only.
    const std::vector<std::string> mod = gc_banner_urls("GMSE04", false);
    EXPECT_EQ(mod.size(), std::size_t(1));
    if (!mod.empty()) EXPECT_EQ(mod[0], std::string("http://banner.rc24.xyz/GMSE04.bnr"));
    EXPECT_EQ(gc_banner_urls("GLMP01", true).size(), std::size_t(2));
    EXPECT_TRUE(gc_banner_id_ok("G8ME01"));
    // Only six capital letters or digits: nothing else reaches a URL.
    for (const char* bad : {"gmse01", "GMSE0", "GMSE01X", "GMS/01", "../GMS", "GMS?01", "GMS 01", "GMS%2F", ""}) {
        EXPECT_FALSE(gc_banner_id_ok(bad));
        EXPECT_TRUE(gc_banner_urls(bad, true).empty());
        EXPECT_TRUE(gc_banner_urls(bad, false).empty());
    }
    EXPECT_FALSE(gc_banner_id_ok(std::string("GMS\0E1", 6)));
}

static void test_regional_copy() {
    // As GameTDB's titles.txt has them: retail copies and the mods the
    // community lists under IDs of their own.
    TitleTable titles;
    titles.add_text("TITLES = https://www.gametdb.com\n"
                    "GMSE01 = Super Mario Sunshine\n"
                    "GMSP01 = Super Mario Sunshine\n"
                    "GMSJ01 = Super Mario Sunshine\n"
                    "GMSE02 = Super Mario Sunshine Multiplayer\n"
                    "GMSE04 = Super Mario Eclipse\n"
                    "GLME01 = Luigi's Mansion\n"
                    "GLMP01 = Luigi's Mansion\n"
                    "GLME09 = Luigi's Mansion: Open World\n"
                    "GZLE01 = The Legend of Zelda: The Wind Waker\n"
                    "GZLJ01 = Kaze no Takuto\n"
                    "GALE = Super Smash Bros. Melee\n"
                    "GALP01 = Super Smash Bros. Melee\n");
    EXPECT_TRUE(gc_banner_regional_copy("GMSE01", false, &titles));
    EXPECT_TRUE(gc_banner_regional_copy("GMSP01", false, &titles));
    EXPECT_TRUE(gc_banner_regional_copy("GLMP01", false, &titles));
    // Mods: listed, but no other region's copy under that maker code and name.
    EXPECT_FALSE(gc_banner_regional_copy("GMSE04", false, &titles));
    EXPECT_FALSE(gc_banner_regional_copy("GMSE02", false, &titles));
    EXPECT_FALSE(gc_banner_regional_copy("GLME09", false, &titles));
    // Not listed at all; listed only by its 4-char prefix (GALE): exact IDs only.
    EXPECT_FALSE(gc_banner_regional_copy("GMSE07", false, &titles));
    EXPECT_FALSE(gc_banner_regional_copy("GALE01", false, &titles));
    // Named differently in each region: its own banner only (the safe side).
    EXPECT_FALSE(gc_banner_regional_copy("GZLE01", false, &titles));
    // A patch-only mod is a mod whatever its ID; no database, no fallback.
    EXPECT_FALSE(gc_banner_regional_copy("GMSE01", true, &titles));
    EXPECT_FALSE(gc_banner_regional_copy("GMSE01", false, nullptr));
    EXPECT_FALSE(gc_banner_regional_copy("gmse01", false, &titles));
}

static void test_download_check() {
    std::string error;
    const Bytes good = make_imet_banner(true);
    EXPECT_TRUE(check_gc_banner_download(good.data(), good.size(), error));
    // An icon without the banner a channel opens to.
    const Bytes icon_only = make_imet_banner(false);
    EXPECT_FALSE(check_gc_banner_download(icon_only.data(), icon_only.size(), error));
    // A web page in place of the banner (an error page answered with 200).
    const std::string page = "<!DOCTYPE html><html><body>Not here</body></html>" + std::string(0x800, ' ');
    EXPECT_FALSE(check_gc_banner_download(reinterpret_cast<const std::uint8_t*>(page.data()), page.size(), error));
    // Cut short anywhere: in IMET's header, in the archive's table, in a file.
    for (const std::size_t cut : {std::size_t(0x50), std::size_t(0x610), good.size() - 8}) {
        EXPECT_FALSE(check_gc_banner_download(good.data(), cut, error));
    }
    // A GameCube disc's own banner is not what the server should send.
    const Bytes bnr = make_bnr2();
    EXPECT_FALSE(check_gc_banner_download(bnr.data(), bnr.size(), error));
    // Too big, whatever it holds.
    Bytes big = good;
    big.resize(kGcBannerDownloadMax + 1, 0);
    EXPECT_FALSE(check_gc_banner_download(big.data(), big.size(), error));
    big.resize(kGcBannerDownloadMax);
    EXPECT_TRUE(check_gc_banner_download(big.data(), big.size(), error));
    EXPECT_FALSE(check_gc_banner_download(nullptr, 0, error));
}

int main() {
    test_parse_bnr2();
    test_parse_bnr1();
    test_bad_bnr();
    test_disc_file();
    test_picture_colours();
    test_icon_layout();
    test_banner_layout();
    test_urls();
    test_regional_copy();
    test_download_check();
    if (g_failures == 0) std::cout << "gcbanner: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
