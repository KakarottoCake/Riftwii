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

}  // namespace

void LaunchModel::add(const std::string& file, const std::string& path, const std::string& xml,
                      const DiscIdentity* disc) {
    LaunchPackage p;
    p.file = file;
    p.path = path;
    std::string error;
    p.valid = parse_package(xml, p.package, error);
    if (!p.valid) {
        p.detail = error;
        p.for_disc = false;
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
    return true;
}

bool LaunchModel::cycle(std::size_t package, std::size_t option, int direction) {
    if (package >= packages.size()) return false;
    LaunchPackage& p = packages[package];
    if (!p.valid || option >= p.package.options.size()) return false;
    Option& o = p.package.options[option];
    const std::size_t states = o.choices.size() + 1;  // off plus each choice
    if (direction >= 0) {
        o.selected = (o.selected + 1) % states;
    } else {
        o.selected = (o.selected + states - 1) % states;
    }
    return true;
}

std::string LaunchModel::choice_name(std::size_t package, std::size_t option) const {
    if (package >= packages.size()) return "";
    const LaunchPackage& p = packages[package];
    if (!p.valid || option >= p.package.options.size()) return "";
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

std::string LaunchModel::save() const {
    std::string text;
    for (const LaunchPackage& p : packages) {
        if (!p.valid) continue;
        text += clean(p.file) + "\t" + (p.enabled ? "on" : "off") + "\n";
        for (const Option& o : p.package.options) {
            const std::string choice = (o.selected == 0 || o.selected > o.choices.size()) ? "" : o.choices[o.selected - 1].name;
            text += clean(p.file) + "\t" + clean(option_key(o)) + "\t" + clean(choice) + "\n";
        }
    }
    return text;
}

void LaunchModel::restore(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::size_t t1 = line.find('\t');
        if (t1 == std::string::npos) continue;
        const std::string file = line.substr(0, t1);
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
            if (state == "on") set_enabled(index, true);
            else if (state == "off") set_enabled(index, false);
            continue;
        }
        const std::string option = line.substr(t1 + 1, t2 - t1 - 1);
        const std::string choice = line.substr(t2 + 1);
        std::string error;
        select_choice(packages[index].package, option, choice, error);  // unknown: ignored
    }
}

}  // namespace riftwii
