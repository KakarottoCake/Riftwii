// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/update.hpp"

#include <algorithm>
#include <cctype>
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

bool release_tag_from_json(const std::string& json, std::string& tag) {
    const std::string key = "\"tag_name\"";
    std::size_t at = json.find(key);
    if (at == std::string::npos) return false;
    at += key.size();
    while (at < json.size() && (json[at] == ' ' || json[at] == '\t' || json[at] == '\n' || json[at] == '\r')) ++at;
    if (at >= json.size() || json[at] != ':') return false;
    ++at;
    while (at < json.size() && (json[at] == ' ' || json[at] == '\t' || json[at] == '\n' || json[at] == '\r')) ++at;
    if (at >= json.size() || json[at] != '"') return false;
    ++at;
    std::string out;
    for (; at < json.size() && json[at] != '"'; ++at) {
        if (json[at] == '\\' && at + 1 < json.size()) ++at;  // tags have no escapes worth decoding
        out += json[at];
    }
    if (at >= json.size() || out.empty() || out.size() > 64) return false;
    tag = out;
    return true;
}

int compare_versions(const std::string& a, const std::string& b) {
    const Version x = parse(a), y = parse(b);
    const std::size_t n = std::max(x.numbers.size(), y.numbers.size());
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned long p = i < x.numbers.size() ? x.numbers[i] : 0;
        const unsigned long q = i < y.numbers.size() ? y.numbers[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    if (x.suffix == y.suffix) return 0;
    if (x.suffix.empty()) return 1;  // the release after its betas
    if (y.suffix.empty()) return -1;
    return x.suffix < y.suffix ? -1 : 1;
}

}  // namespace riftwii
