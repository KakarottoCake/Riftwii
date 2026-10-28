// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/emulatorart.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/homebrewmatch.hpp"
#include "riftwii/http.hpp"

namespace riftwii {
namespace {

bool png_name(const std::string& name) {
    return emulator_art_component(name) && name.size() > 4 &&
           name.compare(name.size() - 4, 4, ".png") == 0;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool decode_link(std::string_view link, std::string& name) {
    name.clear();
    if (link.size() > kEmulatorArtNameMax * 3) return false;
    for (std::size_t i = 0; i < link.size(); ++i) {
        char c = link[i];
        if (c == '%') {
            if (i + 2 >= link.size()) return false;
            const int high = hex_digit(link[i + 1]);
            const int low = hex_digit(link[i + 2]);
            if (high < 0 || low < 0) return false;
            c = static_cast<char>(high * 16 + low);
            i += 2;
        } else if (c == '&') {
            if (link.substr(i, 5) != "&amp;") return false;
            c = '&';
            i += 4;
        }
        if (name.size() == kEmulatorArtNameMax) return false;
        name += c;
    }
    return png_name(name);
}

std::string digits(const std::string& key) {
    std::string result;
    for (char c : key) {
        if (c >= '0' && c <= '9') result += c;
    }
    return result;
}

std::string title_bytes(const std::string& name) {
    std::string result;
    unsigned brackets = 0;
    for (const unsigned char c : name) {
        if (c == '(' || c == '[') ++brackets;
        else if (c == ')' || c == ']') {
            if (brackets) --brackets;
        } else if (!brackets) {
            if (c >= 'A' && c <= 'Z') result += static_cast<char>(c - 'A' + 'a');
            else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 128)
                result += static_cast<char>(c);
        }
    }
    return result;
}

std::string roman_sequels(const std::string& name) {
    static constexpr std::string_view numerals[] = {
        "i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii", "xiii", "xiv", "xv", "xvi"};
    std::string result, word;
    unsigned brackets = 0;
    const auto finish_word = [&]() {
        for (const auto numeral : numerals) {
            if (word == numeral) result += word + ",";
        }
        word.clear();
    };
    for (const unsigned char c : name) {
        if (c == '(' || c == '[') {
            finish_word();
            ++brackets;
        } else if (c == ')' || c == ']') {
            if (brackets) --brackets;
        } else if (!brackets) {
            if (c >= 'A' && c <= 'Z') word += static_cast<char>(c - 'A' + 'a');
            else if (c >= 'a' && c <= 'z') word += static_cast<char>(c);
            else finish_word();
        }
    }
    finish_word();
    return result;
}

int region_rank(const std::string& name, const std::string& game) {
    static constexpr const char* regions[] = {"USA", "Europe", "Japan", "World", "UK", "France", "Germany"};
    int fallback = 20;
    for (int i = 0; i < 7; ++i) {
        // Multi-region tags, e.g. (USA, Japan), count too.
        const auto has_region = [&](const std::string& text) {
            std::size_t at = 0;
            while ((at = text.find('(', at)) != std::string::npos) {
                const auto end = text.find(')', ++at);
                if (end == std::string::npos) return false;
                const std::string tag = text.substr(at, end - at);
                std::size_t word = 0;
                while (word < tag.size()) {
                    const auto comma = tag.find(',', word);
                    std::string part = tag.substr(word, comma == std::string::npos ? comma : comma - word);
                    while (!part.empty() && part.front() == ' ') part.erase(0, 1);
                    while (!part.empty() && part.back() == ' ') part.pop_back();
                    if (part == regions[i]) return true;
                    if (comma == std::string::npos) break;
                    word = comma + 1;
                }
                at = end + 1;
            }
            return false;
        };
        if (has_region(name)) {
            if (has_region(game)) return i;
            fallback = std::min(fallback, i + 10);
        }
    }
    return fallback;
}

// Streaming JSON validation: no tree, bounded nesting, strings and tokens.
// Views point into the caller's bounded response and allocate no input-sized
// objects. Known fields are read only after the whole document is valid.
class Json {
public:
    explicit Json(std::string_view text) : text_(text) {}

    bool whole() {
        if (text_.size() > kSteamGridJsonMax || !value(0)) return false;
        space();
        return at_ == text_.size();
    }

    bool string(std::string& out) {
        space();
        if (!take('"')) return false;
        out.clear();
        while (at_ < text_.size()) {
            unsigned char c = text_[at_++];
            if (c == '"') return out.size() <= 2048;
            if (c < 32 || out.size() >= 2048) return false;
            if (c != '\\') {
                out += static_cast<char>(c);
                continue;
            }
            if (at_ == text_.size()) return false;
            c = text_[at_++];
            if (c == '"' || c == '\\' || c == '/') out += static_cast<char>(c);
            else if (c == 'b') out += '\b';
            else if (c == 'f') out += '\f';
            else if (c == 'n') out += '\n';
            else if (c == 'r') out += '\r';
            else if (c == 't') out += '\t';
            else if (c == 'u') {
                unsigned code = 0;
                if (!unicode(code)) return false;
                if (code >= 0xD800 && code <= 0xDBFF) {
                    if (!take('\\') || !take('u')) return false;
                    unsigned low = 0;
                    if (!unicode(low) || low < 0xDC00 || low > 0xDFFF) return false;
                    code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
                } else if (code >= 0xDC00 && code <= 0xDFFF) return false;
                if (code < 0x80) out += static_cast<char>(code);
                else {
                    if (code >= 0x10000) out += static_cast<char>(0xF0 | (code >> 18));
                    if (code >= 0x800) out += static_cast<char>((code >= 0x10000 ? 0x80 : 0xE0) | ((code >> 12) & 63));
                    out += static_cast<char>((code >= 0x800 ? 0x80 : 0xC0) | ((code >> 6) & 63));
                    out += static_cast<char>(0x80 | (code & 63));
                }
            } else return false;
        }
        return false;
    }

    bool field(const char* wanted, std::string_view& result) {
        if (!punctuation('{')) return false;
        bool found = false;
        space();
        if (take('}')) return false;
        do {
            std::string key;
            if (!string(key) || !punctuation(':')) return false;
            space();
            const std::size_t start = at_;
            if (!value(0)) return false;
            if (key == wanted) {
                if (found) return false;
                result = text_.substr(start, at_ - start);
                found = true;
            }
            space();
            if (take('}')) return found;
        } while (punctuation(','));
        return false;
    }

    bool array(std::vector<std::string_view>& items) {
        if (!punctuation('[')) return false;
        space();
        if (take(']')) return true;
        do {
            space();
            const auto start = at_;
            if (items.size() >= 256 || !value(0) ||
                !try_push_back(items, text_.substr(start, at_ - start))) return false;
            space();
            if (take(']')) return true;
        } while (punctuation(','));
        return false;
    }

private:
    void space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n' ||
               text_[at_] == '\r' || text_[at_] == '\t')) ++at_;
    }
    bool take(char c) {
        if (at_ >= text_.size() || text_[at_] != c) return false;
        ++at_;
        return true;
    }
    bool punctuation(char c) {
        space();
        return take(c);
    }
    bool unicode(unsigned& code) {
        if (text_.size() - at_ < 4) return false;
        for (int i = 0; i < 4; ++i) {
            const int digit = hex_digit(text_[at_++]);
            if (digit < 0) return false;
            code = code * 16 + static_cast<unsigned>(digit);
        }
        return true;
    }
    bool number() {
        take('-');
        if (!take('0')) {
            if (at_ == text_.size() || text_[at_] < '1' || text_[at_] > '9') return false;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
        }
        const auto digits_after = [&]() {
            const auto start = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
            return at_ != start;
        };
        if (take('.') && !digits_after()) return false;
        if (take('e') || take('E')) {
            if (!take('+')) take('-');
            if (!digits_after()) return false;
        }
        return true;
    }
    bool value(unsigned depth) {
        if (depth > 16 || ++tokens_ > 32768) return false;
        space();
        if (at_ == text_.size()) return false;
        if (text_[at_] == '"') {
            std::string ignored;
            return string(ignored);
        }
        const bool object = text_[at_] == '{';
        if (object || text_[at_] == '[') {
            ++at_;
            const char close = object ? '}' : ']';
            space();
            if (take(close)) return true;
            do {
                if (object) {
                    std::string key;
                    if (!string(key) || !punctuation(':')) return false;
                }
                if (!value(depth + 1)) return false;
                space();
                if (take(close)) return true;
            } while (punctuation(','));
            return false;
        }
        for (const auto literal : {std::string_view("true"), std::string_view("false"), std::string_view("null")}) {
            if (text_.substr(at_, literal.size()) == literal) {
                at_ += literal.size();
                return true;
            }
        }
        return number();
    }
    std::string_view text_;
    std::size_t at_ = 0;
    unsigned tokens_ = 0;
};

bool response_items(const std::string& text, std::vector<std::string_view>& items) {
    if (!Json(text).whole()) return false;
    std::string_view success, data;
    return Json(text).field("success", success) && success == "true" &&
           Json(text).field("data", data) && Json(data).array(items);
}

bool string_field(std::string_view object, const char* key, std::string& out) {
    std::string_view value;
    return Json(object).field(key, value) && Json(value).string(out);
}

bool positive_id(std::string_view text, std::uint32_t& id) {
    if (text.empty() || text.size() > 10 || text[0] == '0') return false;
    const auto got = std::from_chars(text.data(), text.data() + text.size(), id);
    return got.ec == std::errc() && got.ptr == text.data() + text.size() && id > 0 && id <= 0x7FFFFFFFu;
}

}  // namespace

bool emulator_art_component(const std::string& name) {
    if (name.empty() || name.size() > kEmulatorArtNameMax || name == "." || name == ".." ||
        name.front() == '.' || name.back() == '.' || name.back() == ' ') return false;
    for (const unsigned char c : name) {
        if (c < 32 || c == 127 || c == '/' || c == '\\' || c == ':' || c == '?' || c == '*' ||
            c == '"' || c == '<' || c == '>' || c == '|') return false;
    }
    return true;
}

bool emulator_cover_id(const std::string& id) {
    if (id.size() != 68 || id.compare(0, 4, "emu-") != 0) return false;
    return std::all_of(id.begin() + 4, id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

std::string libretro_cover_name(const std::string& game) {
    if (game.empty() || game.size() > kEmulatorArtNameMax - 4) return "";
    std::string name = game;
    for (char& c : name) {
        if (std::string_view("&*/:`<>?\\|\"").find(c) != std::string_view::npos) c = '_';
    }
    name += ".png";
    return png_name(name) ? name : "";
}

std::string libretro_cover_url(const std::string& system, const std::string& name) {
    if (!emulator_art_component(system) || system.size() > 64 || (!name.empty() && !png_name(name))) return "";
    return "https://thumbnails.libretro.com/" + url_encode(system) + "/Named_Boxarts/" + url_encode(name);
}

bool parse_libretro_index(const std::string& html, std::vector<std::string>& names) {
    names.clear();
    if (html.size() > kEmulatorIndexMax) return false;
    std::vector<std::string> parsed;
    std::size_t at = 0;
    while ((at = html.find("href=", at)) != std::string::npos) {
        at += 5;
        if (at == html.size() || (html[at] != '"' && html[at] != '\'')) continue;
        const char quote = html[at++];
        const auto end = html.find(quote, at);
        if (end == std::string::npos) return false;
        std::string name;
        if (decode_link(std::string_view(html).substr(at, end - at), name)) {
            if (parsed.size() == kEmulatorIndexNamesMax || !try_push_back(parsed, std::move(name))) return false;
        }
        at = end + 1;
    }
    // An error/login page is no usable index and must not poison the cache.
    if (parsed.empty()) return false;
    std::sort(parsed.begin(), parsed.end());
    parsed.erase(std::unique(parsed.begin(), parsed.end()), parsed.end());
    names = std::move(parsed);
    return true;
}

bool serialize_emulator_index(const std::vector<std::string>& names, std::string& text) {
    text.clear();
    if (names.empty() || names.size() > kEmulatorIndexNamesMax) return false;
    std::string ready = "RIFTWII-EMULATOR-INDEX-1\n";
    for (const auto& name : names) {
        if (!png_name(name) || name.size() + 1 > kEmulatorIndexMax - ready.size() ||
            !try_append(ready, name.data(), name.size()) || !try_append(ready, "\n", 1)) return false;
    }
    text = std::move(ready);
    return true;
}

bool parse_emulator_index(const std::string& text, std::vector<std::string>& names) {
    names.clear();
    constexpr std::string_view header = "RIFTWII-EMULATOR-INDEX-1\n";
    if (text.size() > kEmulatorIndexMax || text.compare(0, header.size(), header) != 0) return false;
    std::vector<std::string> ready;
    std::size_t at = header.size();
    while (at < text.size()) {
        const auto end = text.find('\n', at);
        if (end == std::string::npos || end - at > kEmulatorArtNameMax || ready.size() == kEmulatorIndexNamesMax) return false;
        const auto name = text.substr(at, end - at);
        if (!png_name(name) || !try_push_back(ready, name)) return false;
        at = end + 1;
    }
    if (ready.empty()) return false;
    names = std::move(ready);
    return true;
}

std::string match_emulator_cover(const std::string& game, const std::vector<std::string>& names) {
    if (game.size() > kEmulatorArtNameMax || names.size() > kEmulatorIndexNamesMax) return "";
    const std::string key = homebrew_match_key(game);
    if (key.empty()) return "";
    const std::string numbers = digits(key);
    const std::string roman = roman_sequels(game);
    const bool non_ascii = key.find('#') != std::string::npos;
    std::string best, best_key;
    double score = 0, runner_up = 0;
    int region = 99;
    for (const auto& name : names) {
        if (!png_name(name)) continue;
        const std::string title = name.substr(0, name.size() - 4);
        const std::string candidate = homebrew_match_key(title);
        if (candidate.empty() || digits(candidate) != numbers || roman_sequels(title) != roman) continue;
        // The homebrew key represents each non-ASCII character as '#'.
        // Equal counts of Japanese/accented letters must not match by that.
        if (non_ascii && title_bytes(title) != title_bytes(game)) continue;
        const double likeness = homebrew_key_likeness(key, candidate, 0.94);
        if (likeness < 0.94 || (key.size() < 10 && key != candidate)) continue;
        const int rank = region_rank(name, game);
        if (likeness > score) {
            if (candidate != best_key) runner_up = score;
            best = name;
            best_key = candidate;
            score = likeness;
            region = rank;
        } else if (candidate == best_key) {
            if (rank < region || (rank == region && name < best)) {
                best = name;
                region = rank;
            }
        } else runner_up = std::max(runner_up, likeness);
    }
    if (score < 1.0 && score - runner_up < 0.03) return "";
    return best;
}

std::string emulator_art_miss(const std::string& source, std::int64_t now) {
    if (source.empty() || source.size() > 128 || source.find_first_of("\r\n\0", 0, 3) != std::string::npos || now <= 0) return "";
    return "RIFTWII-ART-MISS-1\n" + source + "\n" + std::to_string(now) + "\n";
}

bool emulator_art_miss_fresh(const std::string& text, const std::string& source, std::int64_t now) {
    if (text.size() > 256 || source.empty() || source.size() > 128 ||
        source.find_first_of("\r\n\0", 0, 3) != std::string::npos) return false;
    const std::string prefix = "RIFTWII-ART-MISS-1\n" + source + "\n";
    if (text.compare(0, prefix.size(), prefix) != 0 || text.empty() || text.back() != '\n') return false;
    const auto stamp = std::string_view(text).substr(prefix.size(), text.size() - prefix.size() - 1);
    std::int64_t when = 0;
    const auto got = std::from_chars(stamp.data(), stamp.data() + stamp.size(), when);
    return got.ec == std::errc() && got.ptr == stamp.data() + stamp.size() && when > 0 &&
           now >= when && now - when < kEmulatorArtWeek;
}

bool steamgrid_key_valid(const std::string& key) {
    return key.size() >= 16 && key.size() <= 128 && std::all_of(key.begin(), key.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

bool steamgrid_image_url(const std::string& url) {
    if (url.size() > 1024) return false;
    HttpUrl parsed;
    std::string error;
    if (!parse_http_url(url, parsed, error) || !parsed.tls || parsed.port != 443 ||
        (parsed.host != "cdn2.steamgriddb.com" && parsed.host != "www.steamgriddb.com" && parsed.host != "steamgriddb.com")) return false;
    return parsed.path.rfind("/grid/", 0) == 0 && parsed.path.size() > 10 &&
           parsed.path.compare(parsed.path.size() - 4, 4, ".png") == 0 &&
           parsed.path.find_first_of("?#\\\r\n") == std::string::npos;
}

bool parse_steamgrid_search(const std::string& json, std::vector<SteamGridGame>& games) {
    games.clear();
    std::vector<std::string_view> items;
    if (!response_items(json, items)) return false;
    std::vector<SteamGridGame> ready;
    for (const auto item : items) {
        SteamGridGame game;
        std::string_view id;
        if (!Json(item).field("id", id) || !positive_id(id, game.id) ||
            !string_field(item, "name", game.name) || game.name.empty() || game.name.size() > 255) return false;
        for (unsigned char c : game.name) {
            if (c < 32 || c == 127) return false;
        }
        if (!try_push_back(ready, std::move(game))) return false;
    }
    games = std::move(ready);
    return true;
}

bool parse_steamgrid_grids(const std::string& json, std::vector<std::string>& urls) {
    urls.clear();
    std::vector<std::string_view> items;
    if (!response_items(json, items)) return false;
    std::vector<std::string> ready;
    for (const auto item : items) {
        std::string url, mime;
        std::string_view width, height;
        if (!string_field(item, "url", url) || !string_field(item, "mime", mime) ||
            !Json(item).field("width", width) || !Json(item).field("height", height)) return false;
        if (mime != "image/png" || width != "600" || height != "900" || !steamgrid_image_url(url)) continue;
        if (!try_push_back(ready, std::move(url))) return false;
    }
    urls = std::move(ready);
    return true;
}

std::uint32_t match_steamgrid_game(const std::string& game, const std::vector<SteamGridGame>& games) {
    if (games.size() > 256) return 0;
    // Search has no system field: require an exact normalized title and
    // reject multiple IDs, rather than choose a similarly named game.
    const std::string key = homebrew_match_key(game);
    if (key.empty() || game.size() > 255) return 0;
    std::uint32_t id = 0;
    for (const auto& candidate : games) {
        if (candidate.name.size() > 255 || homebrew_match_key(candidate.name) != key) continue;
        if (key.find('#') != std::string::npos && title_bytes(candidate.name) != title_bytes(game)) continue;
        if (id && id != candidate.id) return 0;
        id = candidate.id;
    }
    return id;
}

}  // namespace riftwii
