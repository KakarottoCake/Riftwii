// SPDX-FileCopyrightText: 2026 RiftWii contributors
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
#include "memlimits.hpp"
#include "nanduse.hpp"
#include "nandpermissions.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/ctgprsave.hpp"
#include "riftwii/nandlist.hpp"
#include "riftwii/sharednand.hpp"

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
    // From the first folder after the drive ("sd:/", "rwnand:/").
    const std::size_t root = path.find(":/");
    if (root == std::string::npos) return false;
    for (std::size_t at = path.find('/', root + 2); ; at = path.find('/', at + 1)) {
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
// is copied, with the check opened before the first request).
bool read_nand_file(const std::string& path, std::vector<std::uint8_t>& out, bool permissions_open, std::string& why) {
    static char name[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32) = {};
    std::snprintf(name, sizeof(name), "%s", path.c_str());
    s32 fd = ISFS_Open(name, ISFS_OPEN_READ);
    if (fd == -102 && permissions_open) {
        logf("Save copy: %s was still refused (-102) with the NAND permission check open.\n", name);
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

// The same, but only ISFS "not found" is NotThere: any other refusal is
// Failed, with its code in `error`.
NandList list_nand_strict(const std::string& path, std::vector<std::string>& names, std::uint32_t& count,
                          std::int32_t& error) {
    static char name[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32) = {};
    std::snprintf(name, sizeof(name), "%s", path.c_str());
    count = 0;
    return list_nand_strict_with(nand_read_dir, name, nand_alloc, nand_release, names, count, error);
}

// The Wii's save folder into the card's, its subfolders too.
bool copy_nand_tree(const std::string& nand, const std::string& sd, unsigned& files, bool permissions_open,
                    std::string& why) {
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
            if (!copy_nand_tree(from, to, files, permissions_open, why)) return false;
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
        if (!read_nand_file(from, bytes, permissions_open, why)) return false;
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

namespace {

bool prepare_folder(const ImageGame& game, const std::string& dir, bool clone, bool clone_into_empty,
                    bool* whole_nand, std::string& error) {
    // The whole NAND (the Wii's settings and Miis too) only from a folder
    // that holds one: without the Wii's settings file a game started on
    // it may stop at its first screen.
    if (whole_nand != nullptr && *whole_nand) {
        struct stat settings = {};
        if (stat((dir + "/shared2/sys/SYSCONF").c_str(), &settings) != 0) {
            *whole_nand = false;
            logf("Saves: the emulated NAND has no shared2/sys/SYSCONF (the Wii's settings), so d2x keeps only the "
                 "game's save there, not the whole NAND\n");
        }
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
            NandPermissionSession permissions("Save copy");
            const bool copied = copy_nand_tree(nand, data, files, permissions.IsOpen(), why);
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

}  // namespace

bool prepare_d2x_saves(const ImageGame& game, const std::string& dir, bool clone, bool clone_into_empty,
                       std::string& error, bool* whole_nand) {
    if (game.title_id == 0) {
        error = "the game's title ID is not known";
        return false;
    }
    if (!shared_nand_on_usb(dir)) return prepare_folder(game, dir, clone, clone_into_empty, whole_nand, error);
    // On the USB drive (#487): its exact partition, mounted for writing
    // only while the folder is prepared.
    std::string writable;
    if (!mount_usb_nand_folder(dir, writable, error)) return false;
    const bool ok = prepare_folder(game, writable, clone, clone_into_empty, whole_nand, error);
    unmount_usb_nand_folder();
    // Errors name the folder as the player knows it.
    const std::size_t at = error.find(writable);
    if (at != std::string::npos) error.replace(at, writable.size(), dir);
    return ok;
}

namespace {

// One NAND file into `to` (a temporary name on the card), through `buffer`
// in pieces: the 2.8 MB save never sits in memory whole.
bool copy_nand_file_chunked(const std::string& from, const std::string& to, std::uint8_t* buffer,
                            std::size_t buffer_bytes, std::string& why) {
    static char name[ISFS_MAXPATH] ATTRIBUTE_ALIGN(32) = {};
    std::snprintf(name, sizeof(name), "%s", from.c_str());
    const s32 fd = ISFS_Open(name, ISFS_OPEN_READ);
    if (fd < 0) {
        why = from + " could not be opened (" + std::to_string(fd) + ")";
        return false;
    }
    static fstats stats ATTRIBUTE_ALIGN(32) = {};
    if (ISFS_GetFileStats(fd, &stats) < 0 || stats.file_length > 64u * 1024 * 1024) {
        ISFS_Close(fd);
        why = from + " has no size that can be read";
        return false;
    }
    FILE* out = std::fopen(to.c_str(), "wb");
    if (out == nullptr) {
        ISFS_Close(fd);
        why = "cannot write " + to;
        return false;
    }
    bool ok = true;
    u32 left = stats.file_length;
    while (left != 0 && ok) {
        const u32 want = left < buffer_bytes ? left : static_cast<u32>(buffer_bytes);
        const s32 got = ISFS_Read(fd, buffer, want);
        if (got != static_cast<s32>(want)) {
            why = from + " read " + std::to_string(got) + " of " + std::to_string(want) + " bytes";
            ok = false;
        } else if (std::fwrite(buffer, 1, want, out) != want) {
            why = "cannot write " + to + " (the card may be full)";
            ok = false;
        }
        left -= want;
    }
    ISFS_Close(fd);
    if (std::fclose(out) != 0 && ok) {
        why = "cannot write " + to + " (the card may be full)";
        ok = false;
    }
    if (!ok) std::remove(to.c_str());  // our own temporary file only
    return ok;
}

std::vector<std::string> card_names(const std::string& folder) {
    std::vector<std::string> names;
    if (DIR* d = opendir(folder.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n != "." && n != "..") names.push_back(n);
        }
        closedir(d);
    }
    return names;
}

}  // namespace

bool SeedCtgprSdSave(const std::string& game_id, std::string& why) {
    const char region = ctgpr_save_region(game_id);
    if (region == 0) return true;  // not a game CTGP-R starts
    const std::string folder = ctgpr_sd_save_folder(region);
    const std::string nand = ctgpr_nand_data_dir(region);
    const bool existed = is_dir(folder);
    std::vector<std::string> on_card = card_names(folder);
    if (ctgpr_seed_skipped(on_card)) {
        logf("CTGP-R save: %s/rksys.dat is on the card; the folder is left as it is\n", folder.c_str());
        return true;
    }
    if (NandOpen() < 0) {
        why = "the Wii's save could not be read (ISFS)";
        return false;
    }
    // /dev/fs is closed on every way out, so no handle goes on into the probe and the IOS reload.
    struct NandUser {
        ~NandUser() { NandClose(); }
    } nand_user;
    NandPermissionSession permissions("CTGP-R save copy");
    std::vector<std::string> nand_names;
    std::uint32_t count = 0;
    std::int32_t code = 0;
    const NandList listed = list_nand_strict(nand, nand_names, count, code);
    // Only "not found" is a Wii with no save. Any other answer (a permission
    // refusal, an ISFS error) says nothing about the save, and an empty folder
    // made now would let the game start a new save that hides the real one:
    // the card is left untouched.
    if (listed == NandList::Failed) {
        why = "the Wii's save folder " + nand + " could not be listed (ISFS " + std::to_string(code) + ")";
        return false;
    }
    if (listed == NandList::NoRoom || listed == NandList::Damaged) {
        why = listed == NandList::NoRoom ? "no room to list " + nand : nand + " lists a damaged folder";
        return false;
    }
    // No save on the Wii (a new player): the folder alone, as Riivolution
    // makes it; the game starts a save in it.
    if (listed == NandList::NotThere) nand_names.clear();
    if (!existed && !make_dirs(folder)) {
        why = "cannot create " + folder;
        return false;
    }
    logf("CTGP-R save: %s %s; copying the Wii's save from %s\n", folder.c_str(),
         existed ? "has no rksys.dat" : "created", nand.c_str());
    std::vector<std::string> plan;
    for (const std::string& n : ctgpr_seed_plan(nand_names, on_card)) {
        // A folder inside the data folder is not a save file: left alone.
        std::vector<std::string> inner;
        std::uint32_t inner_count = 0;
        if (list_nand(nand + "/" + n, inner, inner_count) == NandList::Ok) {
            logf("CTGP-R save: %s/%s is a folder, not copied\n", nand.c_str(), n.c_str());
            continue;
        }
        plan.push_back(n);
    }
    // A leftover temporary of an earlier copy that stopped, for a name about
    // to be written (only our own ending).
    for (const std::string& n : plan) {
        const std::string temp = folder + "/" + ctgpr_seed_temp_name(n);
        struct stat st = {};
        if (stat(temp.c_str(), &st) == 0 && S_ISREG(st.st_mode) && std::remove(temp.c_str()) == 0)
            logf("CTGP-R save: removed the leftover %s of an earlier copy\n", temp.c_str());
    }
    if (listed == NandList::NotThere)
        logf("CTGP-R save: the Wii has no save for %s (a new player): the folder is left empty, the game makes the "
             "save\n", game_id.c_str());
    // One MEM2 block, freed straight after (TryMem2Alloc may grow `bytes`; 256 KiB of it is
    // used, in pieces); the heap's 32 KiB only if MEM2 has none.
    std::size_t bytes = 256u * 1024;
    std::uint8_t* buffer = static_cast<std::uint8_t*>(plan.empty() ? nullptr : mem::TryMem2Alloc(bytes));
    std::size_t usable = bytes < 256u * 1024 ? bytes : 256u * 1024;
    if (buffer == nullptr && !plan.empty()) {
        usable = 32u * 1024;
        buffer = static_cast<std::uint8_t*>(memalign(32, usable));
        if (buffer == nullptr) {
            if (!existed) rmdir(folder.c_str());
            why = "no room to copy the save";
            return false;
        }
        logf("CTGP-R save: no MEM2 block for the copy; using %u KiB of the heap\n", static_cast<unsigned>(usable / 1024));
    }
    std::vector<std::string> made;  // the final names this run created
    bool ok = true;
    for (const std::string& n : plan) {
        const std::string to = folder + "/" + n, temp = folder + "/" + ctgpr_seed_temp_name(n);
        struct stat st = {};
        if (stat(to.c_str(), &st) == 0) continue;  // appeared meanwhile: never overwritten
        if (!copy_nand_file_chunked(nand + "/" + n, temp, buffer, usable, why)) {
            ok = false;
            break;
        }
        if (std::rename(temp.c_str(), to.c_str()) != 0) {
            std::remove(temp.c_str());
            why = "cannot rename " + temp + " to " + to;
            ok = false;
            break;
        }
        made.push_back(to);
        logf("CTGP-R save: copied %s\n", n.c_str());
    }
    std::free(buffer);
    if (!ok) {
        // Only what this run made goes, so that no half save is taken for the player's.
        for (const std::string& f : made) std::remove(f.c_str());
        if (!existed) rmdir(folder.c_str());
        return false;
    }
    logf("CTGP-R save: %u file(s) copied into %s\n", static_cast<unsigned>(made.size()), folder.c_str());
    return true;
}

}  // namespace riftwii::wii
