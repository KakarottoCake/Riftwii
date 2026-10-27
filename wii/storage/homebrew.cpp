// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "homebrew.hpp"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <cstring>

#include <algorithm>
#include <cstddef>
#include <utility>

#include "channel.hpp"
#include "gameinfo.hpp"
#include "log.hpp"
#include "riftwii/appmeta.hpp"
#include "riftwii/homebrewmatch.hpp"
#include "textfile.hpp"

namespace riftwii::wii {
namespace {

constexpr std::size_t kMaxApps = 120;
constexpr std::size_t kProblemChars = 120;

struct Scan {
    std::vector<HomebrewApp> apps;
    bool read = false;
    bool done = false;
    unsigned matched_with = 0;  // HomebrewTitlesWritten() at the last match
};

Scan& scan() {
    static Scan state;
    return state;
}

HomebrewApp* to_start() {
    static HomebrewApp app;
    return &app;
}

void add_app(Scan& state, const std::string& badge, const std::string& path, const std::string& folder) {
    struct stat info {};
    std::string program;
    bool elf = false;
    if (stat((path + "/boot.dol").c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
        program = path + "/boot.dol";
    } else if (stat((path + "/boot.elf").c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
        program = path + "/boot.elf";
        elf = true;
    } else {
        return;
    }

    AppMeta meta;
    std::string text;
    if (ReadTextFile(path + "/meta.xml", text)) {
        if (!parse_app_meta(text, meta))
            logf("Homebrew: %s/meta.xml cannot be read as XML; the folder's name is used\n", path.c_str());
    }

    HomebrewApp app;
    app.name = app_display_name(meta, folder);
    if (app.name.empty()) app.name = folder;
    app.folder = path;
    app.program = program;
    app.device = badge;
    app.coder = meta.coder;
    app.version = meta.version;
    app.date = app_release_date(meta.release_date);
    app.description = meta.description;
    app.arguments = meta.arguments;
    app.meta_name = meta.name;
    if (elf) {
        app.problem = "this app's program is a boot.elf, and RiftWii starts boot.dol apps only";
    } else {
        app.path = program;
    }
    if (app.problem.size() > kProblemChars) app.problem.resize(kProblemChars);
    state.apps.push_back(std::move(app));
}

void scan_drive(Scan& state, const char* device, const char* badge) {
    const std::string root = std::string(device) + "/apps";
    DIR* dir = opendir(root.c_str());
    if (dir == nullptr) return;
    state.read = true;
    while (dirent* entry = readdir(dir)) {
        const std::string folder = entry->d_name;
        if (folder.empty() || folder == "." || folder == ".." || folder[0] == '.') continue;
        if (strcasecmp(folder.c_str(), "riftwii") == 0) continue;
        const std::string path = root + "/" + folder;
        struct stat info {};
        if (stat(path.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) continue;
        if (state.apps.size() >= kMaxApps) {
            logf("Homebrew: more than %u apps; the rest are left out\n", static_cast<unsigned>(kMaxApps));
            break;
        }
        add_app(state, badge, path, folder);
    }
    closedir(dir);
}

// Each app's GameTDB entry, by its meta.xml name, else its folder's
// name. Nothing when GameTDB's list is not on the card yet.
void match_apps(Scan& state) {
    state.matched_with = HomebrewTitlesWritten();
    std::vector<HomebrewTitle> titles;
    if (!LoadHomebrewTitles(titles)) {
        for (HomebrewApp& app : state.apps) app.gametdb_id.clear();
        return;
    }
    const HomebrewMatcher matcher(titles);
    unsigned matched = 0;
    for (HomebrewApp& app : state.apps) {
        const std::size_t slash = app.folder.find_last_of('/');
        const std::string folder_name = slash == std::string::npos ? app.folder : app.folder.substr(slash + 1);
        app.gametdb_id = matcher.match(app.meta_name, folder_name);
        if (!app.gametdb_id.empty()) ++matched;
    }
    logf("Homebrew: %u of %u app(s) found in GameTDB's list of %u, for their covers\n", matched,
         static_cast<unsigned>(state.apps.size()), static_cast<unsigned>(titles.size()));
}

}

void RescanHomebrewApps() {
    Scan& state = scan();
    state.apps.clear();
    state.read = false;
    scan_drive(state, "sd:", "SD");
    // USB game volumes are not libfat devices. Until app reads use the
    // volume reader too, only offer binaries the stdio launcher can open.
    std::stable_sort(state.apps.begin(), state.apps.end(), [](const HomebrewApp& a, const HomebrewApp& b) {
        const int by = strcasecmp(a.name.c_str(), b.name.c_str());
        return by != 0 ? by < 0 : strcasecmp(a.folder.c_str(), b.folder.c_str()) < 0;
    });
    state.done = true;
    logf("Homebrew: %u app(s) in sd:/apps%s\n", static_cast<unsigned>(state.apps.size()),
         state.read ? "" : " (the apps folder could not be opened)");
    if (!state.apps.empty()) match_apps(state);
}

bool HomebrewMatchStale() {
    const Scan& state = scan();
    return state.done && state.matched_with != HomebrewTitlesWritten();
}

void MatchHomebrewApps() {
    Scan& state = scan();
    if (state.done && !state.apps.empty()) match_apps(state);
}

const std::vector<HomebrewApp>& HomebrewApps() {
    Scan& state = scan();
    if (!state.done) RescanHomebrewApps();
    return state.apps;
}

bool HomebrewFoldersRead() { return scan().read; }

void SetHomebrewToStart(const HomebrewApp& app) { *to_start() = app; }

const HomebrewApp& HomebrewToStart() { return *to_start(); }

bool StartHomebrew(const HomebrewApp& app, std::string& error) {
    if (!app.problem.empty()) {
        error = app.problem;
        return false;
    }
    if (app.path.empty()) {
        error = app.folder + " has no boot.dol to start";
        return false;
    }
    const std::string line = riftwii::app_command_line(app.path, app.arguments);
    if (line.empty()) {
        error = "the app path is empty, contains a NUL or exceeds the command-line limit";
        return false;
    }
    logf("Homebrew: starting %s (%s, %u argument(s))\n", app.name.c_str(), app.path.c_str(),
         static_cast<unsigned>(app.arguments.size()));
    return StartHomebrewApp(app.path, line, error);
}

}
