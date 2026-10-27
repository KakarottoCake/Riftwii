// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// GameTDB lists about 700 homebrew apps in wiitdb.xml (<type>Homebrew</type>,
// four-character IDs such as 9XGX for Snes9x GX) and keeps covers for many
// of them. An app's folder in sd:/apps has no such ID, only the name its
// meta.xml gives, so the Homebrew view finds each app's entry by name: the
// meta.xml name first, the folder's name as a weaker fallback, both
// compared loosely (case, spaces, punctuation and version numbers aside,
// a letter or two off).
//
// Network data: the count of apps, of names an app and of bytes a name
// are bounded, and an ID must look like one (4 capital letters or digits).
namespace riftwii {

struct HomebrewTitle {
    std::string id;
    std::vector<std::string> names;  // the entry's name and its titles in each language
};

constexpr std::size_t kMaxHomebrewTitles = 2048;
constexpr std::size_t kMaxHomebrewNames = 4;
constexpr std::size_t kMaxHomebrewNameBytes = 96;

// Reads one wiitdb.xml <game> block; false when it is not a homebrew app
// with a usable ID and at least one name.
bool parse_wiitdb_homebrew(const std::string& block, HomebrewTitle& out);

// What two names are compared by: the ASCII letters and digits, lowercase,
// each other character as one '#', the words joined, without bracketed
// parts ("(EN)", "[mod]") and words that are a version ("1.2.3", "v4",
// "r1250", "beta"). "Snes9x GX 4.5.3",
// "SNES9xGX (EN)" and "snes9xgx" all give "snes9xgx". Empty when the
// name has no ASCII letter or digit.
std::string homebrew_match_key(const std::string& name);

// How alike two keys are, 0 to 1: 1 less the letters to change, add or
// drop (Levenshtein) for each letter of the longer one. Below `at_least`
// may answer 0 without counting it out.
double homebrew_key_likeness(const std::string& a, const std::string& b, double at_least = 0.0);

// The entries' keys made once, for matching every app on the card.
class HomebrewMatcher {
public:
    // A meta.xml name matches at this likeness or above; the folder's name
    // (a weak hint: "apps/wiimc", "apps/usbloader_gx") needs more.
    static constexpr double kNameLikeness = 0.85;
    static constexpr double kFolderLikeness = 0.92;
    // Keys shorter than this match only exactly: "pix" is one letter from
    // too many other names.
    static constexpr std::size_t kMinFuzzyKey = 5;

    explicit HomebrewMatcher(const std::vector<HomebrewTitle>& titles);

    // The GameTDB ID for an app, "" when no entry is close enough or two
    // entries are equally close and not exact.
    std::string match(const std::string& meta_name, const std::string& folder_name) const;

    bool empty() const { return keys_.empty(); }

private:
    // How many of each letter, digit and other character a key has.
    static constexpr std::size_t kSymbols = 26 + 10 + 1;
    using Counts = std::array<std::uint8_t, kSymbols>;
    static Counts count_letters(const std::string& key);
    struct Key {
        std::string key;
        std::string plain;  // the name in lowercase, its spaces trimmed
        Counts counts;
        std::size_t title;  // index into ids_
    };
    std::string best(const std::string& name, const std::string& key, double at_least) const;
    std::vector<std::string> ids_;
    std::vector<Key> keys_;
};

// The index of GameTDB's homebrew kept on the card
// (sd:/riftwii/homebrew-titles.txt): one line an app, its ID and its
// names separated by tabs. Reading it back is as careful as reading the
// database.
std::string serialize_homebrew_titles(const std::vector<HomebrewTitle>& titles);
bool parse_homebrew_titles(const std::string& text, std::vector<HomebrewTitle>& out);

}  // namespace riftwii
