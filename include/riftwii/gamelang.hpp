// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The game language: a game asks the SDK's SCGetLanguage for the
// console's language setting. Its code is recognisable, so the value it
// returns can be replaced by a fixed one, as USB Loader GX's language
// patch does: the load of the setting (lbz r3,8(r1)) becomes li r3,<code>.
// A game without that language may stop: Super Mario Galaxy 2 (US)
// told German panics looking for files its disc does not have.
namespace riftwii {

// The console's language codes (CONF_LANG_*): 0 Japanese ... 9 Korean.
constexpr int kGameLanguages = 10;

// "console" (-1: the console's setting), "ja", "en", "de", "fr", "es",
// "it", "nl", "zh-hans", "zh-hant", "ko".
const char* game_language_name(int code);
bool parse_game_language(const std::string& s, int& code);

// Patches every SCGetLanguage in `bytes`. Returns how many it changed.
unsigned patch_game_language(std::uint8_t* bytes, std::size_t size, int code);

}  // namespace riftwii
