// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "codebuilds.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

#include "i18n.hpp"
#include "log.hpp"
#include "vsdimage.hpp"
#include "riftwii/cheats.hpp"
#include "riftwii/codehook.hpp"

namespace riftwii::wii {
namespace {

constexpr std::size_t kMaxGctBytes = 1024 * 1024;   // far more than any build's code list has room for
constexpr std::size_t kMaxBuilds = 32;
constexpr const char* kPicksPath = "sd:/riftwii/codebuilds.txt";

bool read_file(const std::string& path, std::size_t limit, std::vector<std::uint8_t>& out) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::uint8_t buf[4096];
    bool ok = true;
    for (;;) {
        const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
        if (n == 0) break;
        if (out.size() + n > limit) {
            ok = false;
            break;
        }
        out.insert(out.end(), buf, buf + n);
    }
    std::fclose(f);
    return ok;
}

std::string read_text(const std::string& path, std::size_t limit) {
    std::vector<std::uint8_t> bytes;
    if (!read_file(path, limit, bytes)) return "";
    return std::string(bytes.begin(), bytes.end());
}

bool is_dir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "RSBE01.GCT" (any case): the game ID, upper case; empty for other names.
std::string gct_game(const std::string& name) {
    if (name.size() != 10 || lower(name.substr(6)) != ".gct") return "";
    std::string id = name.substr(0, 6);
    for (char& c : id) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return "";
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return id;
}

std::vector<std::string> entries(const std::string& dir) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.empty() || name[0] == '.') continue;  // also macOS's "._" copies
        out.push_back(name);
    }
    closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

// "sd:/Brawl Minus/codes/RSBE01.gct": "Brawl Minus/codes/RSBE01.gct",
// shown as "Brawl Minus"; "vsd:/Project+/RSBE01.GCT" in the image
// pm.raw: "pm.raw/Project+/RSBE01.GCT", shown as "Project+".
bool in_image(const std::string& path) { return path.compare(0, 5, kVsdDrive) == 0; }
std::string key_of(const std::string& gct, const std::string& image = "") {
    if (gct.compare(0, 4, "sd:/") == 0) return gct.substr(4);
    if (in_image(gct) && !image.empty()) return image + "/" + gct.substr(5);
    return gct;
}
std::string top_of(std::string key) {
    const std::string image = VsdImageOfKey(key);
    if (!image.empty()) key = key.substr(image.size() + 1);
    return key.substr(0, key.find('/'));
}
std::string dir_of(const std::string& path) { return path.substr(0, path.rfind('/')); }
// "sd:/" or "vsd:/": the drive a path is on.
std::string drive_of(const std::string& path) { return path.substr(0, path.find('/') + 1); }

bool listed(const std::vector<CodeBuildFile>& out, const std::string& gct) {
    for (const CodeBuildFile& b : out)
        if (lower(b.gct) == lower(gct)) return true;
    return false;
}

// The code files in `folder` named after a game; `image` the virtual SD
// card's image `folder` is in ("" for the SD card itself).
void add_codes(const std::string& folder, std::vector<CodeBuildFile>& out, const std::string& image) {
    for (const std::string& file : entries(folder)) {
        const std::string id = gct_game(file);
        if (id.empty() || out.size() >= kMaxBuilds) continue;
        const std::string gct = folder + "/" + file;
        const std::string key = key_of(gct, image);
        out.push_back({key, top_of(key), folder, gct, id});
    }
}

// "RSBE01<tab>sd:/path/file.gct" lines.
std::vector<std::pair<std::string, std::string>> read_picks() {
    std::vector<std::pair<std::string, std::string>> picks;
    std::istringstream lines(read_text(kPicksPath, 64 * 1024));
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0 || line.compare(tab + 1, 4, "sd:/") != 0) continue;
        picks.emplace_back(line.substr(0, tab), line.substr(tab + 1));
    }
    return picks;
}

bool write_picks(const std::vector<std::pair<std::string, std::string>>& picks, std::string& error) {
    mkdir("sd:/riftwii", 0777);
    FILE* f = std::fopen(kPicksPath, "wb");
    if (!f) {
        error = std::string("Cannot write ") + kPicksPath + ".";
        return false;
    }
    std::string text;
    for (const auto& p : picks) text += p.first + "\t" + p.second + "\n";
    const bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    if (std::fclose(f) != 0 || !ok) {
        error = std::string("Cannot write ") + kPicksPath + ".";
        return false;
    }
    return true;
}

// gameconfig.txt and gc.txt files anywhere under `dir`, a few folders
// deep: a mod's download unzipped as it came may keep its gameconfig.txt
// beside the codes folder rather than above it.
void find_configs(const std::string& dir, int depth, std::vector<std::string>& out) {
    for (const std::string& name : entries(dir)) {
        if (out.size() >= 32) return;
        const std::string path = dir + "/" + name;
        if (is_dir(path)) {
            if (depth > 0) find_configs(path, depth - 1, out);
        } else if (lower(name) == "gameconfig.txt" || lower(name) == "gc.txt") {
            out.push_back(path);
        }
    }
}

// Where the code list goes for `game_id`: a gameconfig.txt (or gc.txt, as
// Project+ names it) in the code file's folder or a folder above it, up to
// the mod's folder at the top of the card, or anywhere inside that
// folder; then the top of the card and its codes folder, where USB
// loaders keep theirs.
GameConfig load_config(const std::string& folder, const std::string& game_id, std::string& from) {
    const auto first_found = [&](const std::vector<std::string>& paths, GameConfig& out) {
        for (const std::string& path : paths) {
            const std::string text = read_text(path, 256 * 1024);
            if (text.empty()) continue;
            GameConfig c = parse_gameconfig(text, game_id);
            if (!c.found) continue;
            from = path;
            out = c;
            return true;
        }
        return false;
    };
    std::vector<std::string> paths;
    const std::string drive = drive_of(folder);
    std::string top;  // the mod's folder at the top of the card
    for (std::string dir = folder; dir.size() > drive.size() && dir.compare(0, drive.size(), drive) == 0;
         dir = dir_of(dir)) {
        paths.push_back(dir + "/gameconfig.txt");
        paths.push_back(dir + "/gc.txt");
        top = dir;
    }
    GameConfig c;
    if (first_found(paths, c)) return c;
    // The mod's own copy deeper in its folder wins over one shared by every
    // mod at the top. Only searched when the folders above the code file
    // have none: inside an image every directory read seeks through the
    // image file, and a whole Project+ folder took minutes on a Wii.
    paths.clear();
    if (!top.empty()) find_configs(top, 3, paths);
    paths.push_back(drive + "gameconfig.txt");
    paths.push_back(drive + "codes/gameconfig.txt");
    if (first_found(paths, c)) return c;
    from.clear();
    return GameConfig();
}

// "3.3 KB": one decimal, so a list just over the room never reads as
// the same size as the room.
std::string kb(std::size_t bytes) {
    const std::size_t tenths = bytes * 10 / 1024;
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " KB";
}

}  // namespace

std::vector<CodeBuildFile> ListCodeBuilds() {
    std::vector<CodeBuildFile> out;
    const auto scan = [&](const std::string& drive, const std::string& image) {
        for (const std::string& name : entries(drive)) {
            const std::string folder = drive + name;
            if (lower(name) == "riftwii" || !is_dir(folder)) continue;
            add_codes(folder, out, image);
            if (lower(name) != "codes") add_codes(folder + "/codes", out, image);
        }
    };
    scan("sd:/", "");
    // Every virtual SD card, one mounted at a time.
    for (const VsdImageFile& f : ListVsdImages()) {
        if (out.size() >= kMaxBuilds) break;
        VsdMount image(f.name);
        if (image.ok()) scan(kVsdDrive, f.name);
    }
    for (const auto& pick : read_picks()) {
        if (out.size() >= kMaxBuilds || listed(out, pick.second) || !exists(pick.second)) continue;
        const std::string key = key_of(pick.second);
        CodeBuildFile b{key, top_of(key), dir_of(pick.second), pick.second, pick.first};
        b.picked = true;
        out.push_back(b);
    }
    return out;
}

bool PickCodeBuild(const std::string& game_id, const std::string& gct, std::string& key, std::string& error) {
    key = key_of(gct);
    std::vector<std::uint8_t> bytes;
    if (!read_file(gct, kMaxGctBytes, bytes) || !valid_gct(bytes)) {
        error = gct + " is not a Gecko code file (GCT).";
        return false;
    }
    for (const auto& pick : read_picks())
        if (pick.first == game_id && lower(pick.second) == lower(gct)) return true;
    // Only the scan decides what is listed by its name already: a <game ID>.gct
    // deeper than it looks still has to be picked.
    for (const CodeBuildFile& b : ListCodeBuilds())
        if (lower(b.gct) == lower(gct) && b.game_id == game_id) return true;
    std::vector<std::pair<std::string, std::string>> picks = read_picks();
    picks.emplace_back(game_id, gct);
    return write_picks(picks, error);
}

bool ForgetCodeBuild(const std::string& key, std::string& error) {
    std::vector<std::pair<std::string, std::string>> picks = read_picks();
    const auto gone = std::remove_if(picks.begin(), picks.end(),
                                     [&](const auto& p) { return lower(key_of(p.second)) == lower(key); });
    if (gone == picks.end()) return true;
    picks.erase(gone, picks.end());
    return write_picks(picks, error);
}

bool IsPickedCodeBuild(const std::string& key) {
    for (const auto& pick : read_picks())
        if (lower(key_of(pick.second)) == lower(key)) return true;
    return false;
}

std::vector<CodeBrowseEntry> BrowseForCodes(const std::string& folder) {
    std::vector<CodeBrowseEntry> folders, files;
    const std::string base = folder.back() == '/' ? folder : folder + "/";
    for (const std::string& name : entries(folder)) {
        if (base == "sd:/" && lower(name) == "riftwii") continue;  // RiftWii's own
        if (is_dir(base + name)) {
            if (folders.size() < 200) folders.push_back({name, true});
        } else if (name.size() > 4 && lower(name.substr(name.size() - 4)) == ".gct" && files.size() < 200) {
            files.push_back({name, false});
        }
    }
    folders.insert(folders.end(), files.begin(), files.end());
    return folders;
}

bool PrepareCodeBuilds(const LaunchModel& model, const std::string& game_id, const std::vector<std::uint8_t>& cheats,
                       CodeBuildLaunch& out, std::string& error) {
    out = CodeBuildLaunch();
    const std::vector<const LaunchPackage*> builds = model.code_builds();
    if (builds.empty()) {
        out.gct = cheats;
        return true;
    }
    std::vector<std::uint8_t> codes;
    std::string config_from;
    GameConfig config;
    // The image's builds read their files from the image, the others from
    // the SD card: the game sees one card, so one image or the SD card.
    std::size_t from_image = 0;
    for (const LaunchPackage* b : builds) {
        if (!in_image(b->gct_path)) continue;
        ++from_image;
        const std::string image = VsdImageOfKey(b->file);
        if (out.image.empty()) out.image = image;
        if (lower(image) != lower(out.image)) {
            error = "Code builds from two virtual SD cards (" + out.image + " and " + image +
                    ") can't be on together. Turn one of them off.";
            return false;
        }
    }
    if (from_image != 0 && from_image != builds.size()) {
        error = "Code builds inside " + out.image + " can't be turned on together with ones on the SD card.";
        return false;
    }
    out.in_image = from_image != 0;
    VsdMount image(out.in_image ? out.image : std::string());  // mounted only for the image's builds
    if (out.in_image && !image.ok()) {
        error = "Cannot read " + out.image + ", the virtual SD card these code builds are in.";
        return false;
    }
    for (const LaunchPackage* b : builds) {
        const std::string name = top_of(b->file);
        std::vector<std::uint8_t> bytes;
        if (!read_file(b->gct_path, kMaxGctBytes, bytes)) {
            error = "Cannot read " + b->gct_path + ".";
            return false;
        }
        if (!valid_gct(bytes)) {
            error = b->gct_path + " is not a Gecko code file (GCT).";
            return false;
        }
        codes = join_gct(codes, bytes);
        out.names += (out.names.empty() ? "" : ", ") + name;
        if (config_from.empty()) config = load_config(dir_of(b->gct_path), game_id, config_from);
    }
    out.gct = join_gct(codes, cheats);
    if (config.found) {
        out.list_start = config.codelist_start;
        out.list_end = config.codelist_end;
        out.hooktype = config.hooktype;
        out.pokes = config.pokes;
        logf("Code builds: %s, settings from %s (list 0x%08x-0x%08x, hook type %d, %u poke(s))\n", out.names.c_str(),
             config_from.c_str(), static_cast<unsigned>(out.list_start), static_cast<unsigned>(out.list_end),
             out.hooktype, static_cast<unsigned>(out.pokes.size()));
        for (const std::string& line : config.ignored) logf("  gameconfig line not used: %s\n", line.c_str());
    } else {
        logf("Code builds: %s, no gameconfig.txt section for %s\n", out.names.c_str(), game_id.c_str());
    }
    if (out.list_start != 0 && (out.list_end <= out.list_start)) {
        error = config_from + " gives codeliststart but no codelistend after it.";
        return false;
    }
    // "sd:/wp-combined-mods": the first build's folder at the top of the
    // card, where its own gameconfig.txt goes.
    std::string mod_folder;
    {
        const std::string& gct = builds.front()->gct_path;
        const std::string drive = drive_of(gct);
        const std::string rest = gct.substr(drive.size());
        const std::size_t slash = rest.find('/');
        mod_folder = drive + (slash == std::string::npos ? std::string() : rest.substr(0, slash));
    }
    const std::size_t room = out.list_start != 0 ? out.list_end - out.list_start : kCodeListEnd - kCodeListAddress;
    if (out.gct.size() > room) {
        // Said in a popup when Start is pressed (wii/rift_menu.cpp).
        error = out.list_start != 0
                    ? tr("The codes take {1}, but {2} only makes room for {3}. Turn off some cheats on this game's Cheats page, or turn off a code mod.",
                         {kb(out.gct.size()), config_from, kb(room)})
                    : tr("{1} has more codes than the game has room for ({2}, room for {3}). It needs the file gameconfig.txt (or gc.txt) from the same download as the mod. Copy it into the mod's own folder on the SD card, {4}, so it can't replace another mod's. Not in the download? Ask whoever made the mod.",
                         {out.names, kb(out.gct.size()), kb(room), mod_folder});
        return false;
    }
    if (out.hooktype > 1 && out.hooktype != 7) {
        logf("Code builds: hook type %d is not supported, the video retrace runs the codes\n", out.hooktype);
    }
    return true;
}

}  // namespace riftwii::wii
