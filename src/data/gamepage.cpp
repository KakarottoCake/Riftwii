// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gamepage.hpp"

#include "riftwii/profiles.hpp"
#include "riftwii/gccard.hpp"
#include "riftwii/gcbackup.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/imagevolume.hpp"

#include <algorithm>
#include <cctype>

namespace riftwii {
namespace {

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool safe_leaf(const std::string& name) {
    if (name.empty() || name.size() > 240 || name.back() == '.' || name.back() == ' ' ||
        name.find_first_of("/\\:") != std::string::npos) return false;
    for (unsigned char c : name)
        if (c < 0x20 || c == 0x7F) return false;
    return true;
}

bool safe_image_path(const std::string& path) {
    if (path.size() > 240) return false;
    const std::size_t colon = path.find(":/");
    if (colon == std::string::npos) return false;
    const std::string device = path.substr(0, colon);
    unsigned partition = 0;
    if (device != "sd" && !usb_path_partition(path, partition)) return false;
    std::size_t begin = colon + 2;
    while (begin < path.size()) {
        const std::size_t end = path.find('/', begin);
        const std::string leaf = path.substr(begin, end == std::string::npos ? end : end - begin);
        if (!safe_leaf(leaf)) return false;
        if (end == std::string::npos) return true;
        begin = end + 1;
    }
    return false;
}

std::string preference(const LoaderSettings& settings, const std::string& key) {
    const auto found = settings.other.find(key);
    return found == settings.other.end() ? std::string() : found->second;
}

}  // namespace

bool game_page_id(const std::string& id) {
    if (id.size() != 4 && id.size() != 6) return false;
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

std::string game_page_name(const LoaderSettings& settings, const std::string& id, const std::string& original) {
    if (!game_page_id(id)) return original;
    const std::string name = clean_profile_name(preference(settings, "game_name_" + id));
    return name.empty() ? original : name;
}

void game_page_rename(LoaderSettings& settings, const std::string& id, const std::string& name) {
    if (!game_page_id(id)) return;
    const std::string cleaned = clean_profile_name(name);
    if (cleaned.empty()) settings.other.erase("game_name_" + id);
    else settings.other["game_name_" + id] = cleaned;
}

bool game_page_hidden(const LoaderSettings& settings, const std::string& id) {
    return game_page_id(id) && preference(settings, "game_hidden_" + id) == "on";
}

void game_page_hide(LoaderSettings& settings, const std::string& id, bool hidden) {
    if (!game_page_id(id)) return;
    if (hidden) settings.other["game_hidden_" + id] = "on";
    else settings.other.erase("game_hidden_" + id);
}

bool game_page_show_hidden(const LoaderSettings& settings) {
    return preference(settings, "show_hidden_games") == "on";
}

bool game_deletion_files(const std::string& primary, const std::vector<std::string>& siblings, GameDeletion& out) {
    out = {};
    if (!safe_image_path(primary) || siblings.size() > 4096) return false;
    const std::size_t slash = primary.find_last_of('/');
    const std::string directory = primary.substr(0, slash);
    const std::string leaf = primary.substr(slash + 1);
    const std::string name = lower(leaf);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = name.substr(dot);
    if (ext != ".wbfs" && ext != ".iso" && ext != ".rvz" && ext != ".gcm") return false;
    if (!try_reserve(out.images, siblings.size() + 1)) return false;
    out.images.push_back(primary);
    const std::string split_stem = name.substr(0, dot) + ".wbf";
    for (const std::string& sibling : siblings) {
        if (!safe_leaf(sibling) || directory.size() + 1 + sibling.size() > 240) continue;
        const std::string candidate = lower(sibling);
        bool belongs = false;
        if (ext == ".wbfs" && candidate.compare(0, split_stem.size(), split_stem) == 0) {
            const std::string number = candidate.substr(split_stem.size());
            belongs = !number.empty() && number.size() <= 3 && number[0] != '0';
            for (char c : number)
                if (c < '0' || c > '9') belongs = false;
        }
        // Nintendont's two-disc layout names both images, rather than
        // splitting one image. Only that layout grants this second file.
        if ((name == "game.iso" || name == "game.gcm" || name == "game.rvz") &&
            (candidate == "disc2.iso" || candidate == "disc2.gcm" || candidate == "disc2.rvz")) belongs = true;
        const std::string path = directory + "/" + sibling;
        if (belongs && std::find(out.images.begin(), out.images.end(), path) == out.images.end()) out.images.push_back(path);
    }
    const std::size_t root = directory.find(":/");
    // Keep the top-level games/wbfs/custom folder, even if it becomes empty.
    if (directory.find('/', root + 2) != std::string::npos) out.empty_folder = directory;
    return true;
}

std::vector<std::string> game_deletion_extras(const std::string& id, bool saves) {
    if (!game_page_id(id)) return {};
    std::vector<std::string> paths;
    for (const char* extension : {".rwc", ".png", ".none", ".second", ".rwc-second"})
        paths.push_back("sd:/riftwii/covers/" + id + extension);
    paths.push_back("sd:/riftwii/boxes/" + id + ".rw2");
    paths.push_back("sd:/riftwii/boxes/" + id + ".none");
    paths.push_back("sd:/riftwii/discs/" + id + ".png");
    paths.push_back("sd:/riftwii/discs/" + id + ".none");
    for (const char* extension : {".txt", ".source"}) paths.push_back("sd:/riftwii/cheats/" + id + extension);
    if (saves) {
        paths.push_back("sd:/riftwii/saves/" + id);
        paths.push_back(gc_card_path(id, false));
        paths.push_back(gc_backup_folder(id, false));
    }
    return paths;
}

}  // namespace riftwii
