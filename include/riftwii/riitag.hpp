// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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

}  // namespace riftwii
