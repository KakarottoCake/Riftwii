// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/update.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>
#include <vector>

#include "riftwii/pluginfile.hpp"

namespace riftwii {
namespace {

struct Version {
    std::vector<unsigned long> numbers;
    std::string suffix;  // lowercased, without its separator
};

Version parse(const std::string& text) {
    Version v;
    std::size_t at = 0;
    if (at < text.size() && (text[at] == 'v' || text[at] == 'V')) ++at;
    while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) {
        unsigned long n = 0;
        while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) {
            n = n * 10 + static_cast<unsigned long>(text[at] - '0');
            ++at;
        }
        v.numbers.push_back(n);
        if (at < text.size() && text[at] == '.') ++at;
    }
    while (at < text.size() && (text[at] == '-' || text[at] == ' ' || text[at] == '_')) ++at;
    for (; at < text.size(); ++at) v.suffix += static_cast<char>(std::tolower(static_cast<unsigned char>(text[at])));
    return v;
}

}  // namespace

namespace {

bool blank(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Walk structure without allocating skipped values (an uploader or release
// body can be large). Depth is bounded because API answers are untrusted.
class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    void skip_space() {
        while (at_ < text_.size() && blank(text_[at_])) ++at_;
    }

    bool take(char c) {
        skip_space();
        if (at_ == text_.size() || text_[at_] != c) return false;
        ++at_;
        return true;
    }

    bool done() {
        skip_space();
        return at_ == text_.size();
    }

    bool string(std::string_view& out) {
        skip_space();
        const std::size_t start = at_;
        if (!take('"')) return false;
        while (at_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[at_++]);
            if (c == '"') {
                out = text_.substr(start + 1, at_ - start - 2);
                return true;
            }
            if (c < 0x20) return false;
            if (c != '\\') continue;
            if (at_ == text_.size()) return false;
            const char escape = text_[at_++];
            if (escape == 'u') {
                for (unsigned i = 0; i < 4; ++i) {
                    if (at_ == text_.size() || !std::isxdigit(static_cast<unsigned char>(text_[at_]))) return false;
                    ++at_;
                }
            } else if (std::string_view("\"\\/bfnrt").find(escape) == std::string_view::npos) {
                return false;
            }
        }
        return false;
    }

    bool value(std::string_view& out, unsigned depth = 0) {
        if (depth > 12) return false;
        skip_space();
        if (at_ == text_.size()) return false;
        const std::size_t start = at_;
        const char first = text_[at_];
        if (first == '"') {
            std::string_view ignored;
            if (!string(ignored)) return false;
        } else if (first == '{' || first == '[') {
            ++at_;
            const char close = first == '{' ? '}' : ']';
            if (!take(close)) {
                for (;;) {
                    std::string_view ignored;
                    if (first == '{' && (!string(ignored) || !take(':'))) return false;
                    if (!value(ignored, depth + 1)) return false;
                    if (take(close)) break;
                    if (!take(',')) return false;
                }
            }
        } else {
            while (at_ < text_.size() && !blank(text_[at_]) && text_[at_] != ',' &&
                   text_[at_] != '}' && text_[at_] != ']')
                ++at_;
            if (at_ == start) return false;
            const std::string_view token = text_.substr(start, at_ - start);
            if (token != "true" && token != "false" && token != "null" && !number(token)) return false;
        }
        out = text_.substr(start, at_ - start);
        return true;
    }

private:
    static bool number(std::string_view token) {
        std::size_t at = token[0] == '-' ? 1 : 0;
        const auto digits = [&] {
            const std::size_t start = at;
            while (at < token.size() && token[at] >= '0' && token[at] <= '9') ++at;
            return at != start;
        };
        if (at == token.size()) return false;
        if (token[at] == '0') ++at;
        else if (!digits()) return false;
        if (at < token.size() && token[at] == '.') {
            ++at;
            if (!digits()) return false;
        }
        if (at < token.size() && (token[at] == 'e' || token[at] == 'E')) {
            ++at;
            if (at < token.size() && (token[at] == '+' || token[at] == '-')) ++at;
            if (!digits()) return false;
        }
        return at == token.size();
    }

    std::string_view text_;
    std::size_t at_ = 0;
};

// Only immediate members count: uploader.name is not the asset's name.
// The whole document is validated first, so these walks can stop at a match.
bool member(std::string_view object, const char* wanted, std::string_view& out) {
    JsonReader reader(object);
    if (!reader.take('{') || reader.take('}')) return false;
    do {
        std::string_view key;
        std::string_view value;
        if (!reader.string(key) || !reader.take(':') || !reader.value(value)) return false;
        if (key == wanted) {
            out = value;
            return true;
        }
    } while (reader.take(','));
    return false;
}

// The text between the quotes, with JSON escapes resolved to bytes.
// A lone or reversed surrogate and a NUL character escape are malformed.
bool decode_json_string(std::string_view raw, std::string& out) {
    out.clear();
    std::size_t at = 0;
    const auto hex_value = [](char c, unsigned& v) {
        if (c >= '0' && c <= '9') v = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') v = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        return true;
    };
    const auto unit_at = [&](std::size_t pos, unsigned& unit) {
        if (pos + 4 > raw.size()) return false;
        unit = 0;
        for (unsigned i = 0; i < 4; ++i) {
            unsigned v = 0;
            if (!hex_value(raw[pos + i], v)) return false;
            unit = unit * 16 + v;
        }
        return true;
    };
    const auto append_utf8 = [&](unsigned point) {
        if (point < 0x80) {
            out += static_cast<char>(point);
        } else if (point < 0x800) {
            out += static_cast<char>(0xc0 | (point >> 6));
            out += static_cast<char>(0x80 | (point & 0x3f));
        } else if (point < 0x10000) {
            out += static_cast<char>(0xe0 | (point >> 12));
            out += static_cast<char>(0x80 | ((point >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (point & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (point >> 18));
            out += static_cast<char>(0x80 | ((point >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((point >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (point & 0x3f));
        }
    };
    while (at < raw.size()) {
        const char c = raw[at];
        if (c == '\\') {
            if (at + 1 >= raw.size()) return false;
            const char escape = raw[at + 1];
            if (escape == '"') out += '"';
            else if (escape == '\\') out += '\\';
            else if (escape == '/') out += '/';
            else if (escape == 'b') out += '\b';
            else if (escape == 'f') out += '\f';
            else if (escape == 'n') out += '\n';
            else if (escape == 'r') out += '\r';
            else if (escape == 't') out += '\t';
            else if (escape == 'u') {
                unsigned unit = 0;
                if (!unit_at(at + 2, unit)) return false;
                if (unit >= 0xd800 && unit <= 0xdbff) {
                    if (at + 8 >= raw.size() || raw[at + 6] != '\\' || raw[at + 7] != 'u') return false;
                    unsigned low = 0;
                    if (!unit_at(at + 8, low)) return false;
                    if (low < 0xdc00 || low > 0xdfff) return false;
                    append_utf8(0x10000u + ((unit - 0xd800u) << 10) + (low - 0xdc00u));
                    at += 12;
                    continue;
                }
                if (unit >= 0xdc00 && unit <= 0xdfff) return false;
                if (unit == 0) return false;
                append_utf8(unit);
                at += 6;
                continue;
            } else {
                return false;
            }
            at += 2;
            continue;
        }
        if (c == '"' || static_cast<unsigned char>(c) < 0x20) return false;
        out += c;
        ++at;
    }
    return true;
}

// A raw value decoded when it is a string; false when not a string or malformed.
bool decode_string_value(std::string_view value, std::string& text) {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    return decode_json_string(value.substr(1, value.size() - 2), text);
}

// A direct member decoded as a string; false when missing, not a string,
// or malformed, so callers can tell a match from a refusal.
bool member_text(std::string_view object, const char* key, std::string& text) {
    std::string_view value;
    if (!member(object, key, value)) return false;
    return decode_string_value(value, text);
}

enum class AssetLookup { Missing, Refused, Found };

// The first asset named by the caller in one release, with decoded strings; a missing or null
// digest/size stays empty/0 while any other present digest/size refuses the asset.
AssetLookup release_asset(std::string_view release, const std::string& name, ReleaseAsset& out) {
    std::string_view assets;
    if (!member(release, "assets", assets)) return AssetLookup::Missing;
    JsonReader reader(assets);
    if (!reader.take('[') || reader.take(']')) return AssetLookup::Missing;
    do {
        std::string_view object;
        std::string text;
        if (!reader.value(object)) return AssetLookup::Refused;
        if (object.empty() || object.front() != '{') continue;
        if (!member_text(object, "name", text) || text != name) continue;
        ReleaseAsset asset;
        if (!member_text(object, "browser_download_url", text) ||
            !riftwii_release_asset_url(text)) return AssetLookup::Refused;
        asset.url = text;
        std::string_view digest_value;
        if (member(object, "digest", digest_value)) {
            if (digest_value != "null") {
                if (!decode_string_value(digest_value, text)) return AssetLookup::Refused;
                if (text.size() != 7 + 64 || text.compare(0, 7, "sha256:") != 0) return AssetLookup::Refused;
                asset.sha256.clear();
                for (std::size_t i = 7; i < text.size(); ++i) {
                    if (!std::isxdigit(static_cast<unsigned char>(text[i]))) return AssetLookup::Refused;
                    asset.sha256 += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
                }
            }
        }
        std::string_view size_value;
        if (member(object, "size", size_value)) {
            if (size_value != "null") {
                if (size_value.empty()) return AssetLookup::Refused;
                for (const char c : size_value) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) return AssetLookup::Refused;
                }
                if (size_value.front() == '0') return AssetLookup::Refused;
                unsigned long long value = 0;
                constexpr unsigned long long kMax = std::numeric_limits<unsigned long long>::max();
                for (const char c : size_value) {
                    const unsigned digit = static_cast<unsigned>(c - '0');
                    if (value > (kMax - digit) / 10) return AssetLookup::Refused;
                    value = value * 10 + digit;
                }
                asset.size = value;
            }
        }
        out = asset;
        return AssetLookup::Found;
    } while (reader.take(','));
    return AssetLookup::Missing;
}

}  // namespace

bool release_tag_from_json(const std::string& json, std::string& tag) {
    JsonReader document(json);
    std::string_view root;
    if (!document.value(root) || !document.done()) return false;
    std::string_view release;
    if (root.front() == '{') {
        release = root;
    } else if (root.front() == '[') {
        JsonReader releases(root);
        if (!releases.take('[') || releases.take(']')) return false;
        std::string_view first;
        if (!releases.value(first)) return false;
        if (first.empty() || first.front() != '{') return false;
        release = first;
    } else {
        return false;
    }
    std::string text;
    if (!member_text(release, "tag_name", text)) return false;
    // Tags are stored and later appended to a GitHub API URL (releases look like "v2610-691"),
    // so only A-Z a-z 0-9 . _ + - pass.
    if (text.empty() || text.size() > 64) return false;
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool ok = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') ||
                        u == '.' || u == '_' || u == '+' || u == '-';
        if (!ok) return false;
    }
    tag = text;
    return true;
}

bool riftwii_release_asset_url(const std::string& url) {
    // Owner and repository names are not case-sensitive on GitHub.
    std::string lowered;
    for (const char c : url) lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const char* const kReleaseFiles[] = {
        "https://github.com/kakarottocake/riftwii/releases/download/",
        "https://github.com/riftwii/riftwii/releases/download/",
    };
    for (const char* prefix : kReleaseFiles) {
        const std::size_t n = std::char_traits<char>::length(prefix);
        // A tag and a file name after the prefix, both there, nothing more,
        // and never another level up.
        if (lowered.size() <= n || lowered.compare(0, n, prefix) != 0) continue;
        const std::string rest = lowered.substr(n);
        const std::size_t slash = rest.find('/');
        if (slash == std::string::npos || slash == 0 || slash + 1 == rest.size()) return false;
        if (rest.find('/', slash + 1) != std::string::npos || rest.find("..") != std::string::npos) return false;
        return true;
    }
    return false;
}

bool release_asset_from_json(const std::string& json, const std::string& name, ReleaseAsset& out) {
    JsonReader document(json);
    std::string_view root;
    if (!document.value(root) || !document.done()) return false;
    if (root.front() == '{') return release_asset(root, name, out) == AssetLookup::Found;
    JsonReader releases(root);
    if (!releases.take('[') || releases.take(']')) return false;
    do {
        std::string_view release;
        if (!releases.value(release)) return false;
        const AssetLookup result = release_asset(release, name, out);
        // A refused first match must not be replaced by an older release.
        if (result != AssetLookup::Missing) return result == AssetLookup::Found;
    } while (releases.take(','));
    return false;
}

namespace {

// Suffixes in runs of digits and of other characters: the digits as
// numbers ("rc10" after "rc9", "rc9.1" after "rc9"), the rest as text.
int compare_suffixes(const std::string& a, const std::string& b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const bool da = std::isdigit(static_cast<unsigned char>(a[i])) != 0;
        const bool db = std::isdigit(static_cast<unsigned char>(b[j])) != 0;
        if (da && db) {
            std::size_t ei = i, ej = j;
            while (ei < a.size() && std::isdigit(static_cast<unsigned char>(a[ei]))) ++ei;
            while (ej < b.size() && std::isdigit(static_cast<unsigned char>(b[ej]))) ++ej;
            while (i + 1 < ei && a[i] == '0') ++i;  // leading zeros
            while (j + 1 < ej && b[j] == '0') ++j;
            if (ei - i != ej - j) return ei - i < ej - j ? -1 : 1;
            const int c = a.compare(i, ei - i, b, j, ej - j);
            if (c != 0) return c < 0 ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        if (a[i] != b[j]) return static_cast<unsigned char>(a[i]) < static_cast<unsigned char>(b[j]) ? -1 : 1;
        ++i;
        ++j;
    }
    if (i == a.size() && j == b.size()) return 0;
    return i == a.size() ? -1 : 1;
}

}  // namespace

int compare_versions(const std::string& a, const std::string& b) {
    const Version x = parse(a), y = parse(b);
    const std::size_t n = std::max(x.numbers.size(), y.numbers.size());
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned long p = i < x.numbers.size() ? x.numbers[i] : 0;
        const unsigned long q = i < y.numbers.size() ? y.numbers[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    if (x.suffix == y.suffix) return 0;
    // A suffix of digits alone is a build that many commits after its
    // release (Dolphin's numbering: 2610-123 after 2610); a word (beta,
    // rc1) marks a build before its release (2.0.0-beta before 2.0.0).
    const auto count = [](const std::string& s) {
        return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos;
    };
    if (x.suffix.empty()) return count(y.suffix) ? -1 : 1;
    if (y.suffix.empty()) return count(x.suffix) ? 1 : -1;
    return compare_suffixes(x.suffix, y.suffix);
}

bool plan_update_writes(const ThemePack& apps, const std::string& release, const std::string& plugin_dir,
                        const std::string& dol_path, std::vector<UpdateWrite>& out, std::string& error) {
    out.clear();
    static const std::string kPluginsFolder = "riftwii/plugins/";
    // The releases' tags are "v2610-500"; the version in a file name is
    // the build's own, "2610-500".
    const bool tagged = !release.empty() && (release[0] == 'v' || release[0] == 'V');
    const std::string version = tagged ? release.substr(1) : release;
    if (version.empty() || dol_path.empty() || plugin_dir.empty() || plugin_dir.back() != '/') {
        error = "the update has no version, DOL path or plugin folder to write to";
        return false;
    }
    std::vector<UpdateWrite> plugins;
    UpdateWrite font;
    for (const ThemePackFile& file : apps.files) {
        // The full menu font: beside the new DOL, whose core font lacks
        // what it holds (wii/menu/fullfont.hpp).
        if (file.path == kMenuFontFile) {
            font.path = dol_path.substr(0, dol_path.rfind('/') + 1) + "font.ttf";
            font.offset = file.offset;
            font.size = file.size;
            continue;
        }
        if (file.path.compare(0, kPluginsFolder.size(), kPluginsFolder) != 0) continue;  // the next start's
        const std::string name = file.path.substr(kPluginsFolder.size());
        std::string plugin;
        std::string its_version;
        if (name.find('/') != std::string::npos || !plugin_file_parse(name, plugin, its_version)) {
            error = "the update's apps pack has a plugin file RiftWii does not write: " + file.path;
            return false;
        }
        if (its_version != version) {
            error = "the update's plugin " + name + " is not for " + version + ", the version being installed";
            return false;
        }
        UpdateWrite write;
        write.path = plugin_dir + name;
        write.offset = file.offset;
        write.size = file.size;
        plugins.push_back(write);
    }
    out = plugins;
    if (!font.path.empty()) out.push_back(font);
    UpdateWrite dol;
    dol.path = dol_path;
    dol.dol = true;
    out.push_back(dol);
    return true;
}

}  // namespace riftwii
