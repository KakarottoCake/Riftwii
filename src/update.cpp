// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/update.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

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

// The value after the key that ends at `at` (just past its closing
// quote): a string into `text`, or a bare token (number, null) into
// `bare`. False when no ':' follows.
bool value_after(const std::string& json, std::size_t at, std::string& text, std::string& bare) {
    text.clear();
    bare.clear();
    while (at < json.size() && blank(json[at])) ++at;
    if (at >= json.size() || json[at] != ':') return false;
    ++at;
    while (at < json.size() && blank(json[at])) ++at;
    if (at < json.size() && json[at] == '"') {
        for (++at; at < json.size() && json[at] != '"'; ++at) {
            if (json[at] == '\\' && at + 1 < json.size()) ++at;  // nothing here needs decoding
            text += json[at];
        }
        return at < json.size();
    }
    for (; at < json.size() && json[at] != ',' && json[at] != '}' && json[at] != ']' && !blank(json[at]); ++at)
        bare += json[at];
    return true;
}

// The first `"key"` at or after `from` and before `to`; npos when none.
std::size_t find_key(const std::string& json, const std::string& key, std::size_t from, std::size_t to) {
    const std::size_t at = json.find("\"" + key + "\"", from);
    return at == std::string::npos || at >= to ? std::string::npos : at;
}

}  // namespace

bool release_tag_from_json(const std::string& json, std::string& tag) {
    const std::size_t at = find_key(json, "tag_name", 0, json.size());
    std::string text, bare;
    if (at == std::string::npos || !value_after(json, at + 10, text, bare)) return false;
    if (text.empty() || text.size() > 64) return false;
    tag = text;
    return true;
}

bool release_asset_from_json(const std::string& json, const std::string& name, ReleaseAsset& out) {
    // GitHub lists an asset's "name" before its "size", "digest" and
    // "browser_download_url"; nothing of another asset comes between.
    std::string text, bare;
    for (std::size_t at = find_key(json, "name", 0, json.size()); at != std::string::npos;
         at = find_key(json, "name", at + 6, json.size())) {
        if (!value_after(json, at + 6, text, bare) || text != name) continue;
        const std::size_t url_at = find_key(json, "browser_download_url", at, json.size());
        if (url_at == std::string::npos || !value_after(json, url_at + 22, text, bare) || text.empty()) return false;
        ReleaseAsset asset;
        asset.url = text;
        const std::size_t digest_at = find_key(json, "digest", at, url_at);
        if (digest_at != std::string::npos && value_after(json, digest_at + 8, text, bare) &&
            text.compare(0, 7, "sha256:") == 0 && text.size() == 7 + 64) {
            for (std::size_t i = 7; i < text.size(); ++i)
                asset.sha256 += static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        }
        const std::size_t size_at = find_key(json, "size", at, url_at);
        if (size_at != std::string::npos && value_after(json, size_at + 6, text, bare) && !bare.empty() &&
            std::isdigit(static_cast<unsigned char>(bare[0]))) {
            asset.size = std::strtoull(bare.c_str(), nullptr, 10);
        }
        out = asset;
        return true;
    }
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

std::string effective_update_channel(const std::string& setting, const std::string& version) {
    if (setting == "stable" || setting == "beta") return setting;
    return version.find('-') != std::string::npos ? "beta" : "stable";
}

}  // namespace riftwii
