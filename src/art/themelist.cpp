// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/themelist.hpp"

#include <cstring>

#include "riftwii/themepack.hpp"

namespace riftwii {
namespace {

// Nested arrays and objects deeper than this are refused: the reader is
// recursive, and a list that deep is not ours.
constexpr int kMaxDepth = 6;
// A string longer than this is refused outright, whatever field it is in,
// so a skipped value cannot hold the reader on a huge one.
constexpr std::size_t kMaxStringBytes = 1024;

class Reader {
public:
    Reader(const char* text, std::size_t size) : text_(text), size_(size) {}

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
    bool string(std::string& out) {
        out.clear();
        if (!take('"')) return false;
        while (at_ < size_) {
            const unsigned char c = static_cast<unsigned char>(text_[at_++]);
            if (c == '"') return true;
            if (out.size() > kMaxStringBytes) return false;
            if (c < 0x20) return false;
            if (c != '\\') {
                out += static_cast<char>(c);
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
        if (depth > kMaxDepth) return false;
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
};

// The fields of one theme object, as read, before they are checked.
struct RawTheme {
    std::string folder, name, author, license, sha256, zip, preview;
    std::uint64_t size = 0, files = 0, unpacked_size = 0;
    bool have_size = false, have_files = false, have_unpacked = false, has_music = false;
    bool malformed = false;  // a field of the wrong kind or too long
};

// A name for the menu: printable, nothing that could be taken for layout.
// Control characters turn into spaces; an empty one is refused.
bool clean_text(std::string& text, std::size_t max_bytes) {
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

bool hex_digest_ok(const std::string& text) {
    if (text.size() != 64) return false;
    for (char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool read_theme(Reader& r, RawTheme& t, bool& fatal) {
    if (!r.take('{')) return false;
    if (r.take('}')) return true;
    for (;;) {
        std::string key;
        if (!r.string(key) || !r.take(':')) return false;
        const char next = r.peek();
        const auto text = [&](std::string& into) {
            if (next != '"') {
                t.malformed = true;
                return r.skip_value(1);
            }
            return r.string(into);
        };
        const auto whole = [&](std::uint64_t& into, bool& have) {
            std::uint64_t value;
            bool ok;
            if (next == '"' || next == '{' || next == '[' || next == 't' || next == 'f' || next == 'n') {
                t.malformed = true;
                return r.skip_value(1);
            }
            if (!r.number(value, ok)) return false;
            if (!ok) t.malformed = true;
            into = value;
            have = ok;
            return true;
        };
        bool good;
        if (key == "folder") good = text(t.folder);
        else if (key == "name") good = text(t.name);
        else if (key == "author") good = text(t.author);
        else if (key == "license") good = text(t.license);
        else if (key == "sha256") good = text(t.sha256);
        else if (key == "zip") good = text(t.zip);
        else if (key == "preview") good = text(t.preview);
        else if (key == "size") good = whole(t.size, t.have_size);
        else if (key == "files") good = whole(t.files, t.have_files);
        else if (key == "unpacked_size") good = whole(t.unpacked_size, t.have_unpacked);
        else if (key == "has_music") {
            if (r.word("true")) {
                t.has_music = true;
                good = true;
            } else if (r.word("false")) {
                good = true;
            } else {
                t.malformed = true;
                good = r.skip_value(1);
            }
        } else good = r.skip_value(1);
        if (!good) {
            fatal = true;  // a string too long or broken text: the reader cannot go on
            return false;
        }
        if (r.take(',')) continue;
        return r.take('}');
    }
}

void copy_field(char* to, std::size_t room, const std::string& from) { std::memcpy(to, from.c_str(), from.size() + 1 <= room ? from.size() + 1 : 0); }

}  // namespace

bool theme_folder_ok(const std::string& folder) { return folder.size() < 48 && theme_pack_name_ok(folder, false); }

bool theme_url_ok(const std::string& url) {
    const std::string prefix = kThemeSiteUrl;
    if (url.size() <= prefix.size() || url.size() >= 200 || url.compare(0, prefix.size(), prefix) != 0) return false;
    for (std::size_t i = prefix.size(); i < url.size(); ++i) {
        const char c = url[i];
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
                           c == '_' || c == '.' || c == '~' || c == '/' || c == '%' || c == '+';
        if (!plain) return false;
    }
    const std::string path = url.substr(prefix.size());
    // No part of the path climbs: "..", whole or at either end.
    if (path == ".." || path.compare(0, 3, "../") == 0 || path.find("/../") != std::string::npos ||
        (path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0))
        return false;
    return true;
}

bool parse_theme_list(const char* text, std::size_t text_size, ThemeListing* out, std::size_t max,
                      std::size_t& count, std::size_t& skipped, std::string& error) {
    count = 0;
    skipped = 0;
    if (text_size > kThemeListMaxBytes) {
        error = "the theme list is bigger than RiftWii takes (256 KB)";
        return false;
    }
    if (max > kThemeListMaxThemes) max = kThemeListMaxThemes;
    const auto bad = [&] {
        error = "the theme list is not in a form RiftWii reads";
        return false;
    };
    Reader r(text, text_size);
    if (!r.take('{')) return bad();
    bool have_schema = false, have_themes = false;
    if (!r.take('}')) {
        for (;;) {
            std::string key;
            if (!r.string(key) || !r.take(':')) return bad();
            if (key == "schema") {
                std::uint64_t value;
                bool ok;
                if (!r.number(value, ok) || !ok || value != 1) {
                    error = "the theme list is from a newer RiftWii than this one (schema is not 1)";
                    return false;
                }
                have_schema = true;
            } else if (key == "themes") {
                if (!r.take('[')) return bad();
                have_themes = true;
                count = 0;
                skipped = 0;
                if (!r.take(']')) {
                    for (;;) {
                        RawTheme t;
                        bool fatal = false;
                        if (r.peek() != '{') return bad();
                        if (!read_theme(r, t, fatal)) return bad();
                        ThemeListing listing;
                        std::memset(&listing, 0, sizeof(listing));
                        const bool fits = clean_text(t.name, sizeof(listing.name)) &&
                                          clean_text(t.author, sizeof(listing.author)) &&
                                          clean_text(t.license, sizeof(listing.license));
                        const bool good = !t.malformed && fits && theme_folder_ok(t.folder) && hex_digest_ok(t.sha256) &&
                                          theme_url_ok(t.zip) && theme_url_ok(t.preview) && t.have_size &&
                                          t.have_files && t.have_unpacked && t.size > 0 && t.size <= kThemeZipMaxBytes &&
                                          t.files > 0 && t.files <= kThemeMaxFiles && t.unpacked_size > 0 &&
                                          t.unpacked_size <= kThemeMaxUnpackedBytes;
                        if (!good) {
                            ++skipped;
                        } else if (count >= max) {
                            ++skipped;
                        } else {
                            copy_field(listing.folder, sizeof(listing.folder), t.folder);
                            copy_field(listing.name, sizeof(listing.name), t.name);
                            copy_field(listing.author, sizeof(listing.author), t.author);
                            copy_field(listing.license, sizeof(listing.license), t.license);
                            copy_field(listing.sha256, sizeof(listing.sha256), t.sha256);
                            copy_field(listing.zip, sizeof(listing.zip), t.zip);
                            copy_field(listing.preview, sizeof(listing.preview), t.preview);
                            listing.size = static_cast<std::uint32_t>(t.size);
                            listing.files = static_cast<std::uint32_t>(t.files);
                            listing.unpacked_size = static_cast<std::uint32_t>(t.unpacked_size);
                            listing.has_music = t.has_music;
                            // One folder twice: the first stands.
                            bool again = false;
                            for (std::size_t i = 0; i < count; ++i) {
                                if (t.folder == out[i].folder) again = true;
                            }
                            if (again) ++skipped;
                            else out[count++] = listing;
                        }
                        if (r.take(',')) continue;
                        if (!r.take(']')) return bad();
                        break;
                    }
                }
            } else if (!r.skip_value(1)) {
                return bad();
            }
            if (r.take(',')) continue;
            if (!r.take('}')) return bad();
            break;
        }
    }
    if (!r.at_end()) return bad();
    if (!have_schema) {
        error = "the theme list says nothing about its schema";
        return false;
    }
    if (!have_themes) return bad();
    return true;
}

namespace {

// One line of the records: "<folder> <sha256>".
bool record_line(const std::string& line, std::string& folder, std::string& sha256) {
    const std::size_t space = line.find(' ');
    if (space == std::string::npos) return false;
    folder = line.substr(0, space);
    sha256 = line.substr(space + 1);
    return theme_folder_ok(folder) && hex_digest_ok(sha256);
}

}  // namespace

bool parse_theme_list(const std::string& text, ThemeListing* out, std::size_t max, std::size_t& count,
                      std::size_t& skipped, std::string& error) {
    return parse_theme_list(text.data(), text.size(), out, max, count, skipped, error);
}

ThemeInstall theme_install_state(bool folder_there, bool recorded, const std::string& recorded_sha,
                                const std::string& listed_sha) {
    if (!folder_there) return ThemeInstall::No;
    if (!recorded) return ThemeInstall::Other;
    return recorded_sha == listed_sha ? ThemeInstall::Current : ThemeInstall::Update;
}

std::string theme_record_without(const std::string& records, const std::string& folder) {
    std::string out;
    std::size_t at = 0;
    while (at < records.size()) {
        std::size_t end = records.find('\n', at);
        if (end == std::string::npos) end = records.size();
        std::string line = records.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string name, sha;
        if (!record_line(line, name, sha) || name == folder) continue;
        out += line + "\n";
    }
    return out;
}

std::string theme_record_text(const std::string& records, const std::string& folder, const std::string& sha256) {
    return theme_record_without(records, folder) + folder + " " + sha256 + "\n";
}

bool theme_record_find(const std::string& records, const std::string& folder, std::string& sha256) {
    std::size_t at = 0;
    while (at < records.size()) {
        std::size_t end = records.find('\n', at);
        if (end == std::string::npos) end = records.size();
        std::string line = records.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string name, sha;
        if (record_line(line, name, sha) && name == folder) {
            sha256 = sha;
            return true;
        }
    }
    return false;
}

}  // namespace riftwii
