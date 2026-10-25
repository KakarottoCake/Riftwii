// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2011 Dimok
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// RiiTag (https://riitag.t0g3pii.de): a player's tag shows the game they
// are playing. A loader tells the server at each launch with one GET of
// the player's URL, the game ID and their key filled in. USB Loader GX
// calls it Wiinnertag and keeps the URLs in Wiinnertag.xml:
//
//   <Tag URL="https://riitag.t0g3pii.de/wii?game={ID6}&amp;key={KEY}" Key="..."/>
//
// one <Tag> per server. RiftWii reads that file as it is, or a key alone
// (riitag_key in settings.txt) for the default server.
//
// The tag itself is a picture anyone can fetch by the player's Discord ID
// (riitag_id in settings.txt): https://riitag.t0g3pii.de/<id>/tag.max.png,
// 1200 x 450. Settings > Online > My RiiTag shows it.
namespace riftwii {

constexpr const char* kRiiTagUrl = "https://riitag.t0g3pii.de/wii?game={ID6}&key={KEY}";

struct RiiTagEntry {
    std::string url;  // with {ID6} and {KEY} in it
    std::string key;
};

// The <Tag> elements of a Wiinnertag.xml's text. Tags without both a URL
// and a key, or with a URL that is not http(s), are left out.
std::vector<RiiTagEntry> parse_wiinnertag(const std::string& xml);

// A key as settings.txt takes it: 1 to 128 printable characters, no spaces.
bool valid_riitag_key(const std::string& key);

// The URL to GET for one entry and game: {ID6} and {KEY} filled in, the
// key percent-encoded. Empty when the game ID is not 4 or 6 letters and
// digits.
std::string riitag_url(const RiiTagEntry& entry, const std::string& game_id);

// A Discord ID as settings.txt takes it: a snowflake, 15 to 20 digits.
bool valid_riitag_id(const std::string& id);

// The URL of the player's tag picture, at its largest; empty when `id` is
// not a valid Discord ID.
std::string riitag_image_url(const std::string& id);

// How large the tag's download and picture may be. tag.max.png is 1200 x
// 450 and a few hundred KB; a server answering with more is refused.
constexpr std::size_t kRiiTagImageMaxBytes = 3u << 19;  // 1.5 MB
constexpr std::uint32_t kRiiTagImageMaxSide = 1200;

// The tag as the menu shows it: 608 x 228, 1200 x 450's own 8:3 shape
// (GX's multiples of 4, inside a 640-wide screen). A tag of another shape
// is fitted whole, never stretched (fit_rgba_into).
constexpr int kRiiTagShownWidth = 608;
constexpr int kRiiTagShownHeight = 228;

// An RGBA picture (rows, 4 bytes a pixel) fitted whole into the `bw` x `bh`
// RGBA rows at `out`, centred, the rest transparent. Each pixel of `out`
// averages the source pixels it covers (alpha-weighted), so a tag halved
// keeps its thin text readable. Nothing is taken from the heap: the tag is
// shrunk on the network's thread, where MEM1 is scarce.
void fit_rgba_into(const std::uint8_t* rgba, int w, int h, std::uint8_t* out, int bw, int bh);

}  // namespace riftwii
