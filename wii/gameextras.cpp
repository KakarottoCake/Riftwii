// SPDX-License-Identifier: GPL-3.0-or-later
#include "gameextras.hpp"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include "boot.hpp"
#include "i18n.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "online.hpp"
#include "riftwii/settingsfile.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kHistoryPath = "sd:/riftwii/history.txt";
PlayHistory g_history;
bool g_history_loaded = false;

bool read_text(const std::string& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream s;
    s << in.rdbuf();
    text = s.str();
    return true;
}

}  // namespace

bool LoadGameCheats(const std::string& game_id, bool download, CheatFile& out, std::string& status) {
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
                                   : tr("No cheat file. Put one at {1}", {path.substr(3)});
        return false;
    }
    std::string error;
    if (!parse_cheat_text(text, out, error)) {
        status = tr("The cheat file has no cheats in it: {1}", {path.substr(3)});
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

void RecordPlay(const std::string& game_id) {
    History();
    g_history.record(game_id, static_cast<std::int64_t>(std::time(nullptr)));
    mkdir("sd:/riftwii", 0777);
    const std::string text = g_history.serialize();
    if (FILE* f = std::fopen(kHistoryPath, "wb")) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}

std::string PlayNote(const std::string& game_id) {
    const PlayRecord* r = History().find(game_id);
    if (!r || r->count == 0) return "";
    const std::time_t when = static_cast<std::time_t>(r->last);
    struct tm local;
    localtime_r(&when, &local);
    const std::string day = tr("{1}/{2}", {std::to_string(local.tm_mon + 1), std::to_string(local.tm_mday)});
    if (r->count == 1) return tr("Played once, on {1}", {day});
    return tr("Played {1} times, last on {2}", {std::to_string(r->count), day});
}

void PrepareLaunchExtras(const FrontendState& state) {
    LaunchExtras extras;
    extras.game_id = state.game_id;
    extras.video = effective_video(state.model.game, Settings());
    const std::string& adapter = Settings().gc_adapter;
    extras.gc_adapter = adapter == "on" ? GcAdapterMode::On : adapter == "demo" ? GcAdapterMode::Demo : GcAdapterMode::Off;
    if (state.model.game.cheats && !state.model.game.cheat_names.empty()) {
        CheatFile file;
        std::string status;
        if (LoadGameCheats(state.game_id, false, file, status)) {
            extras.cheat_gct = build_gct(file, state.model.game.cheat_names, extras.cheat_count);
            if (extras.cheat_count == 0) extras.cheat_gct.clear();
            logf("Cheats: %u of %u picked for %s\n", static_cast<unsigned>(extras.cheat_count),
                 static_cast<unsigned>(state.model.game.cheat_names.size()), state.game_id.c_str());
        } else {
            logf("Cheats: none loaded for %s: %s\n", state.game_id.c_str(), status.c_str());
        }
    }
    SetLaunchExtras(std::move(extras));
}

}  // namespace riftwii::wii
