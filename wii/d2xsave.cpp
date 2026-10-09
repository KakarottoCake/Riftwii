// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "d2xsave.hpp"

#include <dirent.h>
#include <fat.h>
#include <gccore.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "boot.hpp"
#include "d2xsd.hpp"
#include "log.hpp"

namespace riftwii::wii {
namespace {

// d2x's FFS plugin (wiidev/d2x-cios, source/ffs-plugin): /dev/fs ioctlv
// 100 sets the NAND emulation, its mode SD (1) without the full flag
// (0x100: partial, the game's paths only); the FAT module's ioctlv 0xF0
// mounts the SD card for it (the commands USB Loader GX sends).
constexpr u32 kSetMode = 100;
constexpr u32 kModeSd = 1;
constexpr u32 kFatMountSd = 0xF0;

std::string hex8(std::uint32_t v) {
    char text[9];
    std::snprintf(text, sizeof(text), "%08x", static_cast<unsigned>(v));
    return text;
}

bool is_dir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// mkdir -p: each level of `path` (sd:/a/b/c).
bool make_dirs(const std::string& path) {
    for (std::size_t at = path.find('/', 4); ; at = path.find('/', at + 1)) {
        const std::string part = at == std::string::npos ? path : path.substr(0, at);
        if (!is_dir(part) && mkdir(part.c_str(), 0777) != 0 && !is_dir(part)) return false;
        if (at == std::string::npos) return true;
    }
}

bool write_file(const std::string& path, const std::uint8_t* data, std::size_t size) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    const bool ok = size == 0 || std::fwrite(data, 1, size, f) == size;
    return std::fclose(f) == 0 && ok;
}

// A NAND file into `out`, under the menu's IOS (its permission check
// opened once for a game's save, as for the menu font).
bool read_nand_file(const std::string& path, std::vector<std::uint8_t>& out, std::string& why) {
    static char name[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32);
    std::snprintf(name, sizeof(name), "%s", path.c_str());
    s32 fd = ISFS_Open(name, ISFS_OPEN_READ);
    static bool opened = false;
    if (fd == -102 && !opened) {
        opened = true;
        if (open_nand_permissions("Save copy")) fd = ISFS_Open(name, ISFS_OPEN_READ);
    }
    if (fd < 0) {
        why = path + " could not be opened (" + std::to_string(fd) + ")";
        return false;
    }
    static fstats stats ATTRIBUTE_ALIGN(32);
    const u32 size = ISFS_GetFileStats(fd, &stats) < 0 ? 0xFFFFFFFFu : stats.file_length;
    if (size > 64u * 1024 * 1024) {
        ISFS_Close(fd);
        why = path + " has no size that can be read";
        return false;
    }
    u8* buffer = static_cast<u8*>(memalign(32, (size + 31) & ~31u));
    if (buffer == nullptr && size != 0) {
        ISFS_Close(fd);
        why = "no room for " + path;
        return false;
    }
    const s32 got = size == 0 ? 0 : ISFS_Read(fd, buffer, size);
    ISFS_Close(fd);
    if (got != static_cast<s32>(size)) {
        std::free(buffer);
        why = path + " read " + std::to_string(got) + " of " + std::to_string(size) + " bytes";
        return false;
    }
    out.assign(buffer, buffer + size);
    std::free(buffer);
    return true;
}

// The names in a NAND folder; false when it is not one (or not there).
bool list_nand(const std::string& path, std::vector<std::string>& names) {
    static char name[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32);
    std::snprintf(name, sizeof(name), "%s", path.c_str());
    u32 count = 0;
    if (ISFS_ReadDir(name, nullptr, &count) < 0) return false;
    names.clear();
    if (count == 0) return true;
    char* list = static_cast<char*>(memalign(32, (count * 13 + 31) & ~31u));
    if (list == nullptr) return false;
    std::memset(list, 0, count * 13);
    if (ISFS_ReadDir(name, list, &count) < 0) {
        std::free(list);
        return false;
    }
    for (u32 i = 0, at = 0; i < count; ++i) {
        const std::string entry(list + at);
        at += static_cast<u32>(entry.size()) + 1;
        if (!entry.empty()) names.push_back(entry);
    }
    std::free(list);
    return true;
}

// The Wii's save folder into the card's, its subfolders too.
bool copy_nand_tree(const std::string& nand, const std::string& sd, unsigned& files, std::string& why) {
    std::vector<std::string> names;
    if (!list_nand(nand, names)) return true;  // no save on the Wii: a new one starts
    if (!make_dirs(sd)) {
        why = "cannot create " + sd;
        return false;
    }
    for (const std::string& n : names) {
        const std::string from = nand + "/" + n, to = sd + "/" + n;
        std::vector<std::string> inner;
        if (list_nand(from, inner)) {
            if (!copy_nand_tree(from, to, files, why)) return false;
            continue;
        }
        std::vector<std::uint8_t> bytes;
        if (!read_nand_file(from, bytes, why)) return false;
        if (!write_file(to, bytes.data(), bytes.size())) {
            why = "cannot write " + to;
            return false;
        }
        ++files;
    }
    return true;
}

// Removes `path` and what is in it.
void remove_tree(const std::string& path) {
    if (DIR* d = opendir(path.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            const std::string child = path + "/" + n;
            if (is_dir(child)) remove_tree(child);
            else unlink(child.c_str());
        }
        closedir(d);
    }
    rmdir(path.c_str());
}

}  // namespace

std::string own_save_data_dir(const std::string& dir, std::uint64_t title_id) {
    return dir + "/title/" + hex8(static_cast<std::uint32_t>(title_id >> 32)) + "/" +
           hex8(static_cast<std::uint32_t>(title_id)) + "/data";
}

bool move_flat_saves(const std::string& dir, const std::string& data_dir, std::string& error) {
    std::vector<std::string> flat;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == ".." || n == "riftwii.cln") continue;
            if (!is_dir(dir + "/" + n)) flat.push_back(n);
        }
        closedir(d);
    }
    if (flat.empty()) return true;
    if (!make_dirs(data_dir)) {
        error = "cannot create the save folder " + data_dir;
        return false;
    }
    for (const std::string& n : flat) {
        const std::string from = dir + "/" + n, to = data_dir + "/" + n;
        struct stat st;
        if (stat(to.c_str(), &st) == 0) continue;  // moved before, the old copy left: the newer stays
        if (std::rename(from.c_str(), to.c_str()) != 0) {
            error = "cannot move " + from + " into " + data_dir;
            return false;
        }
    }
    if (!flat.empty())
        logf("Saves: %u file(s) moved into %s (the save folder's new layout)\n", static_cast<unsigned>(flat.size()),
             data_dir.c_str());
    return true;
}

bool prepare_d2x_saves(const ImageGame& game, const std::string& dir, bool clone, std::string& error) {
    if (game.title_id == 0) {
        error = "the game's title ID is not known";
        return false;
    }
    const std::string data = own_save_data_dir(dir, game.title_id);
    const bool had_save = is_dir(data);
    // The emulated NAND's folders USB Loader GX makes before it turns d2x's
    // emulation on (d2x makes the rest).
    for (const char* sub : {"/import", "/meta", "/shared1", "/shared2", "/sys", "/ticket", "/tmp"}) {
        if (!make_dirs(dir + sub)) {
            error = "cannot create " + dir + sub;
            return false;
        }
    }
    if (!move_flat_saves(dir, data, error)) return false;
    const std::string content = dir + "/title/" + hex8(static_cast<std::uint32_t>(game.title_id >> 32)) + "/" +
                                hex8(static_cast<std::uint32_t>(game.title_id)) + "/content";
    if (!make_dirs(content)) {
        error = "cannot create " + content;
        return false;
    }
    struct stat st;
    const std::string tmd = content + "/title.tmd";
    if (!game.tmd.empty() && stat(tmd.c_str(), &st) != 0 && !write_file(tmd, game.tmd.data(), game.tmd.size())) {
        error = "cannot write " + tmd;
        return false;
    }
    // "SD, from Wii save": the Wii's save, once, into a folder that had
    // none. A copy that fails leaves no half save behind.
    if (clone && !had_save) {
        DIR* d = opendir(data.c_str());
        bool empty = true;
        if (d) {
            while (dirent* e = readdir(d))
                if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) empty = false;
            closedir(d);
        }
        if (empty) {
            if (ISFS_Initialize() < 0) {
                error = "the Wii's save could not be read (ISFS)";
                return false;
            }
            const std::string nand = "/title/" + hex8(static_cast<std::uint32_t>(game.title_id >> 32)) + "/" +
                                     hex8(static_cast<std::uint32_t>(game.title_id)) + "/data";
            unsigned files = 0;
            std::string why;
            if (!copy_nand_tree(nand, data, files, why)) {
                remove_tree(data);
                make_dirs(data);
                error = "the Wii's save could not be copied: " + why;
                return false;
            }
            logf("Saves: the Wii's save copied in (%u file(s) from %s)\n", files, nand.c_str());
        }
    }
    // Made here, by libfat: d2x would otherwise make it while libfat still
    // has the card mounted (two FAT drivers allocating on one card).
    if (!make_dirs(data)) {
        error = "cannot create the save folder " + data;
        return false;
    }
    return true;
}

bool enable_d2x_saves(const std::string& dir, std::string& error) {
    if (dir.compare(0, 4, "sd:/") != 0) {
        error = "not a folder of the SD card";
        return false;
    }
    static char path[256] ATTRIBUTE_ALIGN(32);
    std::snprintf(path, sizeof(path), "%s", dir.c_str() + 3);
    static u32 mode ATTRIBUTE_ALIGN(32) = kModeSd;
    // libfat's writes reach the card before d2x reads its FAT.
    LogClose();
    fatUnmount("sd:");
    s32 mounted = -1, set = -1;
    const s32 fat = IOS_Open("fat", 0);
    if (fat >= 0) {
        mounted = IOS_Ioctlv(fat, kFatMountSd, 0, 0, nullptr);
        IOS_Close(fat);
    }
    const s32 fs = IOS_Open("/dev/fs", 0);
    if (fs >= 0) {
        static ioctlv vectors[2] ATTRIBUTE_ALIGN(32);
        mode = kModeSd;
        vectors[0].data = &mode;
        vectors[0].len = sizeof(mode);
        vectors[1].data = path;
        vectors[1].len = sizeof(path);
        set = IOS_Ioctlv(fs, kSetMode, 2, 0, vectors);
        IOS_Close(fs);
    }
    // And libfat reads the card afresh after d2x made its folders.
    const bool remounted = fatMountSimple("sd", sd_interface());
    LogReopen();
    logf("Saves: d2x NAND emulation from %s: FAT module %d, mount %d, set %d%s\n", dir.c_str(), fat, mounted, set,
         remounted ? "" : "; the SD card did not mount again");
    if (fat < 0 || set < 0) {
        error = "d2x refused the NAND emulation (FAT module " + std::to_string(fat) + ", set " + std::to_string(set) + ")";
        return false;
    }
    if (!remounted) {
        error = "the SD card did not mount again after d2x took it";
        return false;
    }
    return true;
}

}  // namespace riftwii::wii
