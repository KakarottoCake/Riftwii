// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/profiles.hpp"

#include <algorithm>
#include <sstream>

namespace riftwii {
namespace {

bool is_lower_or_digit(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

bool ends_with_png(const std::string& path) {
    if (path.size() < 4) return false;
    std::string tail = path.substr(path.size() - 4);
    for (char& c : tail)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return tail == ".png";
}

// The length of `text` cut to at most `limit` bytes without splitting a
// UTF-8 character (a continuation byte is never the first one kept out).
std::size_t utf8_cut(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text.size();
    std::size_t at = limit;
    while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) --at;
    return at;
}

}  // namespace

bool plausible_game_id(const std::string& id) {
    if (id.size() < 4 || id.size() > 6) return false;
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

bool valid_profile_slug(const std::string& slug) {
    if (slug.empty() || slug.size() > kMaxProfileSlug) return false;
    if (slug.front() == '-' || slug.back() == '-') return false;
    for (std::size_t i = 0; i < slug.size(); ++i) {
        const char c = slug[i];
        if (c == '-') {
            if (slug[i - 1] == '-') return false;
        } else if (!is_lower_or_digit(c)) {
            return false;
        }
    }
    return true;
}

bool valid_tile_key(const std::string& key) {
    const std::size_t at = key.find('@');
    if (at == std::string::npos) return plausible_game_id(key);
    return plausible_game_id(key.substr(0, at)) && valid_profile_slug(key.substr(at + 1));
}

std::string tile_game_id(const std::string& key) { return key.substr(0, key.find('@')); }

std::string clean_profile_name(const std::string& typed) {
    std::string out;
    bool space = false;
    for (char c : typed) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) continue;
        if (c == ' ') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    out.resize(utf8_cut(out, kMaxProfileName));
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string profile_slug(const std::string& name) {
    std::string slug;
    bool dash = false;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (is_lower_or_digit(c)) {
            if (dash && !slug.empty()) slug += '-';
            dash = false;
            slug += c;
        } else {
            dash = true;
        }
        if (slug.size() >= kMaxProfileSlug) break;
    }
    slug.resize(std::min(slug.size(), kMaxProfileSlug));
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug.empty() ? "mods" : slug;
}

bool valid_profile_art(const std::string& path) {
    if (path.empty() || path.size() > kMaxProfileArtPath) return false;
    if (path.compare(0, 4, "sd:/") != 0 && path.compare(0, 5, "usb:/") != 0) return false;
    if (path.find("..") != std::string::npos || !ends_with_png(path)) return false;
    for (char c : path)
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
    return true;
}

void ProfileList::parse(const std::string& text) {
    profiles_.clear();
    // A file past the limit is read that far, back to its last whole line:
    // a line cut in two would keep a shortened name or picture path.
    std::string head = text.substr(0, kMaxProfilesFile);
    if (text.size() > kMaxProfilesFile) head.resize(head.rfind('\n') == std::string::npos ? 0 : head.rfind('\n') + 1);
    std::istringstream in(head);
    std::string line;
    while (std::getline(in, line) && profiles_.size() < kMaxProfiles) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        // <key>\t<name>[\t<picture>]
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        const std::string key = line.substr(0, tab);
        std::string rest = line.substr(tab + 1);
        std::string art;
        const std::size_t tab2 = rest.find('\t');
        if (tab2 != std::string::npos) {
            art = rest.substr(tab2 + 1);
            rest.resize(tab2);
        }
        const std::size_t at = key.find('@');
        if (at == std::string::npos || !valid_tile_key(key) || find(key)) continue;
        Profile p;
        p.game_id = key.substr(0, at);
        p.slug = key.substr(at + 1);
        p.name = clean_profile_name(rest);
        if (p.name.empty() || for_game(p.game_id).size() >= kMaxProfilesPerGame) continue;
        if (valid_profile_art(art)) p.art = art;
        profiles_.push_back(std::move(p));
    }
}

std::string ProfileList::serialize() const {
    std::string out = "# RiftWii's extra Home tiles: key (game ID@name), the tile's name, the picture it shows\n";
    for (const Profile& p : profiles_) {
        out += p.key() + "\t" + p.name;
        if (!p.art.empty()) out += "\t" + p.art;
        out += "\n";
    }
    return out;
}

std::vector<const Profile*> ProfileList::for_game(const std::string& game_id) const {
    std::vector<const Profile*> out;
    for (const Profile& p : profiles_)
        if (p.game_id == game_id) out.push_back(&p);
    return out;
}

std::vector<const Profile*> ProfileList::tiles_for(const std::string& game_id, bool on_drive) const {
    if (!on_drive) return {};
    return for_game(game_id);
}

std::string launched_choices_key(const std::string& launched_id, const std::string& last_tile_text) {
    std::string key = last_tile_text.substr(0, last_tile_text.find('\n'));
    if (!key.empty() && key.back() == '\r') key.pop_back();
    const bool profile = key.find('@') != std::string::npos && valid_tile_key(key);
    return profile && !launched_id.empty() && tile_game_id(key) == launched_id ? key : launched_id;
}

const Profile* ProfileList::find(const std::string& key) const {
    for (const Profile& p : profiles_)
        if (p.key() == key) return &p;
    return nullptr;
}

ProfileList::AddResult ProfileList::add(const std::string& game_id, const std::string& name, const std::string& art,
                                        std::string& key) {
    key.clear();
    if (!plausible_game_id(game_id)) return AddResult::BadGame;
    Profile p;
    p.game_id = game_id;
    p.name = clean_profile_name(name);
    if (p.name.empty()) return AddResult::NoName;
    if (profiles_.size() >= kMaxProfiles) return AddResult::ListFull;
    if (for_game(game_id).size() >= kMaxProfilesPerGame) return AddResult::GameFull;
    // A second "Retro Rewind" gets "retro-rewind-2": the base cut short
    // enough that the number always fits.
    const std::string base = profile_slug(p.name);
    p.slug = base;
    for (int n = 2; find(p.key()); ++n) {
        const std::string suffix = "-" + std::to_string(n);
        std::string cut = base.substr(0, kMaxProfileSlug - suffix.size());
        while (!cut.empty() && cut.back() == '-') cut.pop_back();
        p.slug = cut + suffix;
    }
    if (valid_profile_art(art)) p.art = art;
    key = p.key();
    profiles_.push_back(std::move(p));
    return AddResult::Added;
}

bool ProfileList::rename(const std::string& key, const std::string& name) {
    const std::string clean = clean_profile_name(name);
    if (clean.empty()) return false;
    for (Profile& p : profiles_) {
        if (p.key() != key) continue;
        p.name = clean;
        return true;
    }
    return false;
}

bool ProfileList::remove(const std::string& key) {
    const auto it = std::find_if(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return p.key() == key; });
    if (it == profiles_.end()) return false;
    profiles_.erase(it);
    return true;
}

}  // namespace riftwii
