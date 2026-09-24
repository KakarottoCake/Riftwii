// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <string>
#include <vector>

// The menu's translations: gettext .po files (one per language, English
// text as the msgid), read without gettext itself. Only what a translator
// writes is understood: comments, msgid/msgstr with "..." strings
// continued on following lines, and the escapes \n \t \" \\. An entry
// with an empty msgstr is left untranslated; msgctxt and plural forms are
// not used by RiftWii and are skipped.
namespace riftwii {

using Translations = std::map<std::string, std::string>;

// Adds the entries of `text` to `out` (later files win). Returns how many
// were added.
std::size_t parse_po(const std::string& text, Translations& out);

// Replaces {1}, {2}... in `text` with `args` (missing ones stay as they are).
std::string fill_placeholders(const std::string& text, const std::vector<std::string>& args);

// Decodes UTF-8 into code points. A byte that does not start a valid
// sequence is taken as Latin-1, so older text files still show.
std::vector<char32_t> decode_utf8(const std::string& text);

}  // namespace riftwii
