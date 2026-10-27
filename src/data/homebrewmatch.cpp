// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/homebrewmatch.hpp"

#include <algorithm>
#include <cmath>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/textlines.hpp"

#include "pugixml.hpp"

namespace riftwii {
namespace {

constexpr char kOtherCharacter = '#';

bool is_homebrew_id(const std::string& id) {
    if (id.size() != 4) return false;
    for (char c : id) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!ok) return false;
    }
    return true;
}

// A name as data: control characters (tabs and line breaks among them, as
// they divide the index) made spaces, spaces trimmed, cut without
// splitting a UTF-8 character.
std::string clean_name(const std::string& in) {
    std::string out;
    for (char c : in) out += static_cast<unsigned char>(c) < 32 ? ' ' : c;
    const std::size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    out = out.substr(first, out.find_last_not_of(' ') - first + 1);
    if (out.size() > kMaxHomebrewNameBytes) {
        std::size_t cut = kMaxHomebrewNameBytes;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    return out;
}

// Adds `name` to the entry unless it is empty, adds nothing to compare
// (all of it non-ASCII, say) or compares as one already there.
void add_name(HomebrewTitle& title, const std::string& raw) {
    if (title.names.size() >= kMaxHomebrewNames) return;
    const std::string name = clean_name(raw);
    const std::string key = homebrew_match_key(name);
    if (key.empty()) return;
    for (const std::string& kept : title.names)
        if (homebrew_match_key(kept) == key) return;
    title.names.push_back(name);
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_letter(char c) { return c >= 'a' && c <= 'z'; }

// A word that only says which release it is: "1.2.3", "v4", "r1250",
// "1.0b", "beta". A plain number stays ("Biniax 2", "Quake 3"), as do
// "3d" and "ios51": they name the app.
bool is_version_word(const std::string& word) {
    if (word == "beta" || word == "alpha") return true;
    std::size_t at = 0;
    if (word.compare(0, 3, "rev") == 0) at = 3;
    else if (!word.empty() && (word[0] == 'v' || word[0] == 'r')) at = 1;
    if (at >= word.size() || !is_digit(word[at])) return false;
    bool dotted = false;
    for (std::size_t i = at; i < word.size(); ++i) {
        const char c = word[i];
        if (c == '.') {
            dotted = true;
            continue;
        }
        // A letter only at the end ("1.0b").
        if (is_letter(c) && i + 1 == word.size()) continue;
        if (!is_digit(c)) return false;
    }
    return at > 0 || dotted;
}

std::string without_brackets(const std::string& name) {
    std::string out;
    int depth = 0;
    for (char c : name) {
        if (c == '(' || c == '[') {
            ++depth;
            out += ' ';
        } else if ((c == ')' || c == ']') && depth > 0) {
            --depth;
        } else if (depth == 0) {
            out += c;
        }
    }
    return out;
}

// The name as written, lowercase, without its brackets and version words,
// one space between words: "WiiMC 1.3.8" is "wiimc", "WiiMC+" "wiimc+".
std::string plain_name(const std::string& name) {
    std::string out;
    std::string word;
    const auto end_word = [&]() {
        if (!word.empty() && !is_version_word(word)) out += (out.empty() ? "" : " ") + word;
        word.clear();
    };
    for (char c : without_brackets(name)) {
        if (static_cast<unsigned char>(c) <= ' ') end_word();
        else word += lower(c);
    }
    end_word();
    return out;
}

// Each character outside ASCII stands as one '#': GameTDB's Korean title
// for "WiiFlow Start Configurator" is "WiiFlow" and two Korean words, and
// must not compare as plain "WiiFlow".
std::string key_of(const std::string& name) {
    std::string key;
    std::string word;
    const auto end_word = [&]() {
        if (!is_version_word(word)) {
            for (char c : word) {
                const unsigned char u = static_cast<unsigned char>(c);
                if (is_digit(c) || is_letter(c)) key += c;
                else if (u >= 0xC0) key += kOtherCharacter;  // a UTF-8 character's first byte
            }
        }
        word.clear();
    };
    for (char c : name) {
        if (c == ' ' || c == '_' || c == '-') end_word();
        else word += lower(c);
    }
    end_word();
    // Nothing in ASCII is nothing to compare by.
    const bool any_ascii = std::any_of(key.begin(), key.end(), [](char c) { return c != kOtherCharacter; });
    return any_ascii ? key : std::string();
}

}  // namespace

HomebrewMatcher::Counts HomebrewMatcher::count_letters(const std::string& key) {
    Counts counts{};
    for (char c : key) {
        std::size_t slot = kSymbols - 1;  // kOtherCharacter
        if (is_letter(c)) slot = static_cast<std::size_t>(c - 'a');
        else if (is_digit(c)) slot = 26 + static_cast<std::size_t>(c - '0');
        if (counts[slot] < 255) ++counts[slot];
    }
    return counts;
}

bool parse_wiitdb_homebrew(const std::string& block, HomebrewTitle& out) {
    pugi::xml_document doc;
    if (!doc.load_buffer(block.data(), block.size())) return false;
    const pugi::xml_node game = doc.child("game");
    if (!game || std::string(game.child_value("type")) != "Homebrew") return false;
    HomebrewTitle title;
    title.id = clean_name(game.child_value("id"));
    if (!is_homebrew_id(title.id)) return false;
    // The English title first: the name attribute carries regions and
    // languages ("Wiimmfi Patcher (EN)"), which the key leaves out anyway.
    for (const pugi::xml_node locale : game.children("locale"))
        if (std::string(locale.attribute("lang").as_string()) == "EN") add_name(title, locale.child_value("title"));
    add_name(title, game.attribute("name").as_string());
    for (const pugi::xml_node locale : game.children("locale")) add_name(title, locale.child_value("title"));
    if (title.names.empty()) return false;
    out = std::move(title);
    return true;
}

std::string homebrew_match_key(const std::string& name) {
    const std::string key = key_of(without_brackets(name));
    // A name that is all brackets ("(untitled)") keeps what is in them.
    return key.empty() ? key_of(name) : key;
}

double homebrew_key_likeness(const std::string& a, const std::string& b, double at_least) {
    if (a == b) return a.empty() ? 0.0 : 1.0;
    const std::size_t longer = std::max(a.size(), b.size());
    const std::size_t shorter = std::min(a.size(), b.size());
    if (shorter == 0) return 0.0;
    // The most edits that can still reach `at_least`: a length difference
    // beyond it rules the pair out before counting.
    const double room = (1.0 - std::max(0.0, at_least)) * static_cast<double>(longer);
    const std::size_t allowed = static_cast<std::size_t>(std::floor(room + 1e-9));
    if (longer - shorter > allowed) return 0.0;
    std::vector<std::size_t> previous(b.size() + 1), current(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) previous[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        std::size_t row_best = current[0];
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t change = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            const std::size_t drop = previous[j] + 1;
            const std::size_t add = current[j - 1] + 1;
            current[j] = std::min({change, drop, add});
            row_best = std::min(row_best, current[j]);
        }
        // Every path through this row already costs more than allowed.
        if (row_best > allowed) return 0.0;
        std::swap(previous, current);
    }
    const std::size_t distance = previous[b.size()];
    if (distance > allowed) return 0.0;
    return 1.0 - static_cast<double>(distance) / static_cast<double>(longer);
}

HomebrewMatcher::HomebrewMatcher(const std::vector<HomebrewTitle>& titles) {
    for (const HomebrewTitle& title : titles) {
        if (ids_.size() >= kMaxHomebrewTitles) break;
        const std::size_t index = ids_.size();
        ids_.push_back(title.id);
        for (const std::string& name : title.names) {
            std::string key = homebrew_match_key(name);
            if (key.empty()) continue;
            const Counts counts = count_letters(key);
            keys_.push_back({std::move(key), plain_name(name), counts, index});
        }
    }
}

std::string HomebrewMatcher::best(const std::string& name, const std::string& key, double at_least) const {
    if (key.empty()) return "";
    // Exact: the entry whose name is the app's letter for letter ("WiiMC",
    // not "WiiMC+", which compares the same), else the first with that key
    // (GameTDB lists the same app once for each region at times, all with
    // the same cover).
    const std::string plain = plain_name(name);
    const Key* exact = nullptr;
    for (const Key& k : keys_) {
        if (k.key != key) continue;
        if (k.plain == plain) return ids_[k.title];
        if (!exact) exact = &k;
    }
    if (exact) return ids_[exact->title];
    if (key.size() < kMinFuzzyKey) return "";
    const Counts counts = count_letters(key);
    double best_score = 0.0;
    std::size_t best_title = 0;
    bool tied = false;
    for (const Key& k : keys_) {
        if (k.key.size() < kMinFuzzyKey) continue;
        // The letters one key has more of than the other each take an
        // edit at least: a quick way to pass over nearly every entry
        // before counting edits (a Wii matches 120 apps against 700).
        const std::size_t longer = std::max(key.size(), k.key.size());
        const std::size_t allowed = static_cast<std::size_t>(std::floor((1.0 - at_least) * static_cast<double>(longer) + 1e-9));
        std::size_t extra = 0, missing = 0;
        for (std::size_t i = 0; i < kSymbols; ++i) {
            if (counts[i] > k.counts[i]) extra += counts[i] - k.counts[i];
            else missing += k.counts[i] - counts[i];
        }
        if (std::max(extra, missing) > allowed) continue;
        const double score = homebrew_key_likeness(key, k.key, at_least);
        if (score < at_least || score <= 0.0) continue;
        if (score > best_score) {
            best_score = score;
            best_title = k.title;
            tied = false;
        } else if (score == best_score && ids_[k.title] != ids_[best_title]) {
            tied = true;
        }
    }
    // Two different apps as close as each other: neither is a safe guess.
    if (best_score <= 0.0 || tied) return "";
    return ids_[best_title];
}

std::string HomebrewMatcher::match(const std::string& meta_name, const std::string& folder_name) const {
    const std::string name_key = homebrew_match_key(meta_name);
    const std::string by_name = best(meta_name, name_key, kNameLikeness);
    if (!by_name.empty()) return by_name;
    const std::string folder_key = homebrew_match_key(folder_name);
    if (folder_key == name_key) return "";
    return best(folder_name, folder_key, kFolderLikeness);
}

bool serialize_homebrew_titles(const std::vector<HomebrewTitle>& titles, std::string& out) {
    out.clear();
    static const char kHeader[] = "# GameTDB's homebrew apps, made by RiftWii from wiitdb.xml\n";
    if (!try_append(out, kHeader, sizeof(kHeader) - 1)) return false;
    std::size_t written = 0;
    std::string line;
    for (const HomebrewTitle& title : titles) {
        if (written >= kMaxHomebrewTitles) break;
        if (!is_homebrew_id(title.id) || title.names.empty()) continue;
        line = title.id;
        for (std::size_t i = 0; i < title.names.size() && i < kMaxHomebrewNames; ++i) line += "\t" + clean_name(title.names[i]);
        line += "\n";
        // A file the reader would refuse is not written: the entries past
        // the budget are left out.
        if (out.size() + line.size() > kMaxHomebrewTitlesBytes) break;
        if (!try_append(out, line.data(), line.size())) return false;
        ++written;
    }
    return true;
}

bool parse_homebrew_titles(const std::string& text, std::vector<HomebrewTitle>& out) {
    out.clear();
    LineReader lines(text);
    std::string line;
    while (lines.next(line) && out.size() < kMaxHomebrewTitles) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        LineReader fields(line, '\t');
        std::string field;
        HomebrewTitle title;
        if (!fields.next(title.id) || !is_homebrew_id(title.id)) continue;
        while (fields.next(field)) add_name(title, field);
        if (!title.names.empty()) out.push_back(std::move(title));
    }
    return !out.empty();
}

}  // namespace riftwii
