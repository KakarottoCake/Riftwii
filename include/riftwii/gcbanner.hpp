// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/brlyt.hpp"
#include "riftwii/overlay.hpp"
#include "riftwii/titles.hpp"

// GameCube games in the Channels view.
//
// A GameCube disc's opening.bnr is the old format, from the public format
// pages (YAGCD's "Banner" and wiibrew's Opening.bnr): "BNR1" (one
// language) or "BNR2" (a European disc: English, German, French, Spanish,
// Italian and Dutch), 0x1C bytes of padding, a 96x32 RGB5A3 picture in
// GX's 4x4 tiles at 0x20, then at 0x1820 one 0x140-byte record per
// language: the short name (0x20 bytes), short maker (0x20), long name
// (0x40), long maker (0x40) and description (0x80), each NUL-padded,
// Shift-JIS on a Japanese disc and Windows-1252 elsewhere.
//
// The community has made Wii-format animated banners (IMET + U8, as a Wii
// disc's) for many GameCube games, and RiiConnect24 hosts them by game ID.
// What the server sends is untrusted: it is stored only when it parses as a
// whole banner (riftwii/bnr.hpp's parse_opening_bnr) within a size bound.
namespace riftwii {

constexpr int kGcBannerWidth = 96, kGcBannerHeight = 32;
constexpr std::size_t kGcBannerPictureOffset = 0x20;
constexpr std::size_t kGcBannerPictureBytes = 96 * 32 * 2;  // RGB5A3, two bytes a texel
constexpr std::size_t kGcBannerTextOffset = 0x1820;
constexpr std::size_t kGcBannerTextBytes = 0x140;
constexpr std::size_t kGcBanner1Size = kGcBannerTextOffset + kGcBannerTextBytes;      // 0x1960
constexpr std::size_t kGcBanner2Size = kGcBannerTextOffset + 6 * kGcBannerTextBytes;  // 0x1FA0

// One language's record, as UTF-8 (a line break in the disc's text becomes
// a space).
struct GcBannerText {
    std::string short_name, short_maker, long_name, long_maker, description;
};

struct GcBanner {
    std::vector<std::uint8_t> rgba;  // 96 x 32 x 4, straight alpha
    std::vector<GcBannerText> texts;  // BNR1: one; BNR2: English, German, French, Spanish, Italian, Dutch
};

// Whether the bytes start with BNR1 or BNR2 and are long enough for it.
bool is_gc_banner(const std::uint8_t* data, std::size_t size);

// `shift_jis`: the disc is Japanese (riftwii/gccard.hpp's gc_card_shift_jis).
bool parse_gc_banner(const std::uint8_t* data, std::size_t size, bool shift_jis, GcBanner& out, std::string& error);

// The game's name for the Wii's language (riftwii/bnr.hpp's BannerLanguage):
// its long name, else its short one, in that language when the banner has
// it and in its first language otherwise. Empty when it gives none.
std::string gc_banner_name(const GcBanner& banner, int wii_language);

// A file from a GameCube disc image (its FST: header offsets at 0x424 and
// 0x428, file offsets in bytes), at most `max_size` bytes.
bool read_gc_disc_file(const ByteSource& disc, const std::string& path, std::vector<std::uint8_t>& out,
                       std::string& error, std::size_t max_size = 64u << 10);

// The colours RiftWii's drawing of a game's banner takes from its picture
// (GcBanner::rgba), so it reads as that game's channel and not as a
// thumbnail on a grey card.
struct GcPictureColours {
    // The picture's top and bottom rows, averaged over their opaque
    // texels: the icon's bands above and below the picture continue them.
    // Dark grey for a picture with no opaque edge.
    LytColor top{40, 44, 52, 255}, bottom{40, 44, 52, 255};
    // Its most colourful hue, brought to one brightness (Eclipse's teal),
    // which tints the full banner's background. `colourful` is false for a
    // grey picture, whose banner stays grey.
    LytColor accent{128, 128, 128, 255};
    bool colourful = false;
};
GcPictureColours gc_picture_colours(const std::vector<std::uint8_t>& rgba);

// The banner RiftWii draws for a GameCube game the server has no animated
// banner for. `icon`: the Channels tile's 128x96, the picture across its
// whole width (3:1, so 128 x 43) and the bands above and below it shaded
// from the picture's own top and bottom edges toward a darker rim, as a
// Wii channel fills its tile. Else the full 608x456 banner: the picture
// scaled up 5x in a dark frame with a soft shadow in its upper part, where
// the menu writes the game's name under it (from kGcBannerNameTop), on a
// light background tinted with the picture's accent. The picture is
// texture 0, named kGcPictureTexture; it keeps its shape on a 16:9 menu.
constexpr const char* kGcPictureTexture = "gcpicture";
// Where the name under the full banner's frame starts: this many of the
// banner's 456 units below its middle (the frame and its shadow end above).
constexpr float kGcBannerNameTop = 34.0f;
// The frame's soft shadow: this many faint layers, each a little larger.
constexpr int kGcBannerShadowLayers = 4;
Layout gc_picture_layout(bool icon, const GcPictureColours& colours = GcPictureColours());

// The icon's picture fitted to the channel it is drawn in. `wide_x` is the
// narrowing layout_quads gets for the box (riftwii/brlyt.hpp; below 1 for
// a widescreen menu's channel, wider than 4:3): the picture keeps its
// shape, so it is scaled up by the inverse to still span the channel's
// width, and the bands are cut to the room left above and below it. With
// `wide_x` 1 (a 4:3 channel) the icon is as gc_picture_layout made it.
// Leaves the full banner's layout alone.
void gc_picture_fit(Layout& layout, float wide_x);

// The banner server's addresses for a game, in the order to ask: its full
// ID, then, with `regional_fallback`, its first three letters (the server
// keys most banners by those, one for every region). Empty when the ID is
// not six capital letters or digits, so nothing else ever becomes part of
// a URL.
bool gc_banner_id_ok(const std::string& game_id);
std::vector<std::string> gc_banner_urls(const std::string& game_id, bool regional_fallback);

// Whether the server's three-letter banner may stand in for the game's:
// for a regional copy of a game only, never for a mod, which would show
// the game it was made from (Super Mario Eclipse, GMSE04, would get
// Sunshine's GMS.bnr). A game is a regional copy when it is not a
// patch-only mod (`patch_mod`) and the title database names it exactly as
// it names another region's copy with the same maker code: GLMP01 and
// GLME01 are both "Luigi's Mansion". GameTDB lists mods under their own
// IDs too, but GMSE04 is "Super Mario Eclipse" and no other region has a
// GMS?04. With no database, no game is one.
bool gc_banner_regional_copy(const std::string& game_id, bool patch_mod, const TitleTable* titles);

// The largest banner the download takes (the server's are up to ~1.2 MB).
constexpr std::size_t kGcBannerDownloadMax = 2u << 20;

// Whether a download is a banner the Channels view can play: at most
// kGcBannerDownloadMax bytes, an IMET header, and an icon and a banner that
// unpack. An HTML page, a truncated or broken archive fails, with `error`.
bool check_gc_banner_download(const std::uint8_t* data, std::size_t size, std::string& error);

}  // namespace riftwii
