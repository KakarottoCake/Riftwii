// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameextras.hpp"

#include <cstdio>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <sys/stat.h>

#include "boot.hpp"
#include "codebuilds.hpp"
#include "i18n.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "online.hpp"
#include "textfile.hpp"
#include "riftwii/homeorder.hpp"
#include "riftwii/settingsfile.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kHistoryPath = "sd:/riftwii/history.txt";
PlayHistory g_history;
bool g_history_loaded = false;
constexpr const char* kHomeOrderPath = "sd:/riftwii/order.txt";
std::vector<std::string> g_home_order;
bool g_home_order_loaded = false;
bool g_home_order_unreadable = false;  // there, but not read: never written over

bool read_text(const std::string& path, std::string& text) {
    return ReadTextFile(path, text);
}

}  // namespace

bool LoadGameCheats(const std::string& game_id, int revision, bool download, CheatFile& out, std::string& status) {
    out = CheatFile{};
    if (game_id.empty()) {
        status = tr("No game is selected.");
        return false;
    }
    const std::string path = CheatPath(game_id);
    std::string text;
    bool have = read_text(path, text);
    if (!have && download && Settings().online) {
        std::string error;
        if (DownloadCheats(game_id, error)) have = read_text(path, text);
        else {
            logf("Cheats: not downloaded for %s: %s\n", game_id.c_str(), error.c_str());
            status = tr("No cheats found online for this game.");
            return false;
        }
    }
    if (!have) {
        status = Settings().online ? tr("No cheat file yet. Choose Download to get one.")
                                   : tr("No cheat file at {1}", {path.substr(3)});
        return false;
    }
    std::string error;
    if (!parse_cheat_text(text, out, error, revision)) {
        // Melee's file has sections for 1.0 and 1.1 only: a 1.02 disc gets
        // neither, as their addresses are another release's.
        if (out.other_revision_cheats != 0) {
            logf("Cheats: %s has %u cheat(s), all of them for other releases than this disc's (revision %d)\n",
                 path.c_str(), static_cast<unsigned>(out.other_revision_cheats), revision);
            status = tr("The cheat file only has cheats for other versions of this game.");
        } else {
            status = tr("The cheat file has no cheats in it: {1}", {path.substr(3)});
        }
        return false;
    }
    return true;
}

std::string BorderNote(const std::string& game_id) {
    std::string text;
    if (game_id.empty() || !read_text("sd:/riftwii/choices/" + game_id + ".video", text)) return "";
    const bool side = text.find("side_borders = yes") != std::string::npos;
    const bool top = text.find("top_borders = yes") != std::string::npos;
    if (side && top) return tr("Last time, this game left black borders on all sides.");
    if (side) return tr("Last time, this game left black borders at the sides.");
    if (top) return tr("Last time, this game left black borders at the top and bottom.");
    return tr("Last time, this game filled the whole screen.");
}

const PlayHistory& History() {
    if (!g_history_loaded) {
        g_history_loaded = true;
        std::string text;
        if (read_text(kHistoryPath, text)) g_history.parse(text);
    }
    return g_history;
}

namespace {
void WriteHistory() {
    mkdir("sd:/riftwii", 0777);
    const std::string text = g_history.serialize();
    if (FILE* f = std::fopen(kHistoryPath, "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}
}  // namespace

void RecordPlay(const std::string& game_id) {
    if (Settings().play_history == "off") return;  // Settings > Play history
    History();
    g_history.record(game_id, static_cast<std::int64_t>(std::time(nullptr)));
    WriteHistory();
}

void RememberLaunchedTile(const std::string& key) {
    mkdir("sd:/riftwii", 0777);
    if (FILE* f = std::fopen(kLastTilePath, "wb")) {  // riftwii/profiles.hpp
        const std::string line = key + "\n";
        std::fwrite(line.data(), 1, line.size(), f);
        std::fclose(f);
    }
}

void ForgetPlays(const std::string& game_id) {
    if (!History().find(game_id)) return;
    g_history.forget(game_id);
    WriteHistory();
}

namespace {

// Reads at most kHomeOrderMaxBytes of the order file, however large it
// is: a file grown past that still gives its first games, cut back to
// its last whole line. False when it is there but cannot be read.
bool ReadHomeOrderFile(std::vector<std::string>& order) {
    struct stat st;
    if (stat(kHomeOrderPath, &st) != 0) return true;  // no file yet: nothing listed
    FILE* f = std::fopen(kHomeOrderPath, "rb");
    if (!f) return false;
    std::string head(kHomeOrderMaxBytes, '\0');
    const std::size_t got = std::fread(&head[0], 1, head.size(), f);
    const bool failed = std::ferror(f) != 0;
    // One byte more than the cap means the file went on past it.
    const bool cut = got == kHomeOrderMaxBytes && std::fgetc(f) != EOF;
    std::fclose(f);
    if (failed) return false;
    head.resize(got);
    if (cut)
        logf("Home order: %s is larger than %u KiB, so only its first %u KiB are read\n", kHomeOrderPath,
             static_cast<unsigned>(kHomeOrderMaxBytes >> 10), static_cast<unsigned>(kHomeOrderMaxBytes >> 10));
    order = parse_home_order(home_order_whole_lines(head, cut));
    return true;
}

}  // namespace

const std::vector<std::string>& HomeOrder() {
    if (!g_home_order_loaded) {
        g_home_order_loaded = true;
        g_home_order_unreadable = !ReadHomeOrderFile(g_home_order);
        if (g_home_order_unreadable)
            logf("Home order: %s is there but could not be read, so moves are not saved over it\n", kHomeOrderPath);
        else
            logf("Home order: %u game(s) in %s\n", static_cast<unsigned>(g_home_order.size()), kHomeOrderPath);
    }
    return g_home_order;
}

void SaveHomeOrder(const std::vector<std::string>& order) {
    HomeOrder();
    g_home_order = order;
    // A file that could not be read is kept: writing the moves over it
    // would lose every place it holds.
    if (g_home_order_unreadable) {
        logf("Home order: the move lasts until RiftWii closes; %s is left as it was\n", kHomeOrderPath);
        return;
    }
    mkdir("sd:/riftwii", 0777);
    // Written beside it and renamed over it, so a card pulled mid-write
    // leaves the old order whole (as settings.txt is written).
    const std::string temp = std::string(kHomeOrderPath) + ".part";
    const std::string text = serialize_home_order(order);
    FILE* f = std::fopen(temp.c_str(), "wb");
    bool ok = f != nullptr;
    if (f) {
        ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        ok = std::fclose(f) == 0 && ok;
    }
    if (ok) {
        std::remove(kHomeOrderPath);
        ok = std::rename(temp.c_str(), kHomeOrderPath) == 0;
    }
    if (!ok) {
        std::remove(temp.c_str());
        logf("Home order: %s could not be written, so the new order lasts until RiftWii closes\n", kHomeOrderPath);
        return;
    }
    logf("Home order: %u game(s) written to %s\n", static_cast<unsigned>(std::min(order.size(), kHomeOrderMaxGames)),
         kHomeOrderPath);
}

std::string PlayNote(const std::string& game_id) {
    if (Settings().play_history == "off") return "";
    const PlayRecord* r = History().find(game_id);
    if (!r || r->count == 0) return "";
    const std::time_t when = static_cast<std::time_t>(r->last);
    struct tm local;
    localtime_r(&when, &local);
    const std::string day = tr("{1}/{2}", {std::to_string(local.tm_mon + 1), std::to_string(local.tm_mday)});
    if (r->count == 1) return tr("Played once, on {1}", {day});
    return tr("Played {1} times, last on {2}", {std::to_string(r->count), day});
}

namespace {

// The cheats picked for the game, as a code list (empty when none).
std::vector<std::uint8_t> CheatGct(const FrontendState& state, std::size_t& count, bool log) {
    count = 0;
    if (!state.model.game.cheats || state.model.game.cheat_names.empty()) return {};
    CheatFile file;
    std::string status;
    if (!LoadGameCheats(state.game_id, state.game_revision, false, file, status)) {
        if (log) logf("Cheats: none loaded for %s: %s\n", state.game_id.c_str(), status.c_str());
        return {};
    }
    std::vector<std::uint8_t> gct = build_gct(file, state.model.game.cheat_names, count);
    if (log)
        logf("Cheats: %u of %u picked for %s\n", static_cast<unsigned>(count),
             static_cast<unsigned>(state.model.game.cheat_names.size()), state.game_id.c_str());
    if (count == 0) gct.clear();
    return gct;
}

}  // namespace

bool CheckCodeBuilds(const FrontendState& state, std::string& error) {
    if (state.model.code_builds().empty()) return true;
    std::size_t count = 0;
    CodeBuildLaunch launch;
    return PrepareCodeBuilds(state.model, state.game_id, CheatGct(state, count, false), launch, error);
}

void PrepareLaunchExtras(const FrontendState& state, const HeadlessLaunch* headless) {
    LaunchExtras extras;
    extras.game_id = state.game_id;
    extras.video = effective_video(state.model.game, Settings());
    extras.language = effective_game_language(state.model.game, Settings());
    extras.server = effective_wfc_server(state.model.game, Settings());
    extras.region_video = state.model.game.region_video == "on";
    extras.pack_keeps_saves = !state.model.pack_save_owner().empty();
    // The game's own choice, or Settings' for every game.
    const std::string& aspect = state.model.game.aspect == "global" ? Settings().aspect : state.model.game.aspect;
    extras.aspect = aspect == "4:3" ? 0 : aspect == "16:9" ? 1 : -1;
    // The game's own Rumble, or Settings > Wii Remote rumble for every game.
    extras.rumble_off = state.model.game.rumble == "off" || Settings().wiimote_rumble == "off";
    extras.speaker_off = state.model.game.speaker == "off";
    extras.region_strings = state.model.game.region_strings == "on";
    extras.wfc_domain = wfc_domain(extras.server, Settings().wfc_domain);
    const std::string& adapter = Settings().gc_adapter;
    extras.gc_adapter = adapter == "on"     ? GcAdapterMode::On
                        : adapter == "demo" ? GcAdapterMode::Demo
                        : adapter == "off"  ? GcAdapterMode::Off
                                            : GcAdapterMode::Auto;
    extras.gc_rumble_off = Settings().gc_rumble == "off";
    extras.screenshots = Settings().screenshots == "on" || Settings().screenshots == "demo";
    extras.screenshots_demo = Settings().screenshots == "demo";
    extras.cheat_gct = CheatGct(state, extras.cheat_count, true);
    CodeBuildLaunch builds;
    std::string error;
    if (PrepareCodeBuilds(state.model, state.game_id, extras.cheat_gct, builds, error)) {
        extras.cheat_gct = std::move(builds.gct);
        extras.code_builds = builds.names;
        extras.code_build_in_image = builds.in_image;
        extras.code_build_image = builds.image;
        extras.code_list_start = builds.list_start;
        extras.code_list_end = builds.list_end;
        extras.code_hooktype = builds.hooktype;
        extras.pokes = std::move(builds.pokes);
        // The menu asks at Start; a launch without the menu (autorun, another
        // loader) goes by settings.txt "code_build_extras = off".
        const auto setting = Settings().other.find("code_build_extras");
        const bool setting_off = setting != Settings().other.end() && setting->second == "off";
        extras.code_build_plain = state.code_build_plain || setting_off;
    } else {
        logf("Code builds are off: %s\n", error.c_str());  // the game page checked before Start
    }
    if (headless) {
        if (!headless->wfc_domain.empty() && extras.server == WfcServer::Custom) extras.wfc_domain = headless->wfc_domain;
        // A code build (Project+) brings its own codes; the other loader's
        // cheats only count without one.
        const bool no_build = extras.code_builds.empty();
        if (no_build && headless->gct == "none") {
            extras.cheat_gct.clear();
            extras.cheat_count = 0;
        } else if (no_build && !headless->gct.empty()) {
            std::ifstream in(headless->gct, std::ios::binary);
            std::vector<std::uint8_t> gct((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            // A GCT starts 00D0C0DE 00D0C0DE and ends F0000000 00000000.
            static const std::uint8_t kMagic[8] = {0x00, 0xD0, 0xC0, 0xDE, 0x00, 0xD0, 0xC0, 0xDE};
            if (gct.size() >= 16 && gct.size() % 8 == 0 && std::equal(kMagic, kMagic + 8, gct.begin())) {
                extras.cheat_count = gct.size() / 8 - 2;
                extras.cheat_gct = std::move(gct);
                logf("Cheats: %s (%u lines)\n", headless->gct.c_str(), static_cast<unsigned>(extras.cheat_count));
            } else {
                logf("Cheats: %s is missing or not a GCT file; no cheats\n", headless->gct.c_str());
                extras.cheat_gct.clear();
                extras.cheat_count = 0;
            }
        }
        extras.return_to = headless->return_to;
        extras.return_to_menu = headless->return_to_menu;
    }
    SetLaunchExtras(std::move(extras));
}

}  // namespace riftwii::wii
