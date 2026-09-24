// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/langfile.hpp"

#include <sstream>

namespace riftwii {
namespace {

// The contents of one "..." string, escapes expanded. False when the line
// holds no string.
bool quoted(const std::string& line, std::size_t from, std::string& out) {
    const std::size_t open = line.find('"', from);
    if (open == std::string::npos) return false;
    out.clear();
    for (std::size_t i = open + 1; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') return true;
        if (c != '\\' || i + 1 >= line.size()) {
            out += c;
            continue;
        }
        const char e = line[++i];
        out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
    }
    return true;  // unterminated: take what is there
}

std::string trim_left(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

}  // namespace

std::size_t parse_po(const std::string& text, Translations& out) {
    enum class Field { None, Id, Str, Skip } field = Field::None;
    std::string id, str;
    bool have_id = false;
    bool context = false;          // the entry has a msgctxt: skipped
    bool context_pending = false;  // msgctxt read, its msgid not yet
    std::size_t added = 0;
    const auto flush = [&]() {
        if (have_id && !context && !id.empty() && !str.empty()) {
            out[id] = str;
            ++added;
        }
        id.clear();
        str.clear();
        have_id = false;
        context = false;
    };
    std::istringstream in(text);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        first = false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        line = trim_left(line);
        if (line.empty() || line[0] == '#') continue;
        std::string part;
        if (line[0] == '"') {
            if (!quoted(line, 0, part)) continue;
            if (field == Field::Id) id += part;
            else if (field == Field::Str) str += part;
            continue;
        }
        if (line.compare(0, 6, "msgid ") == 0) {
            if (!context_pending) flush();
            context_pending = false;
            field = Field::Id;
            have_id = quoted(line, 5, id);
        } else if (line.compare(0, 7, "msgstr ") == 0 && have_id) {
            field = Field::Str;
            quoted(line, 6, str);
        } else {
            // msgctxt, msgid_plural, msgstr[n]: not used here.
            if (line.compare(0, 8, "msgctxt ") == 0) {
                flush();
                context = true;
                context_pending = true;
            }
            field = Field::Skip;
        }
    }
    flush();
    return added;
}

std::string fill_placeholders(const std::string& text, const std::vector<std::string>& args) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            std::size_t j = i + 1;
            std::size_t n = 0;
            while (j < text.size() && text[j] >= '0' && text[j] <= '9') n = n * 10 + static_cast<std::size_t>(text[j++] - '0');
            if (j > i + 1 && j < text.size() && text[j] == '}' && n >= 1 && n <= args.size()) {
                out += args[n - 1];
                i = j;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

std::vector<char32_t> decode_utf8(const std::string& text) {
    std::vector<char32_t> out;
    out.reserve(text.size());
    const auto* s = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t n = text.size();
    for (std::size_t i = 0; i < n;) {
        const unsigned char c = s[i];
        std::size_t len = 0;
        char32_t cp = 0;
        if (c < 0x80) {
            out.push_back(c);
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07;
        }
        bool ok = len != 0 && i + len <= n;
        for (std::size_t k = 1; ok && k < len; ++k) {
            if ((s[i + k] & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        // Overlong forms, surrogates and values past Unicode are not UTF-8.
        static const char32_t kMin[] = {0, 0, 0x80, 0x800, 0x10000};
        if (ok && (cp < kMin[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))) ok = false;
        if (!ok) {
            out.push_back(c);  // Latin-1
            ++i;
            continue;
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

}  // namespace riftwii
