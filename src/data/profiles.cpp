// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/profiles.hpp"

#include <algorithm>

#include "riftwii/textlines.hpp"
#include "riftwii/imagevolume.hpp"

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

std::string normalize_gametdb_id(std::string id) {
    if (id.size() != 6) return {};
    for (char& character : id)
        if (character >= 'a' && character <= 'z') character = static_cast<char>(character - 'a' + 'A');
    return plausible_game_id(id) ? id : std::string();
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

static std::string clean_name(const std::string& typed, std::size_t limit) {
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
    out.resize(utf8_cut(out, limit));
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string clean_profile_name(const std::string& typed) { return clean_name(typed, kMaxProfileName); }

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
    if (path.compare(0, 4, "sd:/") != 0 && !riftwii::is_usb_path(path)) return false;
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
    LineReader in(head);
    std::string line;
    while (in.next(line) && profiles_.size() < kMaxProfiles) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        // Older files stop after the name or picture.
        LineReader fields(line, '\t');
        std::string key, name, art, entry_id, name_source, name_before, extra;
        if (!fields.next(key) || !fields.next(name)) continue;
        fields.next(art);
        fields.next(entry_id);
        fields.next(name_source);
        fields.next(name_before);
        const std::size_t at = key.find('@');
        if (at == std::string::npos || !valid_tile_key(key) || find(key)) continue;
        Profile p;
        p.game_id = key.substr(0, at);
        p.slug = key.substr(at + 1);
        // A field this RiftWii doesn't know: the line is from a newer one,
        // so its entry choice is left out rather than half understood.
        if (!fields.next(extra)) {
            p.gametdb_id = normalize_gametdb_id(entry_id);
            p.use_entry_name = name_source == "gametdb" && !p.gametdb_id.empty();
            if (p.use_entry_name) p.name_before_entry = clean_profile_name(name_before);
        }
        p.name = clean_name(name, p.use_entry_name ? kMaxProfileEntryName : kMaxProfileName);
        if (p.name.empty() || for_game(p.game_id).size() >= kMaxProfilesPerGame) continue;
        if (valid_profile_art(art)) p.art = art;
        profiles_.push_back(std::move(p));
    }
}

std::string ProfileList::serialize() const {
    std::string out = "# RiftWii's extra Home tiles: key, name, picture, GameTDB ID, name source\n";
    for (const Profile& p : profiles_) {
        out += p.key() + "\t" + p.name;
        if (!p.art.empty() || !p.gametdb_id.empty() || p.use_entry_name) out += "\t" + p.art;
        if (!p.gametdb_id.empty() || p.use_entry_name) out += "\t" + p.gametdb_id;
        if (p.use_entry_name) out += "\tgametdb";
        if (p.use_entry_name && !p.name_before_entry.empty()) out += "\t" + p.name_before_entry;
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
                                        std::string& key, const std::string& entry_id, bool use_entry_name,
                                        const std::string& name_before_entry) {
    key.clear();
    if (!plausible_game_id(game_id)) return AddResult::BadGame;
    Profile p;
    p.game_id = game_id;
    p.gametdb_id = normalize_gametdb_id(entry_id);
    p.use_entry_name = use_entry_name && !p.gametdb_id.empty();
    if (p.use_entry_name) p.name_before_entry = clean_profile_name(name_before_entry);
    p.name = clean_name(name, p.use_entry_name ? kMaxProfileEntryName : kMaxProfileName);
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
        p.use_entry_name = false;
        p.name_before_entry.clear();
        return true;
    }
    return false;
}

bool ProfileList::set_gametdb_entry(const std::string& key, const std::string& id, const std::string& title,
                                    const std::string& fallback_name) {
    const std::string entry_id = normalize_gametdb_id(id);
    if (!id.empty() && entry_id.empty()) return false;
    for (Profile& profile : profiles_) {
        if (profile.key() != key) continue;
        profile.gametdb_id = entry_id;
        if (!profile.use_entry_name) return true;
        if (!entry_id.empty()) {
            const std::string name = clean_name(title, kMaxProfileEntryName);
            if (!name.empty()) profile.name = name;
            return true;
        }
        // Back to Automatic: the entry's title goes with the entry.
        std::string name = profile.name_before_entry;
        if (name.empty()) name = clean_profile_name(fallback_name);
        if (!name.empty()) profile.name = name;
        else profile.name = clean_profile_name(profile.name);
        profile.use_entry_name = false;
        profile.name_before_entry.clear();
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
