// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend.hpp"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

#include "autorun.hpp"

namespace riftwii::wii {

namespace {
constexpr std::size_t kMaxPackages = 150;  // the option browser's limit
}

void IdentifyDisc(FrontendState& state) {
    std::string error;
    if (ProbeInserted(state.game_id, state.disc_title, error)) {
        state.disc_status = "Disc: " + state.game_id + "  " + state.disc_title;
        state.choices_path = std::string(kChoicesDir) + "/" + state.game_id + ".txt";
    } else {
        state.game_id.clear();
        state.choices_path.clear();
        state.disc_status = "No disc identified (" + error + "); Launch needs one";
    }
}

std::string ScanPackages(FrontendState& state) {
    state.model.packages.clear();
    std::unique_ptr<DIR, int (*)(DIR*)> dir(opendir(kPackageDir), closedir);
    if (!dir) {
        return errno == ENOENT ? "Create sd:/riivolution for XML packages" : "Cannot read SD package directory";
    }
    std::vector<std::string> names;
    bool limited = false;
    while (true) {
        errno = 0;
        const dirent* ent = readdir(dir.get());
        if (!ent) {
            if (errno != 0) return "SD directory read failed; rescan to retry";
            break;
        }
        const char* dot = std::strrchr(ent->d_name, '.');
        if (!dot || strcasecmp(dot, ".xml") != 0) continue;
        if (names.size() == kMaxPackages) {
            limited = true;
            break;
        }
        names.push_back(ent->d_name);
    }
    std::sort(names.begin(), names.end());
    DiscIdentity identity;
    const DiscIdentity* disc = nullptr;
    if (!state.game_id.empty()) {
        identity.id = state.game_id;
        disc = &identity;
    }
    for (const std::string& name : names) {
        const std::string path = std::string(kPackageDir) + "/" + name;
        struct stat info;
        if (stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) continue;
        std::ifstream input(path, std::ios::binary);
        std::stringstream text;
        if (input) text << input.rdbuf();
        state.model.add(name, path, input ? text.str() : std::string(), disc);
    }
    if (!state.choices_path.empty()) {
        std::ifstream saved(state.choices_path, std::ios::binary);
        if (saved) {
            std::stringstream text;
            text << saved.rdbuf();
            state.model.restore(text.str());
        }
    }
    if (limited) return "First 150 packages shown; directory limit reached";
    if (state.model.packages.empty()) return "No XML packages in sd:/riivolution";
    return "A: enable or disable   +: options   Launch (1): the enabled packages, or the plain disc";
}

void SaveChoices(const FrontendState& state) {
    if (state.choices_path.empty()) return;
    mkdir(kChoicesDir, 0777);
    std::ofstream out(state.choices_path, std::ios::binary | std::ios::trunc);
    if (out) out << state.model.save();
}

}  // namespace riftwii::wii
