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

}  // namespace riftwii
