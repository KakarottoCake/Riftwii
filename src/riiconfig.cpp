// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/riiconfig.hpp"

#include <cstdlib>
#include <set>

#include "pugixml.hpp"

namespace riftwii {
namespace {

// One option as Riivolution sees it: every pack's copy of it (same
// section and config id), in pack order.
struct Group {
    std::string id;
    std::vector<LaunchModel::OptionRef> members;
};

std::vector<Group> groups_of(const LaunchModel& model) {
    std::vector<Group> groups;
    std::vector<std::string> sections;
    for (std::size_t p = 0; p < model.packages.size(); ++p) {
        const LaunchPackage& pack = model.packages[p];
        if (!pack.valid || !pack.for_disc) continue;
        for (std::size_t o = 0; o < pack.package.options.size(); ++o) {
            const Option& opt = pack.package.options[o];
            std::size_t g = 0;
            while (g < groups.size() && !(groups[g].id == opt.config_id && sections[g] == opt.section)) ++g;
            if (g == groups.size()) {
                groups.push_back(Group{opt.config_id, {}});
                sections.push_back(opt.section);
            }
            groups[g].members.push_back(LaunchModel::OptionRef{p, o});
        }
    }
    return groups;
}

const Option& option_at(const LaunchModel& model, const LaunchModel::OptionRef& ref) {
    return model.packages[ref.package].package.options[ref.option];
}

}  // namespace

bool parse_riivolution_config(const std::string& xml, RiivolutionConfig& out) {
    out = RiivolutionConfig{};
    pugi::xml_document doc;
    if (!doc.load_buffer(xml.data(), xml.size())) return false;
    const pugi::xml_node root = doc.child("riivolution");
    if (!root || root.attribute("version").as_int(-1) != 2) return false;
    for (const pugi::xml_node n : root.children("option")) {
        const std::string id = n.attribute("id").as_string();
        const long value = std::strtol(n.attribute("default").as_string("0"), nullptr, 10);
        if (!id.empty()) out.options.emplace_back(id, value < 0 ? 0u : static_cast<unsigned>(value));
    }
    return true;
}

std::string write_riivolution_config(const RiivolutionConfig& config) {
    pugi::xml_document doc;
    pugi::xml_node decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    pugi::xml_node root = doc.append_child("riivolution");
    root.append_attribute("version") = 2;
    for (const auto& o : config.options) {
        pugi::xml_node n = root.append_child("option");
        n.append_attribute("id") = o.first.c_str();
        n.append_attribute("default") = o.second;
    }
    struct Writer : pugi::xml_writer {
        std::string text;
        void write(const void* data, std::size_t size) override { text.append(static_cast<const char*>(data), size); }
    } writer;
    doc.save(writer, "  ");
    return writer.text;
}

unsigned apply_riivolution_config(LaunchModel& model, const RiivolutionConfig& config) {
    unsigned applied = 0;
    std::vector<bool> touched(model.packages.size(), false);
    for (const Group& g : groups_of(model)) {
        // Riivolution takes the first entry with the option's id.
        const std::pair<std::string, unsigned>* entry = nullptr;
        for (const auto& o : config.options) {
            if (o.first == g.id) {
                entry = &o;
                break;
            }
        }
        if (entry == nullptr) continue;
        std::size_t total = 0;
        for (const auto& m : g.members) total += option_at(model, m).choices.size();
        if (entry->second > total) continue;
        std::size_t left = entry->second;
        for (const auto& m : g.members) {
            Option& opt = model.packages[m.package].package.options[m.option];
            const std::size_t n = opt.choices.size();
            opt.selected = left >= 1 && left <= n ? left : 0;
            left = left > n ? left - n : 0;
            touched[m.package] = true;
        }
        ++applied;
    }
    for (std::size_t p = 0; p < model.packages.size(); ++p) {
        if (!touched[p]) continue;
        bool any = false;
        for (const Option& o : model.packages[p].package.options) any = any || o.selected != 0;
        model.packages[p].enabled = any;
    }
    return applied;
}

RiivolutionConfig merge_riivolution_config(const LaunchModel& model, const RiivolutionConfig& existing) {
    RiivolutionConfig out = existing;
    std::set<std::string> written;
    for (const Group& g : groups_of(model)) {
        // Groups in different sections can share an id; the first one is
        // what Riivolution reads back.
        if (!written.insert(g.id).second) continue;
        unsigned value = 0;
        unsigned before = 0;
        for (const auto& m : g.members) {
            const Option& opt = option_at(model, m);
            if (value == 0 && model.packages[m.package].enabled && opt.selected != 0) {
                value = before + static_cast<unsigned>(opt.selected);
            }
            before += static_cast<unsigned>(opt.choices.size());
        }
        bool replaced = false;
        for (auto& o : out.options) {
            if (o.first == g.id) {
                o.second = value;
                replaced = true;
                break;
            }
        }
        if (!replaced) out.options.emplace_back(g.id, value);
    }
    return out;
}

std::string riivolution_config_name(const std::string& game_id) { return game_id.substr(0, 4); }

}  // namespace riftwii
