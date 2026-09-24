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
#include "riftwii/riiconfig.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "menuios.hpp"
#include "netpacks.hpp"

namespace riftwii::wii {

namespace {
constexpr std::size_t kMaxPackages = 150;  // the option browser's limit

bool ensure_directory(const char* path, std::string& error) {
    if (mkdir(path, 0777) == 0) return true;
    if (errno == EEXIST) {
        struct stat info {};
        if (stat(path, &info) == 0 && S_ISDIR(info.st_mode)) return true;
    }
    error = std::string("cannot create choices directory ") + path + ": " + std::strerror(errno);
    return false;
}

bool ReadRiivolutionConfig(const std::string& game_id, RiivolutionConfig& out) {
    const std::string path = std::string(kPackageDir) + "/config/" + riivolution_config_name(game_id) + ".xml";
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream text;
    text << in.rdbuf();
    if (parse_riivolution_config(text.str(), out)) return true;
    logf("Riivolution config %s: not a version 2 config, ignored\n", path.c_str());
    return false;
}

// Riivolution's record of the game's choices follows RiftWii's, so the
// same mods are on in either loader. Only games with packs get one.
void WriteRiivolutionConfig(const FrontendState& state) {
    bool any = false;
    for (const LaunchPackage& p : state.model.packages) any = any || (p.valid && p.for_disc && !p.package.options.empty());
    if (!any || state.game_id.size() < 4) return;
    RiivolutionConfig existing;
    ReadRiivolutionConfig(state.game_id, existing);
    const std::string text = write_riivolution_config(merge_riivolution_config(state.model, existing));
    std::string error;
    const std::string dir = std::string(kPackageDir) + "/config";
    if (!ensure_directory(kPackageDir, error) || !ensure_directory(dir.c_str(), error)) {
        logf("Riivolution config: %s\n", error.c_str());
        return;
    }
    const std::string path = dir + "/" + riivolution_config_name(state.game_id) + ".xml";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.flush();
    if (!out) logf("Riivolution config: cannot write %s\n", path.c_str());
}
}

void InitializeFrontend(FrontendState& state) {
    state = FrontendState{};
    state.usb_catalog.device = ImageDevice::Usb;
    state.sd_catalog.device = ImageDevice::Sd;
    state.usb_catalog.status = "USB: select to scan";
    state.sd_catalog.status = "SD: select to scan";
    state.disc_status = "Disc: select to probe";
}

void IdentifyDisc(FrontendState& state, void (*progress)(const char*)) {
    // DISC is explicit. Do not enumerate image media here: autorun calls this
    // for `disc launch`, while GUI startup uses InitializeFrontend instead.
    std::string error;
    state.use_usb = false;
    state.use_sd = false;
    state.usb_index = 0;
    state.sd_index = 0;
    state.has_compiled = false;
    state.compiled = CompiledMod{};
    if (progress) progress("Probing disc...");
    if (ProbeInserted(state.game_id, state.disc_title, error, &state.game_revision, &state.game_disc_number)) {
        state.disc_status = "Disc: " + state.game_id + "  " + state.disc_title;
        state.choices_path = std::string(kChoicesDir) + "/" + state.game_id + ".txt";
    } else {
        state.game_id.clear();
        state.disc_title.clear();
        state.game_revision = 0;
        state.game_disc_number = 0;
        state.choices_path.clear();
        state.disc_status = "No disc in drive";
    }
}

// The game the DISC button boots: a fresh probe, so a swapped disc is
// picked up. False when no disc answers.
bool SelectDisc(FrontendState& state, std::string& error) {
    state.launch_warning.clear();
    state.warning_shown = false;
    state.has_compiled = false;
    state.compiled = CompiledMod{};
    state.use_usb = false;
    state.use_sd = false;
    state.usb_index = 0;
    state.sd_index = 0;
    if (!ProbeInserted(state.game_id, state.disc_title, error, &state.game_revision, &state.game_disc_number)) {
        state.game_id.clear();
        state.disc_title.clear();
        state.game_revision = 0;
        state.game_disc_number = 0;
        state.choices_path.clear();
        state.disc_status = "No disc in drive";
        return false;
    }
    state.disc_status = "Disc: " + state.game_id + "  " + state.disc_title;
    state.choices_path = std::string(kChoicesDir) + "/" + state.game_id + ".txt";
    error.clear();
    return true;
}

// The USB game the games screen hands over: index into the catalog.
bool SelectUsbGame(FrontendState& state, std::size_t index, std::string& error) {
    state.launch_warning.clear();
    state.warning_shown = false;
    state.has_compiled = false;
    state.compiled = CompiledMod{};
    if (index >= state.usb_catalog.games.size()) {
        error = "no such USB game";
        return false;
    }
    // Listed from its name only: open it now (header, d2x fragments).
    if (!check_image_game(state.usb_catalog.games[index], error)) return false;
    state.use_usb = true;
    state.use_sd = false;
    state.usb_index = index;
    state.sd_index = 0;
    const UsbGame& g = state.usb_catalog.games[index];
    state.game_id = g.id;
    state.disc_title = g.title;
    state.game_revision = g.revision;
    state.game_disc_number = g.disc_number;
    state.disc_status = "USB: " + (g.display.empty() ? g.title : g.display) + "  (" + g.id + ")";
    // The full warning names the slots and the d2x version; the status
    // line only carries the short form (the note is also in boot.log).
    if (!state.usb_catalog.cios_note.empty()) state.disc_status += "  [no cIOS: install d2x for USB boot]";
    state.choices_path = std::string(kChoicesDir) + "/" + g.id + ".txt";
    error.clear();
    return true;
}

bool SelectSdGame(FrontendState& state, std::size_t index, std::string& error) {
    state.has_compiled = false; state.compiled = CompiledMod{};
    if (index >= state.sd_catalog.games.size()) { error = "no such SD game"; return false; }
    if (!check_image_game(state.sd_catalog.games[index], error)) return false;
    state.use_usb = false; state.use_sd = true; state.usb_index = 0; state.sd_index = index;
    const ImageGame& g = state.sd_catalog.games[index];
    state.launch_warning = rvz_warning(g);
    state.warning_shown = false;
    state.game_id=g.id; state.disc_title=g.title; state.disc_status="SD: "+(g.display.empty() ? g.title : g.display)+"  ("+g.id+")";
    state.game_revision=g.revision; state.game_disc_number=g.disc_number;
    if (!state.sd_catalog.cios_note.empty()) state.disc_status += "  [no cIOS: install d2x for SD boot]";
    state.choices_path=std::string(kChoicesDir)+"/"+g.id+".txt";
    error.clear(); return true;
}

LaunchSource SelectedSource(const FrontendState& state) {
    LaunchSource source;
    if (state.use_usb && state.usb_index < state.usb_catalog.games.size()) { source.kind=LaunchSource::Kind::Usb; source.game=state.usb_catalog.games[state.usb_index]; }
    if (state.use_sd && state.sd_index < state.sd_catalog.games.size()) { source.kind=LaunchSource::Kind::Sd; source.game=state.sd_catalog.games[state.sd_index]; }
    // The game's cIOS when one is picked, else the one the menu runs
    // under, so its modules (fakemote) stay with the game.
    const int picked = effective_game_cios(state.model.game, Settings());
    source.cios_slot = picked != 0 ? picked : MenuCiosSlot();
    return source;
}

std::string ScanPackages(FrontendState& state) {
    state.model.packages.clear();
    // A reused frontend state must not carry game A's saved mode into game B
    // when B has no choices file. restore() below may replace this default.
    state.model.save_mode = "nand";
    // sd:/riivolution, then the packs cached from RiiFS servers. Hidden
    // files are never packs: macOS leaves a binary "._name.xml"
    // (AppleDouble) beside every file it copies to FAT.
    bool limited = false;
    const std::vector<PackFile> found = ListPackFiles(kMaxPackages, limited);
    DiscIdentity identity;
    const DiscIdentity* disc = nullptr;
    if (!state.game_id.empty()) {
        identity.id = state.game_id;
        identity.revision = state.game_revision;
        identity.number = state.game_disc_number;
        disc = &identity;
    }
    for (const PackFile& pack : found) {
        std::ifstream input(pack.path, std::ios::binary);
        std::stringstream text;
        if (input) text << input.rdbuf();
        state.model.add(pack.file, pack.path, input ? text.str() : std::string(), disc);
    }
    if (!state.choices_path.empty()) {
        std::ifstream saved(state.choices_path, std::ios::binary);
        if (saved) {
            std::stringstream text;
            text << saved.rdbuf();
            state.model.restore(text.str());
        } else if (!state.game_id.empty()) {
            // A game RiftWii has never saved: start from what Riivolution
            // last used for it.
            RiivolutionConfig config;
            if (ReadRiivolutionConfig(state.game_id, config)) {
                const unsigned applied = apply_riivolution_config(state.model, config);
                logf("Riivolution config for %s: %u option(s) taken\n", state.game_id.c_str(), applied);
            }
        }
    }
    std::size_t shown = 0;
    for (const LaunchPackage& p : state.model.packages) {
        if (!p.valid) logf("Package %s invalid: %s\n", p.file.c_str(), p.detail.c_str());
        if (show_package(p)) ++shown;
    }
    logf("Packages: %u XML file(s), %u shown for %s\n", static_cast<unsigned>(state.model.packages.size()),
         static_cast<unsigned>(shown), state.game_id.empty() ? "no game" : state.game_id.c_str());
    if (limited) return "First 150 packages shown; directory limit reached";
    if (state.model.packages.empty()) return "No XML packages in sd:/riivolution";
    return kScanReady;
}

bool SaveChoices(const FrontendState& state, std::string& error) {
    if (state.choices_path.empty()) {
        error = "cannot save choices: no game is selected";
        return false;
    }
    if (!ensure_directory("sd:/riftwii", error) || !ensure_directory(kChoicesDir, error)) return false;
    std::ofstream out(state.choices_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot open " + state.choices_path + " for writing";
        return false;
    }
    out << state.model.save();
    out.flush();
    if (!out) {
        error = "cannot write " + state.choices_path;
        return false;
    }
    WriteRiivolutionConfig(state);
    error.clear();
    return true;
}

}  // namespace riftwii::wii
