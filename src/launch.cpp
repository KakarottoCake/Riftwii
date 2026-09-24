// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/launch.hpp"

#include <sstream>

namespace riftwii {
namespace {

std::string option_key(const Option& o) {
    return o.section + "/" + o.name;
}

// Tabs and newlines cannot appear in a saved field; they are folded to
// spaces so a line stays one fact.
std::string clean(std::string s) {
    for (char& c : s) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

// A one-option, one-choice package has no choice for the user to make.  Its
// sole choice is the enabled state.  Keep this in one helper so live toggles
// and restored legacy choices files get identical behaviour.
void select_simple_choice_on_enable(LaunchPackage& package) {
    if (package.package.options.size() != 1) return;
    Option& option = package.package.options.front();
    if (option.choices.size() == 1 && option.selected == 0) option.selected = 1;
}

// For XML that does not parse: the game its <id> element names, read
// straight from the text, so a broken pack for another game stays hidden
// just like a working one. Empty when no game attribute can be found.
std::string sniff_game(const std::string& xml) {
    for (std::size_t at = xml.find("<id"); at != std::string::npos; at = xml.find("<id", at + 3)) {
        const char after = at + 3 < xml.size() ? xml[at + 3] : '\0';
        if (after != ' ' && after != '\t' && after != '\r' && after != '\n') continue;
        const std::size_t end = xml.find('>', at);
        const std::string tag = xml.substr(at, end == std::string::npos ? std::string::npos : end - at);
        for (std::size_t g = tag.find("game"); g != std::string::npos; g = tag.find("game", g + 4)) {
            const char before = tag[g - 1];
            if (before != ' ' && before != '\t' && before != '\r' && before != '\n') continue;
            std::size_t i = g + 4;
            while (i < tag.size() && (tag[i] == ' ' || tag[i] == '\t')) ++i;
            if (i >= tag.size() || tag[i] != '=') continue;
            ++i;
            while (i < tag.size() && (tag[i] == ' ' || tag[i] == '\t')) ++i;
            if (i >= tag.size() || (tag[i] != '"' && tag[i] != '\'')) continue;
            const std::size_t close = tag.find(tag[i], i + 1);
            if (close == std::string::npos) return "";
            return tag.substr(i + 1, close - i - 1);
        }
        return "";
    }
    return "";
}

}  // namespace

bool same_disc_identity(const DiscIdentity& left, const DiscIdentity& right) {
    return left.id == right.id && left.revision == right.revision && left.number == right.number;
}

bool needs_launch_pipeline(bool has_selected_packages, const std::string& save_mode) {
    return has_selected_packages || save_mode == "separate" || save_mode == "fresh";
}

void PackIndex::add(const std::string& xml) {
    Package package;
    std::string error;
    DiscFilter filter;
    if (parse_package(xml, package, error)) {
        filter = package.filter;
    } else {
        filter.game = sniff_game(xml);
        if (filter.game.empty()) return;  // no game named: not listed under any
    }
    filter.revision = -1;
    filter.number = -1;
    filters_.push_back(std::move(filter));
}

bool PackIndex::has_packs(const std::string& game_id) const {
    DiscIdentity disc;
    disc.id = game_id;
    for (const DiscFilter& f : filters_) {
        if (f.matches(disc)) return true;
    }
    return false;
}

void LaunchModel::add(const std::string& file, const std::string& path, const std::string& xml,
                      const DiscIdentity* disc) {
    LaunchPackage p;
    p.file = file;
    p.path = path;
    std::string error;
    p.valid = parse_package(xml, p.package, error);
    if (!p.valid) {
        p.detail = error;
        const std::string game = sniff_game(xml);
        p.for_disc = disc == nullptr || game.empty() ||
                     (game.size() <= 6 && disc->id.compare(0, game.size(), game) == 0);
        if (!game.empty()) p.detail += " (for " + game + ")";
    } else {
        p.for_disc = disc == nullptr || p.package.filter.matches(*disc);
        std::size_t choices = 0;
        for (const Option& o : p.package.options) choices += o.choices.size();
        p.detail = std::to_string(p.package.options.size()) + " option(s), " + std::to_string(choices) +
                   " choice(s), " + std::to_string(p.package.patches.size()) + " patch definition(s)";
        if (!p.for_disc) p.detail += "; for " + (p.package.filter.game.empty() ? std::string("another disc") : p.package.filter.game);
        if (!p.package.warnings.empty()) {
            p.detail += "; " + std::to_string(p.package.warnings.size()) + " ignored: " + p.package.warnings.front();
        }
    }
    packages.push_back(std::move(p));
}

bool LaunchModel::set_enabled(std::size_t package, bool enabled) {
    if (package >= packages.size()) return false;
    LaunchPackage& p = packages[package];
    if (enabled && (!p.valid || !p.for_disc)) return false;
    p.enabled = enabled;
    // Most single-choice packages mean "turn this patch on".  Selecting
    // that one choice here removes an otherwise surprising second step in
    // the UI.  Leave richer packages alone: their choices can be mutually
    // exclusive or independently meaningful and must stay explicit.
    if (enabled) select_simple_choice_on_enable(p);
    return true;
}

std::vector<LaunchModel::OptionRef> LaunchModel::merge_group(std::size_t package, std::size_t option) const {
    std::vector<OptionRef> group;
    if (package >= packages.size() || option >= packages[package].package.options.size()) return group;
    const Option& self = packages[package].package.options[option];
    const LaunchPackage& owner = packages[package];
    if (self.id.empty() || !owner.valid || !owner.for_disc) return {{package, option}};
    for (std::size_t i = 0; i < packages.size(); ++i) {
        const LaunchPackage& p = packages[i];
        if (!p.valid || !p.for_disc) continue;
        for (std::size_t k = 0; k < p.package.options.size(); ++k) {
            const Option& o = p.package.options[k];
            if (i == package && k == option) {
                group.push_back({i, k});
            } else if (i != package && o.id == self.id && o.section == self.section) {
                group.push_back({i, k});
                break;  // one copy per pack
            }
        }
    }
    return group;
}

bool LaunchModel::option_shown(std::size_t package, std::size_t option) const {
    for (const OptionRef& r : merge_group(package, option)) {
        if (r.package == package) return true;
        if (packages[r.package].enabled) return false;
    }
    return true;
}

bool LaunchModel::cycle(std::size_t package, std::size_t option, int direction) {
    if (package >= packages.size()) return false;
    LaunchPackage& p = packages[package];
    if (!p.valid || option >= p.package.options.size()) return false;
    const std::vector<OptionRef> group = merge_group(package, option);
    if (group.size() > 1) {
        // One list: off, then each pack's choices in pack order.
        std::vector<std::pair<OptionRef, std::size_t>> states{{group.front(), 0}};
        std::size_t at = 0;
        for (const OptionRef& r : group) {
            const Option& o = packages[r.package].package.options[r.option];
            for (std::size_t c = 1; c <= o.choices.size(); ++c) {
                if (at == 0 && o.selected == c && packages[r.package].enabled) at = states.size();
                states.push_back({r, c});
            }
        }
        const std::size_t n = states.size();
        at = direction >= 0 ? (at + 1) % n : (at + n - 1) % n;
        for (const OptionRef& r : group) packages[r.package].package.options[r.option].selected = 0;
        if (at != 0) {
            const OptionRef r = states[at].first;
            packages[r.package].package.options[r.option].selected = states[at].second;
            packages[r.package].enabled = true;
        }
        return true;
    }
    Option& o = p.package.options[option];
    const std::size_t states = o.choices.size() + 1;  // off plus each choice
    if (direction >= 0) {
        o.selected = (o.selected + 1) % states;
    } else {
        o.selected = (o.selected + states - 1) % states;
    }
    return true;
}

std::string LaunchModel::pack_save_owner() const {
    for (const LaunchPackage& p : packages) {
        if (!p.valid || !p.for_disc || !p.enabled) continue;
        for (const Option& o : p.package.options) {
            if (o.selected == 0 || o.selected > o.choices.size()) continue;
            for (const std::string& id : o.choices[o.selected - 1].patches) {
                const auto it = p.package.patches.find(id);
                if (it != p.package.patches.end() && !it->second.savegames.empty()) return p.file;
            }
        }
    }
    return "";
}

std::string LaunchModel::choice_name(std::size_t package, std::size_t option) const {
    if (package >= packages.size()) return "";
    const LaunchPackage& p = packages[package];
    if (!p.valid || option >= p.package.options.size()) return "";
    const std::vector<OptionRef> group = merge_group(package, option);
    if (group.size() > 1) {
        for (const OptionRef& r : group) {
            const Option& o = packages[r.package].package.options[r.option];
            if (packages[r.package].enabled && o.selected != 0 && o.selected <= o.choices.size())
                return o.choices[o.selected - 1].name;
        }
        return "Off";
    }
    const Option& o = p.package.options[option];
    if (o.selected == 0 || o.selected > o.choices.size()) return "Off";
    return o.choices[o.selected - 1].name;
}

std::vector<PackageChoices> LaunchModel::selections() const {
    std::vector<PackageChoices> out;
    for (const LaunchPackage& p : packages) {
        if (!p.enabled || !p.valid || !p.for_disc) continue;
        PackageChoices s;
        s.xml_sd_path = p.path;
        for (const Option& o : p.package.options) {
            const std::string choice = (o.selected == 0 || o.selected > o.choices.size()) ? "" : o.choices[o.selected - 1].name;
            s.choices.emplace_back(option_key(o), choice);
        }
        out.push_back(std::move(s));
    }
    return out;
}

SaveOverride resolve_save_override(const std::string& save_mode, const std::string& xml_dir,
                                   const std::string& game_id) {
    SaveOverride none;
    if (!xml_dir.empty() || game_id.empty()) return none;
    if (save_mode == "separate") {
        const std::string dir = "sd:/riftwii/saves/" + game_id + "/clone";
        SaveOverride o;
        o.dir = dir;
        o.clone = true;
        o.note = "saves separate at " + dir + " (NAND progress cloned in once)";
        return o;
    }
    if (save_mode == "fresh") {
        const std::string dir = "sd:/riftwii/saves/" + game_id + "/fresh";
        SaveOverride o;
        o.dir = dir;
        o.clone = false;
        o.note = "saves separate at " + dir + " (fresh start)";
        return o;
    }
    return none;
}

std::string LaunchModel::save() const {
    std::string text = "*riftwii*\tsaves\t" + save_mode + "\n";
    for (const LaunchPackage& p : packages) {
        // Only this game's packs: the file is per game ID, and another
        // game's pack could not be turned on here anyway.
        if (!p.valid || !p.for_disc) continue;
        text += clean(p.file) + "\t" + (p.enabled ? "on" : "off") + "\n";
        for (const Option& o : p.package.options) {
            const std::string choice = (o.selected == 0 || o.selected > o.choices.size()) ? "" : o.choices[o.selected - 1].name;
            text += clean(p.file) + "\t" + clean(option_key(o)) + "\t" + clean(choice) + "\n";
        }
    }
    return text;
}

void LaunchModel::restore(const std::string& text) {
    // Apply choices before package on/off state.  A legacy enabled simple
    // package may have an explicitly saved empty choice from before the
    // one-choice convenience existed; normalize that exact shape after all
    // records are known, regardless of their line ordering.
    std::vector<int> restored_enabled(packages.size(), -1);
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        const std::string file = line.substr(0, t1);
        if (file == "*riftwii*") {
            // Reserved for loader settings (a package file with this exact
            // name would collide, which FAT allows but nobody does).
            // Unknown settings stay untouched.
            const std::size_t t2 = line.find('\t', t1 + 1);
            if (t2 != std::string::npos && line.substr(t1 + 1, t2 - t1 - 1) == "saves") {
                const std::string mode = line.substr(t2 + 1);
                if (mode == "nand" || mode == "separate" || mode == "fresh") save_mode = mode;
            }
            continue;
        }
        std::size_t index = packages.size();
        for (std::size_t i = 0; i < packages.size(); ++i) {
            if (packages[i].file == file) {
                index = i;
                break;
            }
        }
        if (index == packages.size() || !packages[index].valid) continue;
        const std::size_t t2 = line.find('\t', t1 + 1);
        if (t2 == std::string::npos) {
            const std::string state = line.substr(t1 + 1);
            if (state == "on") restored_enabled[index] = 1;
            else if (state == "off") restored_enabled[index] = 0;
            continue;
        }
        const std::string option = line.substr(t1 + 1, t2 - t1 - 1);
        const std::string choice = line.substr(t2 + 1);
        std::string error;
        select_choice(packages[index].package, option, choice, error);  // unknown: ignored
    }
    for (std::size_t i = 0; i < packages.size(); ++i) {
        if (restored_enabled[i] < 0) continue;
        if (restored_enabled[i] != 0 && packages[i].for_disc) {
            packages[i].enabled = true;
            select_simple_choice_on_enable(packages[i]);
        } else {
            // Package Off is non-destructive: leave every saved choice as it
            // was, including the one choice of a simple package.
            packages[i].enabled = false;
        }
    }
}

}  // namespace riftwii
