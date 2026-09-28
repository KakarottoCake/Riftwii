// SPDX-License-Identifier: GPL-3.0-or-later
#include "headless.hpp"

#include <gccore.h>

#include <cctype>
#include <cstdio>
#include <fstream>

#include "autorun.hpp"
#include "frontend.hpp"
#include "gameextras.hpp"
#include "ios_reload.hpp"
#include "log.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kLogPath = "sd:/riftwii/boot.log";
constexpr const char* kLaunchFile = "sd:/riftwii/launch.txt";

bool same_path(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// The image path= names, else one whose ID is game= (all six characters,
// or the first four).
bool wanted(const HeadlessLaunch& h, const ImageGame& g) {
    if (!h.path.empty() && !same_path(g.path, h.path)) return false;
    return h.game.empty() || g.id.compare(0, h.game.size(), h.game) == 0;
}

// Finds the game on the drive `from` names, else on the USB drive, the SD
// card and the disc, in that order.
bool select_game(const HeadlessLaunch& h, FrontendState& state, std::string& error) {
    const std::string what = h.path.empty() ? h.game : h.path;
    if (h.from.empty() || h.from == "usb") {
        std::string scan_error;
        if (scan_usb_games(state.usb_catalog, scan_error)) {
            for (std::size_t i = 0; i < state.usb_catalog.games.size(); ++i) {
                if (wanted(h, state.usb_catalog.games[i])) return SelectUsbGame(state, i, error);
            }
        }
        if (h.from == "usb") {
            error = what + " is not on the USB drive" + (scan_error.empty() ? "" : " (" + scan_error + ")");
            return false;
        }
    }
    if (h.from.empty() || h.from == "sd") {
        std::string scan_error;
        if (scan_sd_games(state.sd_catalog, scan_error)) {
            for (std::size_t i = 0; i < state.sd_catalog.games.size(); ++i) {
                if (wanted(h, state.sd_catalog.games[i])) return SelectSdGame(state, i, error);
            }
        }
        if (h.from == "sd") {
            error = what + " is not on the SD card" + (scan_error.empty() ? "" : " (" + scan_error + ")");
            return false;
        }
    }
    IdentifyDisc(state);
    if (state.game_id.empty() || state.game_id.compare(0, h.game.size(), h.game) != 0) {
        error = h.game.empty() ? std::string("there is no game in the disc drive")
                               : h.game + " was not found on the USB drive, the SD card or in the disc drive";
        return false;
    }
    return true;
}

// A pack named by its file name alone (sd:/riivolution/ctgp.xml for
// "ctgp.xml"), in any folder the packs were found in.
bool same_pack(const std::string& path, const std::string& xml) {
    if (xml.find_first_of(":/") != std::string::npos) return same_path(path, xml);
    const std::size_t slash = path.find_last_of('/');
    return same_path(slash == std::string::npos ? path : path.substr(slash + 1), xml);
}

// The other loader's picks over RiftWii's saved ones: the packs it named
// on and every other one off, its settings where it gave one.
bool apply_choices(const HeadlessLaunch& h, FrontendState& state, std::string& error) {
    LaunchModel& model = state.model;
    if (h.packs_given) {
        // xml=all: each pack for this game that can be used.
        for (std::size_t i = 0; i < model.packages.size(); ++i) model.set_enabled(i, h.all_packs);
        for (const std::string& xml : h.xmls) {
            bool found = false;
            for (std::size_t i = 0; i < model.packages.size() && !found; ++i) {
                if (!same_pack(model.packages[i].path, xml)) continue;
                found = true;
                if (!model.set_enabled(i, true)) {
                    const LaunchPackage& p = model.packages[i];
                    error = xml + ": " + (!p.valid ? p.detail : "this pack is not for " + state.game_id);
                    return false;
                }
            }
            if (!found) {
                error = xml + " was not found (packs go in sd:/riivolution)";
                return false;
            }
        }
    }
    const GameSettings& g = h.settings;
    GameSettings& to = model.game;
    if (g.video_mode != "global") to.video_mode = g.video_mode;
    if (g.video_width != "global") to.video_width = g.video_width;
    if (g.deflicker != "global") to.deflicker = g.deflicker;
    if (g.borders != "global") to.borders = g.borders;
    if (g.language != "global") to.language = g.language;
    if (g.cios != "global") to.cios = g.cios;
    if (g.server != "global") to.server = g.server;
    return true;
}

bool launch(const std::vector<std::string>& args, std::string& error) {
    HeadlessLaunch h;
    if (!parse_headless_launch(args, h, error)) return false;
    FrontendState state;
    InitializeFrontend(state);
    if (!select_game(h, state, error)) return false;
    logf("  %s\n", state.disc_status.c_str());
    logf("  %s\n", ScanPackages(state).c_str());
    if (!apply_choices(h, state, error)) return false;
    for (const LaunchPackage& p : state.model.packages) {
        if (p.enabled) logf("  pack on: %s\n", p.path.c_str());
    }
    if (const std::string problem = ModPlaceProblem(state); !problem.empty()) {
        error = problem;
        return false;
    }
    if (!CheckCodeBuilds(state, error)) return false;
    PrepareLaunchExtras(state, &h);
    RecordPlay(state.game_id);
    const LaunchSource source = SelectedSource(state);
    const std::vector<PackageChoices> selections = state.model.selections();
    const bool booted = needs_launch_pipeline(!selections.empty(), state.model.save_mode)
                            ? RunLaunch(selections, error, source, state.model.save_mode, state.game_id)
                            : RunBoot(true, error, source);
    if (reload_terminal_failure()) halt_after_terminal_reload();
    LogOpen(kLogPath, true);  // the boot closed it and remounted the card
    return booted;
}

}  // namespace

bool HeadlessArguments(std::vector<std::string>& args) {
    args.clear();
    if (__system_argv != nullptr && __system_argv->argvMagic == ARGV_MAGIC && __system_argv->argv != nullptr) {
        // Started the way a loader starts Friivolution: argv[1] is its
        // binary FRIIV_CFG (NUL bytes and all), read from the argument
        // buffer itself.
        if (__system_argv->argc >= 2 && __system_argv->argv[1] != nullptr && __system_argv->commandLine != nullptr) {
            const char* start = __system_argv->commandLine;
            const char* cfg = __system_argv->argv[1];
            const std::ptrdiff_t at = cfg - start;
            if (at > 0 && at < __system_argv->length &&
                friiv_launch_args(reinterpret_cast<const std::uint8_t*>(cfg),
                                  static_cast<std::size_t>(__system_argv->length - at), args))
                return true;
            args.clear();
        }
        for (int i = 1; i < __system_argv->argc; ++i) {
            if (__system_argv->argv[i] != nullptr) args.emplace_back(__system_argv->argv[i]);
        }
    }
    if (is_headless_launch(args)) return true;
    // A loader that cannot pass arguments (and Dolphin, which starts a DOL
    // without any) writes them to a file instead, one per line. It is
    // read once: deleted before the launch, so a failed one never repeats.
    args.clear();
    std::ifstream file(kLaunchFile);
    if (!file) return false;
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) args.push_back(line);
    }
    file.close();
    std::remove(kLaunchFile);
    return is_headless_launch(args);
}

void RunHeadless(const std::vector<std::string>& args) {
    LogOpen(kLogPath);
    std::string line;
    for (const std::string& a : args) line += " " + a;
    logf("RiftWii %s: headless launch:%s\n", RIFTWII_VERSION, line.c_str());
    std::string error;
    if (!launch(args, error)) logf("FAILED: %s\n", error.c_str());
    LogClose();
}

}  // namespace riftwii::wii
