// SPDX-License-Identifier: GPL-3.0-or-later
#include "codebuilds.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

#include "log.hpp"
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
// shown as "Brawl Minus".
std::string key_of(const std::string& gct) { return gct.compare(0, 4, "sd:/") == 0 ? gct.substr(4) : gct; }
std::string top_of(const std::string& key) { return key.substr(0, key.find('/')); }
std::string dir_of(const std::string& path) { return path.substr(0, path.rfind('/')); }

bool listed(const std::vector<CodeBuildFile>& out, const std::string& gct) {
    for (const CodeBuildFile& b : out)
        if (lower(b.gct) == lower(gct)) return true;
    return false;
}

// The code files in `folder` named after a game.
void add_codes(const std::string& folder, std::vector<CodeBuildFile>& out) {
    for (const std::string& file : entries(folder)) {
        const std::string id = gct_game(file);
        if (id.empty() || out.size() >= kMaxBuilds) continue;
        const std::string gct = folder + "/" + file;
        const std::string key = key_of(gct);
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

// Where the code list goes for `game_id`: a gameconfig.txt in the code
// file's folder or a folder above it, up to the top of the card.
GameConfig load_config(const std::string& folder, const std::string& game_id, std::string& from) {
    std::vector<std::string> paths;
    for (std::string dir = folder; dir.size() > 4 && dir.compare(0, 4, "sd:/") == 0; dir = dir_of(dir)) {
        paths.push_back(dir + "/gameconfig.txt");
    }
    paths.push_back("sd:/gameconfig.txt");
    for (const std::string& path : paths) {
        const std::string text = read_text(path, 256 * 1024);
        if (text.empty()) continue;
        GameConfig c = parse_gameconfig(text, game_id);
        if (!c.found) continue;
        from = path;
        return c;
    }
    from.clear();
    return GameConfig();
}

std::string kb(std::size_t bytes) { return std::to_string((bytes + 1023) / 1024) + " KB"; }

}  // namespace

std::vector<CodeBuildFile> ListCodeBuilds() {
    std::vector<CodeBuildFile> out;
    for (const std::string& name : entries("sd:/")) {
        const std::string folder = "sd:/" + name;
        if (lower(name) == "riftwii" || !is_dir(folder)) continue;
        add_codes(folder, out);
        if (lower(name) != "codes") add_codes(folder + "/codes", out);
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
    std::vector<CodeBuildFile> found;
    add_codes(dir_of(gct), found);
    for (const CodeBuildFile& b : found)
        if (lower(b.gct) == lower(gct) && b.game_id == game_id) return true;  // listed by its name already
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
    const std::size_t room = out.list_start != 0 ? out.list_end - out.list_start : kCodeListEnd - kCodeListAddress;
    if (out.gct.size() > room) {
        error = out.list_start != 0
                    ? "The codes need " + kb(out.gct.size()) + ", more than the " + kb(room) + " " + config_from +
                          " makes room for. Turn off some cheats."
                    : out.names + "'s codes need " + kb(out.gct.size()) +
                          ": put the build's gameconfig.txt at the top of the SD card, which says where they go.";
        return false;
    }
    if (out.hooktype > 1 && out.hooktype != 7) {
        logf("Code builds: hook type %d is not supported, the video retrace runs the codes\n", out.hooktype);
    }
    return true;
}

}  // namespace riftwii::wii
