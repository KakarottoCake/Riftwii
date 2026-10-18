// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcbanner.hpp"

#include <algorithm>
#include <cstring>
#include <new>

#include "riftwii/bnr.hpp"
#include "riftwii/fst.hpp"
#include "riftwii/gccard.hpp"
#include "riftwii/tpl.hpp"

namespace riftwii {
namespace {

constexpr const char* kBannerServer = "http://banner.rc24.xyz/";

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

// One NUL-padded field of a record as UTF-8. Line breaks (some long names
// are split over two lines for the GameCube's own menu) become spaces, and
// spaces at the ends or doubled are dropped.
std::string field_text(const std::uint8_t* field, std::size_t length, bool shift_jis) {
    std::string raw;
    for (std::size_t i = 0; i < length && field[i] != 0; ++i) {
        const char c = static_cast<char>(field[i]);
        // A Shift-JIS trail byte is 0x40 or more, so these are never half of a character.
        const bool line_break = c == '\n' || c == '\r' || c == '\t';
        raw += line_break ? ' ' : c;
    }
    const std::string text = gc_card_text_utf8(raw, shift_jis);
    std::string tidy;
    for (const char c : text) {
        const bool doubled_space = c == ' ' && (tidy.empty() || tidy.back() == ' ');
        if (!doubled_space) tidy += c;
    }
    while (!tidy.empty() && tidy.back() == ' ') tidy.pop_back();
    return tidy;
}

LytPane plain_pane(PaneKind kind, const char* name, int parent, float width, float height) {
    LytPane pane;
    pane.kind = kind;
    pane.name = name;
    pane.parent = parent;
    pane.width = width;
    pane.height = height;
    return pane;
}

// A rectangle shaded from `top` to `bottom`, drawn with material 0 (no texture).
LytPane shaded_pane(const char* name, int parent, float width, float height, LytColor top, LytColor bottom) {
    LytPane pane = plain_pane(PaneKind::Picture, name, parent, width, height);
    pane.vertex[0] = pane.vertex[1] = top;
    pane.vertex[2] = pane.vertex[3] = bottom;
    pane.material = 0;
    return pane;
}

int add_pane(Layout& layout, LytPane pane) {
    const int index = static_cast<int>(layout.panes.size());
    if (pane.parent >= 0) layout.panes[static_cast<std::size_t>(pane.parent)].children.push_back(index);
    layout.panes.push_back(std::move(pane));
    return index;
}

}  // namespace

bool is_gc_banner(const std::uint8_t* data, std::size_t size) {
    if (!data || size < 4) return false;
    if (std::memcmp(data, "BNR1", 4) == 0) return size >= kGcBanner1Size;
    if (std::memcmp(data, "BNR2", 4) == 0) return size >= kGcBanner2Size;
    return false;
}

bool parse_gc_banner(const std::uint8_t* data, std::size_t size, bool shift_jis, GcBanner& out, std::string& error) {
    if (!is_gc_banner(data, size)) {
        error = size >= 4 && (std::memcmp(data, "BNR1", 4) == 0 || std::memcmp(data, "BNR2", 4) == 0)
                    ? "the GameCube banner is too short"
                    : "not a GameCube banner (no BNR1 or BNR2)";
        return false;
    }
    GcBanner banner;
    // The picture through the TPL decoder: a TPL image is the same GX
    // texture, here at a fixed place.
    TplImage picture;
    picture.width = kGcBannerWidth;
    picture.height = kGcBannerHeight;
    picture.format = kTplRGB5A3;
    picture.data_offset = kGcBannerPictureOffset;
    picture.data_size = kGcBannerPictureBytes;
    if (!tpl_to_rgba(data, size, picture, banner.rgba, error)) return false;
    const bool european = data[3] == '2';
    const int languages = european ? 6 : 1;
    for (int i = 0; i < languages; ++i) {
        const std::uint8_t* record = data + kGcBannerTextOffset + static_cast<std::size_t>(i) * kGcBannerTextBytes;
        GcBannerText text;
        text.short_name = field_text(record + 0x00, 0x20, shift_jis);
        text.short_maker = field_text(record + 0x20, 0x20, shift_jis);
        text.long_name = field_text(record + 0x40, 0x40, shift_jis);
        text.long_maker = field_text(record + 0x80, 0x40, shift_jis);
        text.description = field_text(record + 0xC0, 0x80, shift_jis);
        banner.texts.push_back(std::move(text));
    }
    out = std::move(banner);
    return true;
}

std::string gc_banner_name(const GcBanner& banner, int wii_language) {
    // BNR2's records in the Wii's language numbering.
    int record = 0;
    switch (wii_language) {
    case kBnrGerman: record = 1; break;
    case kBnrFrench: record = 2; break;
    case kBnrSpanish: record = 3; break;
    case kBnrItalian: record = 4; break;
    case kBnrDutch: record = 5; break;
    default: record = 0; break;
    }
    if (record >= static_cast<int>(banner.texts.size())) record = 0;
    const auto name_of = [](const GcBannerText& t) { return t.long_name.empty() ? t.short_name : t.long_name; };
    if (banner.texts.empty()) return "";
    const std::string name = name_of(banner.texts[static_cast<std::size_t>(record)]);
    return name.empty() ? name_of(banner.texts[0]) : name;
}

bool read_gc_disc_file(const ByteSource& disc, const std::string& path, std::vector<std::uint8_t>& out,
                       std::string& error, std::size_t max_size) {
    std::uint8_t header[0x440];
    if (disc.size() < sizeof header || !disc.read(0, header, sizeof header)) {
        error = "cannot read the disc's header";
        return false;
    }
    const std::uint32_t fst_offset = be32(header + 0x424), fst_size = be32(header + 0x428);
    if (fst_size == 0 || fst_size > (8u << 20) || std::uint64_t(fst_offset) + fst_size > disc.size()) {
        error = "the disc's FST is out of bounds";
        return false;
    }
    std::vector<std::uint8_t> fst_bytes(fst_size);
    if (!disc.read(fst_offset, fst_bytes.data(), fst_bytes.size())) {
        error = "cannot read the disc's FST";
        return false;
    }
    Fst fst;
    if (!Fst::parse(fst_bytes.data(), fst_bytes.size(), false, fst, error)) return false;
    const std::uint32_t index = fst.find(path, true);
    if (index == Fst::npos || fst.entries()[index].is_directory) {
        error = path + " is not on the disc";
        return false;
    }
    const FstEntry& entry = fst.entries()[index];
    if (entry.size > max_size || entry.offset + entry.size > disc.size()) {
        error = path + " is too large or past the image's end";
        return false;
    }
    std::vector<std::uint8_t> bytes(entry.size);
    if (entry.size && !disc.read(entry.offset, bytes.data(), bytes.size())) {
        error = "cannot read " + path;
        return false;
    }
    out.swap(bytes);
    return true;
}

GcPictureColours gc_picture_colours(const std::vector<std::uint8_t>& rgba) {
    GcPictureColours colours;
    const std::size_t texels = static_cast<std::size_t>(kGcBannerWidth) * kGcBannerHeight;
    if (rgba.size() < texels * 4) return colours;

    // The two outermost rows at the top and at the bottom (a picture often
    // has a one-texel outline there, the row inside it is the scene's), or
    // more toward the middle while they hold too few opaque texels: a
    // cut-out logo's edges are mostly transparent.
    constexpr unsigned long kEnoughTexels = kGcBannerWidth / 4;
    constexpr int kFewestRows = 2, kMostRows = 6;
    const auto edge = [&](int first_row, int step, LytColor fallback) {
        unsigned long sum[3] = {0, 0, 0}, count = 0;
        for (int k = 0; k < kMostRows && (k < kFewestRows || count < kEnoughTexels); ++k) {
            const int row = first_row + k * step;
            for (int x = 0; x < kGcBannerWidth; ++x) {
                const std::uint8_t* texel = &rgba[(static_cast<std::size_t>(row) * kGcBannerWidth + x) * 4];
                const bool opaque = texel[3] >= 128;
                if (!opaque) continue;
                for (int c = 0; c < 3; ++c) sum[c] += texel[c];
                ++count;
            }
        }
        if (count == 0) return fallback;
        LytColor average;
        average.r = static_cast<std::uint8_t>(sum[0] / count);
        average.g = static_cast<std::uint8_t>(sum[1] / count);
        average.b = static_cast<std::uint8_t>(sum[2] / count);
        return average;
    };
    colours.top = edge(0, 1, colours.top);
    colours.bottom = edge(kGcBannerHeight - 1, -1, colours.bottom);

    // The accent: the hue the picture has most of. Each opaque texel goes
    // to one of twelve hues, weighted by its chroma squared (max minus min
    // channel), so the vivid texels of a logo on black outweigh the black
    // and the greys, which count for nothing; the heaviest hue's texels
    // are averaged. Averaging every colour instead would mix a logo's
    // purple letters with its green and skin tones into grey.
    constexpr int kHues = 12;
    double sum[kHues][3] = {}, weight[kHues] = {};
    for (std::size_t i = 0; i < texels; ++i) {
        const std::uint8_t* texel = &rgba[i * 4];
        if (texel[3] < 128) continue;
        const int r = texel[0], g = texel[1], b = texel[2];
        const int high = std::max({r, g, b});
        const int low = std::min({r, g, b});
        if (high == low) continue;
        const double chroma = high - low;
        // The hue, 0 to 6: red, yellow, green, cyan, blue, magenta.
        double hue = 0.0;
        if (high == r) hue = (g - b) / chroma;
        else if (high == g) hue = (b - r) / chroma + 2.0;
        else hue = (r - g) / chroma + 4.0;
        if (hue < 0.0) hue += 6.0;
        const int bin = std::min(kHues - 1, static_cast<int>(hue * kHues / 6.0));
        const double w = (chroma / 255.0) * (chroma / 255.0);
        sum[bin][0] += w * r;
        sum[bin][1] += w * g;
        sum[bin][2] += w * b;
        weight[bin] += w;
    }
    int heaviest = 0;
    for (int bin = 1; bin < kHues; ++bin) {
        if (weight[bin] > weight[heaviest]) heaviest = bin;
    }
    // Less than about 40 fully vivid texels' worth: a grey picture.
    constexpr double kColourfulEnough = 40.0;
    const double total = weight[heaviest];
    if (total < kColourfulEnough) return colours;
    const double mean[3] = {sum[heaviest][0] / total, sum[heaviest][1] / total, sum[heaviest][2] / total};
    const double brightest = std::max({mean[0], mean[1], mean[2]});
    if (brightest <= 0.0) return colours;
    // One brightness for every game, so a dark logo tints as much as a light one.
    constexpr double kAccentBrightness = 220.0;
    const auto scaled = [&](double v) { return static_cast<std::uint8_t>(v * kAccentBrightness / brightest + 0.5); };
    colours.accent = LytColor{scaled(mean[0]), scaled(mean[1]), scaled(mean[2]), 255};
    colours.colourful = true;
    return colours;
}

namespace {

// `colour` a share `amount` (0 to 1) of the way to `toward`.
LytColor mix(LytColor colour, LytColor toward, float amount) {
    const auto channel = [amount](std::uint8_t from, std::uint8_t to) {
        return static_cast<std::uint8_t>(from + (to - from) * amount + 0.5f);
    };
    return LytColor{channel(colour.r, toward.r), channel(colour.g, toward.g), channel(colour.b, toward.b), 255};
}

constexpr float kIconWidth = 128.0f, kIconHeight = 96.0f;
// The icon's picture: across the whole channel, 3:1.
constexpr float kIconPictureWidth = kIconWidth, kIconPictureHeight = kIconWidth / 3.0f;
// The channel's tile shows 88 of the icon's 96 lines (wii/menu/gui_gamegrid.cpp):
// the picture never grows taller than that.
constexpr float kIconShownHeight = 88.0f;
// The bands reach this far past the icon's edges, under the channel's rim.
constexpr float kIconOverhang = 8.0f;

// The icon's bands above and below the picture, for a picture `picture_h` tall.
void place_bands(Layout& layout, float picture_h) {
    const float band_h = kIconHeight / 2 + kIconOverhang - picture_h / 2;
    // One unit under the picture, so no seam of the tile's colour shows between them.
    const float overlap = 1.0f;
    const int top = layout.find_pane("band_top"), bottom = layout.find_pane("band_bottom");
    if (top < 0 || bottom < 0) return;
    LytPane& above = layout.panes[static_cast<std::size_t>(top)];
    LytPane& below = layout.panes[static_cast<std::size_t>(bottom)];
    above.height = below.height = band_h + overlap;
    above.ty = picture_h / 2 - overlap + above.height / 2;
    below.ty = -above.ty;
}

Layout gc_icon_layout(Layout layout, const GcPictureColours& colours) {
    const int root = 0;
    // The bands continue the picture's top and bottom edges and darken
    // toward the channel's rim, as a Wii channel's art runs to its edges.
    const LytColor black{0, 0, 0, 255};
    constexpr float kRimDarkening = 0.45f;
    const float band_w = kIconWidth + 2 * kIconOverhang;
    add_pane(layout, shaded_pane("band_top", root, band_w, 1, mix(colours.top, black, kRimDarkening), colours.top));
    add_pane(layout,
             shaded_pane("band_bottom", root, band_w, 1, colours.bottom, mix(colours.bottom, black, kRimDarkening)));
    place_bands(layout, kIconPictureHeight);

    LytPane group = plain_pane(PaneKind::Null, "frame", root, kIconPictureWidth, kIconPictureHeight);
    group.flags = 1 | 4;  // keeps its shape when the channel is wider than 4:3 (gc_picture_fit)
    const int frame = add_pane(layout, group);
    // Behind the picture, for its transparent texels (Luigi's Mansion's
    // picture is a cut-out logo): the bands' colours, joined top to bottom.
    add_pane(layout, shaded_pane("backdrop", frame, kIconPictureWidth, kIconPictureHeight, colours.top, colours.bottom));
    LytPane image = plain_pane(PaneKind::Picture, "picture", frame, kIconPictureWidth, kIconPictureHeight);
    image.material = 1;
    image.uvs.push_back({0, 0, 1, 0, 0, 1, 1, 1});
    add_pane(layout, image);
    return layout;
}

Layout gc_banner_layout(Layout layout, const GcPictureColours& colours) {
    const int root = 0;
    // A light background, a little past the view's edges, shaded downwards
    // as the Wii Menu's own channels are, and tinted with the picture's
    // accent (more toward the bottom) so each game's banner has its colour.
    const LytColor light_top{248, 249, 251, 255}, light_bottom{204, 210, 221, 255};
    const float tint_top = colours.colourful ? 0.18f : 0.0f;
    const float tint_bottom = colours.colourful ? 0.45f : 0.0f;
    add_pane(layout, shaded_pane("background", root, layout.width + 32, layout.height + 32,
                                 mix(light_top, colours.accent, tint_top), mix(light_bottom, colours.accent, tint_bottom)));

    // The picture in its frame, scaled by a whole number (5x): about four
    // fifths of a 4:3 view's width and most of the room above the name,
    // as a Wii banner fills the screen above the buttons.
    const float scale = 5.0f;
    const float picture_w = kGcBannerWidth * scale, picture_h = kGcBannerHeight * scale;
    const float border = 6.0f;
    LytPane group = plain_pane(PaneKind::Null, "frame", root, picture_w, picture_h);
    group.flags = 1 | 4;  // keeps its shape when the menu stretches a banner to 16:9
    // Above the middle, its top clear of the favourite star in the top
    // right corner at 4:3; the menu writes the name under it, clear of
    // the bar along the screen's bottom (kGcBannerNameTop).
    group.ty = 66.0f;
    const int frame = add_pane(layout, group);
    // A soft shadow, as under the menu's cards: layers a little larger
    // each and faint, so it fades out from the frame's edge, a little
    // below it as if lit from above.
    const LytColor shadow{0, 0, 0, 14};
    for (int layer = kGcBannerShadowLayers; layer >= 1; --layer) {
        const float grow = border + 2.0f * static_cast<float>(layer);
        LytPane shade = shaded_pane("shadow", frame, picture_w + 2 * grow, picture_h + 2 * grow, shadow, shadow);
        shade.ty = -3.0f;
        add_pane(layout, shade);
    }
    const LytColor dark_top{58, 62, 72, 255}, dark_bottom{30, 33, 40, 255};
    add_pane(layout, shaded_pane("border", frame, picture_w + 2 * border, picture_h + 2 * border, dark_top, dark_bottom));
    LytPane image = plain_pane(PaneKind::Picture, "picture", frame, picture_w, picture_h);
    image.material = 1;
    image.uvs.push_back({0, 0, 1, 0, 0, 1, 1, 1});
    add_pane(layout, image);
    return layout;
}

}  // namespace

Layout gc_picture_layout(bool icon, const GcPictureColours& colours) {
    Layout layout;
    layout.centered = true;
    layout.width = icon ? kIconWidth : 608.0f;
    layout.height = icon ? kIconHeight : 456.0f;
    layout.textures.push_back(kGcPictureTexture);
    LytMaterial plain;
    plain.name = "plain";
    LytMaterial picture;
    picture.name = "picture";
    LytTexMap map;
    map.texture = 0;
    picture.maps.push_back(map);
    layout.materials.push_back(plain);
    layout.materials.push_back(picture);
    add_pane(layout, plain_pane(PaneKind::Null, "RootPane", -1, layout.width, layout.height));
    return icon ? gc_icon_layout(std::move(layout), colours) : gc_banner_layout(std::move(layout), colours);
}

void gc_picture_fit(Layout& layout, float wide_x) {
    const int frame = layout.find_pane("frame");
    if (frame < 0 || layout.find_pane("band_top") < 0) return;  // the full banner
    // A flagged pane is narrowed by wide_x; scaled up by its inverse, the
    // picture spans the channel's width again, and keeps its shape.
    float grow = wide_x > 0.0f && wide_x < 1.0f ? 1.0f / wide_x : 1.0f;
    const float tallest = kIconShownHeight / kIconPictureHeight;
    if (grow > tallest) grow = tallest;
    LytPane& group = layout.panes[static_cast<std::size_t>(frame)];
    group.sx = group.sy = grow;
    place_bands(layout, kIconPictureHeight * grow);
}

bool gc_banner_id_ok(const std::string& game_id) {
    if (game_id.size() != 6) return false;
    for (const char c : game_id) {
        const bool letter_or_digit = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!letter_or_digit) return false;
    }
    return true;
}

std::vector<std::string> gc_banner_urls(const std::string& game_id, bool regional_fallback) {
    if (!gc_banner_id_ok(game_id)) return {};
    std::vector<std::string> urls = {std::string(kBannerServer) + game_id + ".bnr"};
    if (regional_fallback) urls.push_back(std::string(kBannerServer) + game_id.substr(0, 3) + ".bnr");
    return urls;
}

bool gc_banner_regional_copy(const std::string& game_id, bool patch_mod, const TitleTable* titles) {
    if (patch_mod || !titles || !gc_banner_id_ok(game_id)) return false;
    const std::string* name = titles->find_exact(game_id);
    if (!name) return false;
    // The same game for another region: the region letter (the 4th) changed,
    // the game's three letters and the maker code kept.
    std::string other = game_id;
    for (char region = 'A'; region <= 'Z'; ++region) {
        if (region == game_id[3]) continue;
        other[3] = region;
        const std::string* other_name = titles->find_exact(other);
        if (other_name && *other_name == *name) return true;
    }
    return false;
}

bool check_gc_banner_download(const std::uint8_t* data, std::size_t size, std::string& error) {
    if (size > kGcBannerDownloadMax) {
        error = "the banner is larger than " + std::to_string(kGcBannerDownloadMax) + " bytes";
        return false;
    }
    OpeningBanner banner;
    try {
        if (!parse_opening_bnr(data, size, banner, error, true)) return false;
    } catch (const std::bad_alloc&) {
        error = "out of memory for the banner";
        return false;
    }
    if (banner.banner.empty()) {
        error = "the download has an icon but no banner";
        return false;
    }
    return true;
}

}  // namespace riftwii
