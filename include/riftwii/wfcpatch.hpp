// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Online play after Nintendo Wi-Fi Connection: a game can be pointed at a
// replacement server. The patches that work on any game's loaded code are
// here; the Wii side (wii/wfc.cpp) adds Mario Kart Wii's Wiimmfi patch and
// WiiLink WFC's hook. Reimplemented from USB Loader GX's gamepatches.c
// (PrivateServerPatcher, domainpatcher, do_new_wiimmfi_nonMKWii by
// Leseratte of the Wiimmfi team), which credits ToadKing's
// wiilauncher-nossl for the first two.
namespace riftwii {

enum class WfcServer { Off, Wiimmfi, WiiLink, AltWfc, Custom };

// "off", "wiimmfi", "wiilink", "altwfc", "custom".
const char* to_string(WfcServer s);
bool parse_wfc_server(const std::string& s, WfcServer& out);
// The domain that replaces "nintendowifi.net" (empty for Off and WiiLink,
// which needs no domain patch); `custom` for Custom.
std::string wfc_domain(WfcServer s, const std::string& custom);
// A custom domain must fit where "nintendowifi.net" was: 4 to 16 letters,
// digits, dots and hyphens.
bool valid_wfc_domain(const std::string& domain);

// Every "https://..." string becomes "http://..." (the text moves one
// byte left inside its own terminator). Returns how many.
unsigned patch_https_to_http(std::uint8_t* bytes, std::size_t size);
// Every "nintendowifi.net" in a string becomes `domain`, the rest of the
// string following it and the end padded with zeros. Returns how many.
unsigned patch_wfc_domain(std::uint8_t* bytes, std::size_t size, const std::string& domain);

// Wiimmfi's patch for games other than Mario Kart Wii: its User-Agent
// mark, and for games with the GT2 receive bug the fix in its P2P and
// MASTER code. 0 when done, 1 when the game has that bug more than once
// (nothing patched), 2 when the bug's code was not found.
int patch_wiimmfi_generic(std::uint8_t* bytes, std::size_t size);

}  // namespace riftwii
