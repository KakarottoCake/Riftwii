// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// A small JSON reader for the lists RiftWii downloads (themes.json,
// apps.json): it reads in place from the downloaded text, bounds every
// string and the nesting, and leaves the meaning of each field to its
// list's parser. Not a general JSON library: whole numbers only, and a
// \u surrogate pair becomes '?' (the menu's font has no use for it).
namespace riftwii {

// Nested arrays and objects deeper than this are refused: the reader is
// recursive, and a list that deep is not ours.
constexpr int kJsonMaxDepth = 6;
// A string longer than this is refused outright, whatever field it is in,
// so a skipped value cannot hold the reader on a huge one.
constexpr std::size_t kJsonMaxStringBytes = 1024;

class JsonReader {
public:
    JsonReader(const char* text, std::size_t size) : text_(text), size_(size) {}

    void skip_space() {
        while (at_ < size_ &&
               (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r'))
            ++at_;
    }
    bool at_end() {
        skip_space();
        return at_ >= size_;
    }
    char peek() {
        skip_space();
        return at_ < size_ ? text_[at_] : '\0';
    }
    bool take(char c) {
        if (peek() != c) return false;
        ++at_;
        return true;
    }
    bool word(const char* w) {
        skip_space();
        const std::size_t n = std::strlen(w);
        if (size_ - at_ < n || std::memcmp(text_ + at_, w, n) != 0) return false;
        at_ += n;
        return true;
    }

    // A string, with its escapes resolved to UTF-8 (a \u surrogate pair
    // becomes '?': the menu's font has no use for it).
    // A string longer than kJsonMaxStringBytes is read to its end but not
    // kept: `out` is left empty and last_overlong() says so, so a list can
    // leave out the one entry that holds it instead of the whole list.
    bool string(std::string& out) {
        out.clear();
        overlong_ = false;
        if (!take('"')) return false;
        while (at_ < size_) {
            const unsigned char c = static_cast<unsigned char>(text_[at_++]);
            if (c == '"') {
                if (overlong_) out.clear();
                return true;
            }
            if (out.size() > kJsonMaxStringBytes) {
                overlong_ = true;
                out.clear();
            }
            if (c < 0x20) return false;
            if (c != '\\') {
                if (!overlong_) out += static_cast<char>(c);
                continue;
            }
            if (at_ >= size_) return false;
            const char e = text_[at_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (size_ - at_ < 4) return false;
                unsigned code = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = text_[at_++];
                    code <<= 4;
                    if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                    else return false;
                }
                if (code >= 0xD800 && code <= 0xDFFF) out += '?';
                else append_utf8(out, code);
                break;
            }
            default: return false;
            }
        }
        return false;  // no closing quote
    }

    // A whole number, 0 to 4294967295; `ok` false for a sign, a fraction,
    // an exponent or a bigger value (the field is then left out).
    bool number(std::uint64_t& value, bool& ok) {
        skip_space();
        const std::size_t start = at_;
        value = 0;
        ok = true;
        if (at_ < size_ && text_[at_] == '-') {
            ok = false;
            ++at_;
        }
        const std::size_t digits = at_;
        while (at_ < size_ && text_[at_] >= '0' && text_[at_] <= '9') {
            if (value <= 0xFFFFFFFFull) value = value * 10 + static_cast<unsigned>(text_[at_] - '0');
            ++at_;
        }
        if (at_ == digits) return false;
        if (at_ < size_ && (text_[at_] == '.' || text_[at_] == 'e' || text_[at_] == 'E')) {
            ok = false;
            while (at_ < size_ && (text_[at_] == '.' || text_[at_] == 'e' || text_[at_] == 'E' ||
                                          text_[at_] == '+' || text_[at_] == '-' || (text_[at_] >= '0' && text_[at_] <= '9')))
                ++at_;
        }
        if (value > 0xFFFFFFFFull) ok = false;
        return at_ > start;
    }

    // Any value, left unread.
    bool skip_value(int depth) {
        if (depth > kJsonMaxDepth) return false;
        const char c = peek();
        if (c == '"') {
            std::string ignored;
            return string(ignored);
        }
        if (c == '{') {
            ++at_;
            if (take('}')) return true;
            for (;;) {
                std::string key;
                if (!string(key) || !take(':') || !skip_value(depth + 1)) return false;
                if (take(',')) continue;
                return take('}');
            }
        }
        if (c == '[') {
            ++at_;
            if (take(']')) return true;
            for (;;) {
                if (!skip_value(depth + 1)) return false;
                if (take(',')) continue;
                return take(']');
            }
        }
        if (word("true") || word("false") || word("null")) return true;
        std::uint64_t value;
        bool ok;
        return number(value, ok);
    }

private:
    static void append_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
    }
    const char* text_;
    std::size_t size_;
    std::size_t at_ = 0;
    bool overlong_ = false;

public:
    // Whether the last string() was longer than the reader keeps.
    bool last_overlong() const { return overlong_; }
};

// A name for the menu: printable, nothing that could be taken for layout.
// Control characters turn into spaces; an empty one is refused.
// Well-formed UTF-8: no stray continuation byte, no overlong form, no
// surrogate, nothing past U+10FFFF (how libfat and the font would take
// those is not something to depend on).
inline bool utf8_ok(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t extra;
        unsigned code;
        if (c < 0x80) {
            ++i;
            continue;
        } else if (c >= 0xC2 && c <= 0xDF) {
            extra = 1;
            code = c & 0x1Fu;
        } else if (c >= 0xE0 && c <= 0xEF) {
            extra = 2;
            code = c & 0x0Fu;
        } else if (c >= 0xF0 && c <= 0xF4) {
            extra = 3;
            code = c & 0x07u;
        } else {
            return false;
        }
        if (text.size() - i <= extra) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned char n = static_cast<unsigned char>(text[i + k]);
            if ((n & 0xC0) != 0x80) return false;
            code = (code << 6) | (n & 0x3Fu);
        }
        if ((extra == 2 && code < 0x800) || (extra == 3 && (code < 0x10000 || code > 0x10FFFF)) ||
            (code >= 0xD800 && code <= 0xDFFF))
            return false;
        i += extra + 1;
    }
    return true;
}

inline bool json_clean_text(std::string& text, std::size_t max_bytes) {
    if (!utf8_ok(text)) return false;
    for (char& c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) c = ' ';
    }
    while (!text.empty() && text.back() == ' ') text.pop_back();
    std::size_t lead = 0;
    while (lead < text.size() && text[lead] == ' ') ++lead;
    text.erase(0, lead);
    return !text.empty() && text.size() < max_bytes;
}

// 64 lower-case hex digits, as a list gives a SHA-256.
inline bool sha256_hex_ok(const std::string& text) {
    if (text.size() != 64) return false;
    for (char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

}  // namespace riftwii
