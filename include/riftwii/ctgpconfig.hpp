// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace riftwii {

// Adds "key = value" to `section` of an INI text ("[section]" headers,
// "key = value" lines, "#" comments) when that section has no such key
// yet. A key the player already set, to any value, is left alone, and so
// is every other line. Returns true when the text changed.
bool ini_add_default(std::string& text, const std::string& section, const std::string& key,
                     const std::string& value);

// CTGP Revolution's channel (sd:/ctgpr/config.ini, read from its
// [exploit] section): RiftWii starts it under a d2x cIOS, where CTGP's own
// IOS exploit can wait forever for hardware access. Adds
// "disable_ios_exploit = yes" unless the player set that key. Returns true
// when the text changed.
bool ctgp_config_defaults(std::string& text);

// Sets "key = value" in `section` of an INI text: the first such line's
// value is replaced (its indentation, the line break and every other line
// kept), or the line is added as ini_add_default would. `old_value`, when
// given, gets the value that was there ("" when the key was missing).
// Returns true when the text changed.
bool ini_set(std::string& text, const std::string& section, const std::string& key, const std::string& value,
             std::string* old_value = nullptr);

// CTGP Revolution's channel started on a d2x virtual disc: "[sd]
// compatible_mode = yes" makes the channel reload, before the game, into
// the IOS it started on (the d2x cIOS) instead of the game's own IOS
// (IOS36 for Mario Kart Wii). The channel checks the IOS version right
// after that reload and, when it differs, tries again; d2x redirects only
// the first try, so the retry lands in the real IOS36 and the virtual disc
// is gone ("Please insert the Mario Kart Wii game disc"). The channel's
// own default writes "compatible_mode = no", so the value is set, not
// only added. Returns true when the text changed.
bool ctgp_channel_disc_config(std::string& text, std::string* old_value = nullptr);

// The same launch: "[exploit] disable_patches = yes". Without it the
// channel, when it has hardware access, rewrites every match of nine short
// byte patterns (ES patches meant for Nintendo's IOS, some only four bytes
// long) across the whole IOS area of MEM2, which also holds d2x's own
// modules, and then tests the result with a fakesigned ES_AddTitleStart.
// All of that happens before the channel's first IOS reload, the one that
// lost the virtual disc on a real Wii. d2x already allows what those
// patches are for. Set, not only added (the channel writes "= no").
// Returns true when the text changed.
bool ctgp_channel_disc_patches(std::string& text, std::string* old_value = nullptr);

}  // namespace riftwii
