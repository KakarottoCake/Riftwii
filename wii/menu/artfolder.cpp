// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "artfolder.hpp"

#include <dirent.h>
#include <ogc/lwp.h>

#include <atomic>
#include <cerrno>
#include <map>

#include "log.hpp"
#include "riftwii/foldernames.hpp"

namespace riftwii::wii {
namespace {

// The look's thread and how many looks it has open. Other threads read
// g_look_thread (atomic) only to find it is not their own; g_look_depth is
// touched by the look's own thread alone, after that check.
std::atomic<lwp_t> g_look_thread{LWP_THREAD_NULL};
int g_look_depth = 0;

struct Listing {
    bool usable = false;
    FolderNames names;
};
// By folder ("sd:/riftwii/covers"), for the open look. Its thread only.
std::map<std::string, Listing> g_listings;

// The folder's names, once. False for a folder that is not there, cannot
// be read or holds more files than are kept: the card is asked instead.
bool ListFolder(const std::string& folder, FolderNames& names) {
    DIR* dir = opendir(folder.c_str());
    if (!dir) return false;
    bool whole = true;
    // readdir returns null at the end and on a read error alike; only errno
    // tells them apart. libfat leaves ENOENT (or nothing) at a normal end.
    errno = 0;
    while (const dirent* entry = readdir(dir)) {
        // Stops at FolderNames::kMaxNames, so a folder whose chain on the
        // card loops back on itself ends here too.
        if (!names.add(entry->d_name)) {
            whole = false;
            break;
        }
        errno = 0;
    }
    const int read_error = errno;
    closedir(dir);
    if (whole && read_error != 0 && read_error != ENOENT) {
        whole = false;
        logf("Pictures: %s could not be read to the end (error %d); each picture is looked for on its own\n",
             folder.c_str(), read_error);
    } else if (!whole) {
        logf("Pictures: %s holds too many files to list at once; each picture is looked for on its own\n",
             folder.c_str());
        names.clear();
        return false;
    }
    names.finish();
    return true;
}

// The listing of `path`'s folder when this thread has a look open and the
// folder could be listed; `name` is then the file's own name.
const FolderNames* ListingOf(const std::string& path, std::string& name) {
    if (LWP_GetSelf() != g_look_thread.load() || g_look_depth == 0) return nullptr;
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash + 1 >= path.size()) return nullptr;
    const std::string folder = path.substr(0, slash);
    auto found = g_listings.find(folder);
    if (found == g_listings.end()) {
        found = g_listings.emplace(folder, Listing{}).first;
        found->second.usable = ListFolder(folder, found->second.names);
    }
    if (!found->second.usable) return nullptr;
    name = path.substr(slash + 1);
    return &found->second.names;
}

}  // namespace

ArtFolderLook::ArtFolderLook() {
    // A look opened on a second thread while one is open leaves that
    // thread asking the card, which is always right.
    lwp_t expected = LWP_THREAD_NULL;
    g_look_thread.compare_exchange_strong(expected, LWP_GetSelf());
    if (LWP_GetSelf() == g_look_thread.load()) ++g_look_depth;
}

ArtFolderLook::~ArtFolderLook() {
    if (LWP_GetSelf() != g_look_thread.load() || g_look_depth == 0) return;
    if (--g_look_depth == 0) {
        g_listings.clear();
        g_look_thread.store(LWP_THREAD_NULL);
    }
}

bool ArtFileThere(const std::string& path) {
    std::string name;
    if (const FolderNames* names = ListingOf(path, name)) return names->has(name);
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

bool ArtFileStat(const std::string& path, struct stat& info) {
    std::string name;
    const FolderNames* names = ListingOf(path, name);
    if (names && !names->has(name)) {
        errno = ENOENT;  // what stat() would have said
        return false;
    }
    return stat(path.c_str(), &info) == 0;
}

}  // namespace riftwii::wii
