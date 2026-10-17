// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "modtiles.hpp"

#include <sys/stat.h>

#include <cstdio>

#include "gameextras.hpp"
#include "i18n.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "modpicture.hpp"
#include "textfile.hpp"

namespace riftwii::wii {
namespace {

ProfileList g_profiles;
bool g_profilesRead = false;

// Written whole beside it, then put in its place, as settings.txt.
bool SaveProfiles(std::string& error) {
    mkdir("sd:/riftwii", 0777);
    const std::string temp = std::string(kProfilesPath) + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    bool ok = f != nullptr;
    if (f) {
        const std::string text = g_profiles.serialize();
        ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
        ok = std::fclose(f) == 0 && ok;
    }
    if (ok) {
        std::remove(kProfilesPath);
        ok = std::rename(temp.c_str(), kProfilesPath) == 0;
    }
    if (!ok) {
        std::remove(temp.c_str());
        error = tr("Could not save the tiles to the SD card.");
        logf("Home tiles: cannot write %s\n", kProfilesPath);
    }
    return ok;
}

// A change that could not be saved: the list is read from the card again,
// so what Home shows stays what the card holds.
void ForgetUnsaved() {
    g_profiles = ProfileList();
    g_profilesRead = false;
}

// The picture of the first mod turned on that has one.
std::string FirstModPicture(const FrontendState& state) {
    for (const LaunchPackage& p : state.model.packages) {
        if (!p.valid || !p.enabled) continue;
        const std::string path = ModPicturePath(p);
        if (!path.empty()) return path;
    }
    return "";
}

}  // namespace

ProfileList& Profiles() {
    if (!g_profilesRead) {
        g_profilesRead = true;
        std::string text;
        if (ReadTextFile(kProfilesPath, text, kMaxProfilesFile)) g_profiles.parse(text);
        if (!g_profiles.all().empty())
            logf("Home tiles: %u read from %s\n", static_cast<unsigned>(g_profiles.all().size()), kProfilesPath);
    }
    return g_profiles;
}

bool CreateProfile(const FrontendState& state, const std::string& name, std::string& key, std::string& error) {
    switch (Profiles().add(state.game_id, name, FirstModPicture(state), key)) {
        case ProfileList::AddResult::Added: break;
        case ProfileList::AddResult::NoName: error = tr("The tile needs a name."); return false;
        case ProfileList::AddResult::GameFull:
            error = tr("This game has {1} tiles already, as many as it can have.", {std::to_string(kMaxProfilesPerGame)});
            return false;
        case ProfileList::AddResult::ListFull:
            error = tr("Home has {1} extra tiles already, as many as it can have.", {std::to_string(kMaxProfiles)});
            return false;
        case ProfileList::AddResult::BadGame: error = tr("This game has no ID to keep its tile under."); return false;
    }
    // Its choices first: a tile in the list always has its own file.
    const std::string path = std::string(kChoicesDir) + "/" + key + ".txt";
    std::string why;
    if (!SaveChoicesTo(state.model, path, why)) {
        logf("Home tiles: %s\n", why.c_str());
        Profiles().remove(key);
        error = tr("Could not save the tile's mods to the SD card.");
        return false;
    }
    if (!SaveProfiles(error)) {
        Profiles().remove(key);
        std::remove(path.c_str());
        return false;
    }
    const Profile* made = Profiles().find(key);
    logf("Home tiles: %s made from %s's page, named \"%s\", picture %s\n", key.c_str(), state.game_id.c_str(),
         made->name.c_str(), made->art.empty() ? "none" : made->art.c_str());
    ForgetProfilePictures();
    return true;
}

bool RenameProfile(FrontendState& state, const std::string& name, std::string& error) {
    if (!Profiles().rename(state.profile_key, name)) {
        error = tr("The tile needs a name.");
        return false;
    }
    if (!SaveProfiles(error)) {
        ForgetUnsaved();
        return false;
    }
    state.profile_name = Profiles().find(state.profile_key)->name;
    logf("Home tiles: %s renamed \"%s\"\n", state.profile_key.c_str(), state.profile_name.c_str());
    return true;
}

bool RemoveProfile(FrontendState& state, std::string& error) {
    const std::string key = state.profile_key;
    if (!Profiles().remove(key)) {
        error = tr("This tile is not on the list any more.");
        return false;
    }
    if (!SaveProfiles(error)) {
        ForgetUnsaved();
        return false;
    }
    std::remove(state.choices_path.c_str());
    // Its picture too, or a new tile of the same name (the same key) would
    // show it; the remove popup says so, as the player put it there.
    std::remove((std::string(kProfilePictureDir) + "/" + key + ".png").c_str());
    if (Settings().favorites.erase(key) != 0) SaveSettings();
    ForgetPlays(key);
    ForgetProfilePictures();
    logf("Home tiles: %s removed\n", key.c_str());
    state.profile_key.clear();
    state.profile_name.clear();
    state.choices_path = std::string(kChoicesDir) + "/" + state.game_id + ".txt";
    return true;
}

}  // namespace riftwii::wii
