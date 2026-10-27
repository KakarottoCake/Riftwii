// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2011 Dimok
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "d2xsave.hpp"

#include <dirent.h>
#include <gccore.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "boot.hpp"
#include "log.hpp"
#include "nanduse.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/nandlist.hpp"

namespace riftwii::wii {
namespace {

std::string hex8(std::uint32_t v) {
    char text[9] = {};
    std::snprintf(text, sizeof(text), "%08x", static_cast<unsigned>(v));
    return text;
}

bool is_dir(const std::string& path) {
    struct stat st = {};
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
// opened for a game's save, as for the menu font, and closed once the save
// is copied; `refused` stops asking again after it could not be opened).
bool read_nand_file(const std::string& path, std::vector<std::uint8_t>& out, bool& refused, std::string& why) {
    static char name[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32) = {};
    std::snprintf(name, sizeof(name), "%s", path.c_str());
    s32 fd = ISFS_Open(name, ISFS_OPEN_READ);
    if (fd == -102 && !refused) {
        if (open_nand_permissions("Save copy")) fd = ISFS_Open(name, ISFS_OPEN_READ);
        else refused = true;
    }
    if (fd < 0) {
        why = path + " could not be opened (" + std::to_string(fd) + ")";
        return false;
    }
    static fstats stats ATTRIBUTE_ALIGN(32) = {};
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
    if (!try_assign_range(out, buffer, buffer + size)) {
        std::free(buffer);
        why = "no room for " + path;
        return false;
    }
    std::free(buffer);
    return true;
}

std::int32_t nand_read_dir(void* ctx, char* list, std::uint32_t* count) {
    char* name = static_cast<char*>(ctx);
    u32 wii_count = 0;
    if (count != nullptr) wii_count = static_cast<u32>(*count);
    const s32 result = ISFS_ReadDir(name, list, &wii_count);
    if (count != nullptr) *count = static_cast<std::uint32_t>(wii_count);
    return static_cast<std::int32_t>(result);
}

void* nand_alloc(std::size_t bytes) {
    return memalign(32, bytes);
}

void nand_release(void* block) {
    std::free(block);
}

// The names in a NAND folder; NotThere when it is not one (or not there).
NandList list_nand(const std::string& path, std::vector<std::string>& names, std::uint32_t& count) {
    static char name[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32) = {};
    std::snprintf(name, sizeof(name), "%s", path.c_str());
    count = 0;
    return list_nand_with(nand_read_dir, name, nand_alloc, nand_release, names, count);
}

// The Wii's save folder into the card's, its subfolders too.
bool copy_nand_tree(const std::string& nand, const std::string& sd, unsigned& files, bool& refused, std::string& why) {
    std::vector<std::string> names;
    std::uint32_t count = 0;
    const NandList listed = list_nand(nand, names, count);
    if (listed == NandList::NotThere) return true;  // no save on the Wii: a new one starts
    if (listed == NandList::NoRoom) {
        why = "no room to list " + nand;
        return false;
    }
    if (listed == NandList::Damaged) {
        logf("Saves: %s lists a damaged folder (count %u)\n", nand.c_str(), static_cast<unsigned>(count));
        why = nand + " lists a damaged folder";
        return false;
    }
    if (!make_dirs(sd)) {
        why = "cannot create " + sd;
        return false;
    }
    for (const std::string& n : names) {
        const std::string from = nand + "/" + n, to = sd + "/" + n;
        std::vector<std::string> inner;
        std::uint32_t inner_count = 0;
        const NandList inner_listed = list_nand(from, inner, inner_count);
        if (inner_listed == NandList::Ok) {
            if (!copy_nand_tree(from, to, files, refused, why)) return false;
            continue;
        }
        if (inner_listed == NandList::NoRoom) {
            why = "no room to list " + from;
            return false;
        }
        if (inner_listed == NandList::Damaged) {
            logf("Saves: %s lists a damaged folder (count %u)\n", from.c_str(), static_cast<unsigned>(inner_count));
            why = from + " lists a damaged folder";
            return false;
        }
        std::vector<std::uint8_t> bytes;
        if (!read_nand_file(from, bytes, refused, why)) return false;
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
    // Only RiftWii's own save folders ever held flat saves. The shared
    // emulated NAND (riftwii/sharednand.hpp) is another loader's: a file
    // at its top is not a save of this game and stays where it is.
    if (dir.compare(0, 18, "sd:/riftwii/saves/") != 0) return true;
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
        struct stat st = {};
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

bool save_folder_empty(const std::string& data_dir) {
    DIR* d = opendir(data_dir.c_str());
    if (d == nullptr) return true;
    bool empty = true;
    while (dirent* e = readdir(d)) {
        const std::string n = e->d_name;
        if (n != "." && n != ".." && n != "riftwii.cln") empty = false;
    }
    closedir(d);
    return empty;
}

bool prepare_d2x_saves(const ImageGame& game, const std::string& dir, bool clone, bool clone_into_empty,
                       std::string& error) {
    if (game.title_id == 0) {
        error = "the game's title ID is not known";
        return false;
    }
    const std::string data = own_save_data_dir(dir, game.title_id);
    const bool had_save = is_dir(data) && !(clone_into_empty && save_folder_empty(data));
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
    struct stat st = {};
    const std::string tmd = content + "/title.tmd";
    if (!game.tmd.empty() && stat(tmd.c_str(), &st) != 0 && !write_file(tmd, game.tmd.data(), game.tmd.size())) {
        error = "cannot write " + tmd;
        return false;
    }
    // "SD, from Wii save" and "SD, emulated NAND": the Wii's save, once,
    // into a folder that had none. A copy that fails leaves no half save
    // behind.
    if (clone && !had_save) {
        if (save_folder_empty(data)) {
            if (NandOpen() < 0) {
                error = "the Wii's save could not be read (ISFS)";
                return false;
            }
            const std::string nand = "/title/" + hex8(static_cast<std::uint32_t>(game.title_id >> 32)) + "/" +
                                     hex8(static_cast<std::uint32_t>(game.title_id)) + "/data";
            unsigned files = 0;
            std::string why;
            bool refused = false;
            const bool copied = copy_nand_tree(nand, data, files, refused, why);
            close_nand_permissions("Save copy");
            if (!copied) {
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

}  // namespace riftwii::wii
