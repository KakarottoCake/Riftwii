// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "emunandmanage.hpp"

#include <gccore.h>
#include <malloc.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "log.hpp"
#include "nandpermissions.hpp"
#include "nanduse.hpp"
#include "sharednandfind.hpp"
#include "usbcatalog.hpp"
#include "usbnand.hpp"
#include "riftwii/bnr.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/emunand.hpp"
#include "riftwii/nandchannel.hpp"
#include "riftwii/nandlist.hpp"
#include "riftwii/sharednand.hpp"
#include "riftwii/wadfile.hpp"

namespace riftwii::wii {
namespace {

constexpr std::size_t kChunk = 32 * 1024;
constexpr unsigned kMaxTitles = 1024;
constexpr unsigned kMaxFolders = 64;

struct NandSession {
    bool open = NandOpen() >= 0;
    NandPermissionSession permissions{"Emulated NAND manager"};
    ~NandSession() { if (open) NandClose(); }
};

struct CardRoot {
    std::string path;
    bool mounted = false;

    bool Open(const std::string& configured, std::string& error) {
        if (!emunand_root(configured, path)) {
            error = "the emulated NAND folder is not safe to use";
            return false;
        }
        if (shared_nand_on_usb(path)) {
            std::string writable;
            if (!mount_usb_nand_folder(path, writable, error)) return false;
            mounted = true;
            path = writable;
        }
        return true;
    }
    ~CardRoot() { if (mounted) unmount_usb_nand_folder(); }
};

// Separate buffers are needed for the two arguments of ISFS_Rename.
struct NandPath {
    char text[64] ATTRIBUTE_ALIGN(32) = {};
    explicit NandPath(const std::string& path) {
        if (path.size() < sizeof(text)) std::memcpy(text, path.c_str(), path.size() + 1);
    }
};

struct Location {
    bool real = false;
    std::string path;
};

enum class Kind { Missing, File, Directory, Failed };

struct Attributes {
    u32 owner = 0;
    u16 group = 0;
    u8 flags = 0;
    u8 owner_perm = 3;
    u8 group_perm = 3;
    u8 other_perm = 0;
};

bool get_attributes(const std::string& path, Attributes& attributes) {
    const NandPath name(path);
    return name.text[0] && ISFS_GetAttr(name.text, &attributes.owner, &attributes.group, &attributes.flags,
        &attributes.owner_perm, &attributes.group_perm, &attributes.other_perm) >= 0;
}

bool set_attributes(const std::string& path, const Attributes& attributes) {
    const NandPath name(path);
    return name.text[0] && ISFS_SetAttr(name.text, attributes.owner, attributes.group, attributes.flags,
        attributes.owner_perm, attributes.group_perm, attributes.other_perm) >= 0;
}

Kind inspect(const Location& location, std::uint64_t& size) {
    size = 0;
    if (!location.real) {
        struct stat info = {};
        if (lstat(location.path.c_str(), &info) != 0) return errno == ENOENT ? Kind::Missing : Kind::Failed;
        if (S_ISDIR(info.st_mode)) return Kind::Directory;
        if (!S_ISREG(info.st_mode) || info.st_size < 0) return Kind::Failed;
        size = static_cast<std::uint64_t>(info.st_size);
        return Kind::File;
    }
    const NandPath path(location.path);
    if (!path.text[0]) return Kind::Failed;
    u32 count = 0;
    const s32 listed = ISFS_ReadDir(path.text, nullptr, &count);
    if (listed >= 0) return Kind::Directory;
    const s32 file = ISFS_Open(path.text, ISFS_OPEN_READ);
    if (file < 0) return file == kNandNotFound && listed == kNandNotFound ? Kind::Missing : Kind::Failed;
    fstats stats ATTRIBUTE_ALIGN(32) = {};
    const bool ok = ISFS_GetFileStats(file, &stats) >= 0;
    const bool closed = ISFS_Close(file) >= 0;
    size = stats.file_length;
    return ok && closed ? Kind::File : Kind::Failed;
}

std::int32_t read_directory(void* context, char* list, std::uint32_t* count) {
    u32 found = *count;
    const s32 result = ISFS_ReadDir(static_cast<NandPath*>(context)->text, list, &found);
    *count = found;
    return result;
}

void* allocate_list(std::size_t size) { return memalign(32, size); }

bool directory_names(const Location& location, std::vector<std::string>& names) {
    names.clear();
    if (location.real) {
        NandPath path(location.path);
        std::uint32_t count = 0;
        std::int32_t error = 0;
        return path.text[0] && list_nand_strict_with(read_directory, &path, allocate_list, std::free,
            names, count, error) == NandList::Ok;
    }
    DIR* directory = opendir(location.path.c_str());
    if (!directory) return false;
    bool ok = true;
    errno = 0;
    while (const dirent* entry = readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (names.size() >= kEmuNandMaxNodes || !try_push_back(names, name)) {
            ok = false;
            break;
        }
        errno = 0;
    }
    if (errno != 0) ok = false;
    if (closedir(directory) != 0) ok = false;
    return ok;
}

Location child(const Location& root, const std::string& relative) {
    return {root.real, root.path + (relative.empty() ? "" : "/" + relative)};
}

struct Node {
    std::string relative;
    bool directory = false;
    std::uint64_t size = 0;
};

struct Tree {
    std::vector<Node> nodes;
    std::uint64_t bytes = 0;
};

// Finish the whole walk before deleting or writing anything. Names from a
// damaged card cannot escape the chosen root or become truncated ISFS paths.
bool scan_tree(const Location& root, const std::string& relative, unsigned depth,
               bool save, std::uint64_t title, Tree& tree, std::string& error) {
    const Location at = child(root, relative);
    std::uint64_t size = 0;
    const Kind kind = inspect(at, size);
    if (kind != Kind::Directory && kind != Kind::File) {
        error = "could not read " + at.path;
        return false;
    }
    const std::uint64_t file_limit = save ? kEmuNandMaxSaveFile : 512ULL * 1024 * 1024;
    const std::uint64_t total_limit = save ? kEmuNandMaxSaveBytes : 4ULL * 1024 * 1024 * 1024;
    if (depth > kEmuNandMaxDepth || tree.nodes.size() >= kEmuNandMaxNodes ||
        size > file_limit || size > total_limit - tree.bytes) {
        error = "the folder has too many files, too many subfolders or too much data";
        return false;
    }
    if (save) {
        std::string nand_path;
        if (!emunand_save_path(title, relative, nand_path)) {
            error = "a save filename cannot be used on the Wii";
            return false;
        }
    }
    tree.bytes += size;
    if (!try_push_back(tree.nodes, Node{relative, kind == Kind::Directory, size})) {
        error = "there is not enough memory to list the folder";
        return false;
    }
    if (kind == Kind::File) return true;
    std::vector<std::string> names;
    if (!directory_names(at, names)) {
        error = "could not list " + at.path;
        return false;
    }
    for (const std::string& name : names) {
        // The WAD installer keeps this marker alongside its contents.
        const bool marker = !save && name == ".riftwii-wad";
        if (!marker && !emunand_file_name(name)) {
            error = "the folder contains an unsafe filename";
            return false;
        }
        const std::string next = relative.empty() ? name : relative + "/" + name;
        if (!scan_tree(root, next, depth + 1, save, title, tree, error)) return false;
    }
    return true;
}

bool make_card_dirs(const std::string& path) {
    const std::size_t root = path.find(":/");
    if (root == std::string::npos) return false;
    for (std::size_t end = path.find('/', root + 2); ; end = path.find('/', end + 1)) {
        const std::string part = end == std::string::npos ? path : path.substr(0, end);
        std::uint64_t ignored = 0;
        const Kind kind = inspect({false, part}, ignored);
        if (kind != Kind::Directory && (kind != Kind::Missing || mkdir(part.c_str(), 0777) != 0)) return false;
        if (end == std::string::npos) return true;
    }
}

bool make_directory(const Location& location, const Attributes& attributes) {
    std::uint64_t ignored = 0;
    const Kind kind = inspect(location, ignored);
    if (kind == Kind::Directory) {
        if (!location.real) return true;
        // In the Wii's memory a folder already there (one a failed copy
        // left) must have the title's owner too, or is put right first.
        Attributes existing;
        if (!get_attributes(location.path, existing)) return false;
        if (existing.owner == attributes.owner && existing.group == attributes.group) return true;
        existing.owner = attributes.owner;
        existing.group = attributes.group;
        return set_attributes(location.path, existing);
    }
    if (kind != Kind::Missing) return false;
    if (!location.real) return make_card_dirs(location.path);
    const NandPath path(location.path);
    if (!path.text[0] || ISFS_CreateDir(path.text, attributes.flags, attributes.owner_perm,
        attributes.group_perm, attributes.other_perm) < 0) return false;
    if (set_attributes(location.path, attributes)) return true;
    // Not left behind with the wrong owner: a retry would take it as it is.
    ISFS_Delete(path.text);
    return false;
}

bool remove_one(const Location& location, bool directory) {
    if (!location.real) return (directory ? rmdir(location.path.c_str()) : std::remove(location.path.c_str())) == 0;
    const NandPath path(location.path);
    return path.text[0] && ISFS_Delete(path.text) >= 0;
}

bool rename_one(const Location& from, const Location& to) {
    if (from.real != to.real) return false;
    if (!from.real) return std::rename(from.path.c_str(), to.path.c_str()) == 0;
    const NandPath old_path(from.path);
    const NandPath new_path(to.path);
    return old_path.text[0] && new_path.text[0] && ISFS_Rename(old_path.text, new_path.text) >= 0;
}

bool unused_path(const Location& base, const std::string& suffix, Location& result) {
    for (unsigned index = 0; index < 10000; ++index) {
        result = {base.real, base.path + suffix + std::to_string(index)};
        std::uint64_t ignored = 0;
        const Kind kind = inspect(result, ignored);
        if (kind == Kind::Missing) return true;
        if (kind == Kind::Failed) return false;
    }
    return false;
}

// Temporary real-NAND files stay in the permitted data folder (or the
// FaceLib folder for Miis), with a twelve-byte name and no overwrite.
bool temporary_path(const Location& root, const std::string& destination, Location& result) {
    for (unsigned index = 0; index < 10000; ++index) {
        char name[13];
        std::snprintf(name, sizeof(name), "rw%06u.tmp", index);
        result = child(root, name);
        if (result.path == destination) continue;
        if (result.real && result.path.size() >= 64) return false;
        std::uint64_t ignored = 0;
        const Kind kind = inspect(result, ignored);
        if (kind == Kind::Missing) return true;
        if (kind == Kind::Failed) return false;
    }
    return false;
}

// Both endpoints are already bounded by the manifest. Copy in small aligned
// pieces, close successfully, then publish with rename. Never delete the old
// file to make room for the new one.
bool copy_file(const Location& from, const Location& to, std::uint64_t size,
               const Location& temp_root, const Attributes& inherited, std::string& error) {
    std::uint64_t current_size = 0;
    if (inspect(from, current_size) != Kind::File || current_size != size) {
        error = "the source file changed or could not be read: " + from.path;
        return false;
    }
    const Kind destination_kind = inspect(to, current_size);
    if (destination_kind != Kind::Missing && destination_kind != Kind::File) {
        error = "the destination is not a writable file: " + to.path;
        return false;
    }
    Attributes attributes = inherited;
    if (to.real && destination_kind == Kind::File && !get_attributes(to.path, attributes)) {
        error = "could not read the destination file's permissions";
        return false;
    }
    Location temp;
    if (!temporary_path(temp_root, to.path, temp)) {
        error = "there is no unused temporary filename";
        return false;
    }
    using Buffer = std::unique_ptr<u8, decltype(&std::free)>;
    Buffer buffer(static_cast<u8*>(memalign(32, kChunk)), &std::free);
    if (!buffer) {
        error = "there is not enough memory to copy a file";
        return false;
    }
    const NandPath source_path(from.path);
    const NandPath temp_path(temp.path);
    const s32 input_nand = from.real ? ISFS_Open(source_path.text, ISFS_OPEN_READ) : -1;
    FILE* input_card = from.real ? nullptr : std::fopen(from.path.c_str(), "rb");
    bool created = false;
    s32 output_nand = -1;
    FILE* output_card = nullptr;
    if (from.real ? input_nand >= 0 : input_card != nullptr) {
        if (to.real) {
            created = ISFS_CreateFile(temp_path.text, attributes.flags, attributes.owner_perm,
                attributes.group_perm, attributes.other_perm) >= 0;
            if (created) output_nand = ISFS_Open(temp_path.text, ISFS_OPEN_WRITE);
        } else {
            output_card = std::fopen(temp.path.c_str(), "wb");
            created = output_card != nullptr;
        }
    }
    bool ok = created && (to.real ? output_nand >= 0 : output_card != nullptr);
    std::uint64_t done = 0;
    while (ok && done < size) {
        const u32 bytes = static_cast<u32>(std::min<std::uint64_t>(kChunk, size - done));
        ok = from.real ? ISFS_Read(input_nand, buffer.get(), bytes) == static_cast<s32>(bytes)
                       : std::fread(buffer.get(), 1, bytes, input_card) == bytes;
        if (ok) {
            ok = to.real ? ISFS_Write(output_nand, buffer.get(), bytes) == static_cast<s32>(bytes)
                         : std::fwrite(buffer.get(), 1, bytes, output_card) == bytes;
        }
        done += bytes;
    }
    if (input_nand >= 0 && ISFS_Close(input_nand) < 0) ok = false;
    if (input_card && std::fclose(input_card) != 0) ok = false;
    if (output_nand >= 0 && ISFS_Close(output_nand) < 0) ok = false;
    if (output_card && std::fclose(output_card) != 0) ok = false;
    if (ok && to.real) ok = set_attributes(temp.path, attributes);

    Location old;
    bool moved_old = false;
    if (ok && destination_kind == Kind::File) {
        ok = temporary_path(temp_root, to.path, old) && rename_one(to, old);
        moved_old = ok;
    }
    if (ok) ok = rename_one(temp, to);
    if (!ok && moved_old && !rename_one(old, to)) {
        logf("Emulated NAND: the old file could not be put back; it remains at %s.\n", old.path.c_str());
    }
    if (!ok) {
        if (created) remove_one(temp, false);
        error = "could not finish copying " + from.path + " to " + to.path;
        return false;
    }
    if (moved_old && !remove_one(old, false)) {
        error = "the copy finished, but an old temporary file could not be removed: " + old.path;
        return false;
    }
    return true;
}

bool copy_tree(const Location& from, const Location& to, const Tree& tree,
               const Attributes& attributes, std::string& error) {
    for (const Node& node : tree.nodes) {
        const Location destination = child(to, node.relative);
        if (node.directory) {
            if (!make_directory(destination, attributes)) {
                error = "could not create " + destination.path;
                return false;
            }
        } else if (!copy_file(child(from, node.relative), destination, node.size, to, attributes, error)) {
            return false;
        }
    }
    return true;
}

bool delete_tree(const Location& root, const Tree& tree, std::string& error) {
    for (auto node = tree.nodes.rbegin(); node != tree.nodes.rend(); ++node) {
        const Location at = child(root, node->relative);
        if (!remove_one(at, node->directory)) {
            error = "could not delete " + at.path + "; some files may already have been removed";
            return false;
        }
    }
    return true;
}

bool backup_tree(const Location& source, const Tree& tree, const std::string& base, std::string& error) {
    Location backup;
    if (!unused_path({false, base}, ".bak", backup) ||
        !copy_tree(source, backup, tree, Attributes{}, error)) {
        if (error.empty()) error = "could not create a backup";
        return false;
    }
    logf("Emulated NAND: kept a backup of %s at %s.\n", source.path.c_str(), backup.path.c_str());
    return true;
}

bool backup_file(const Location& source, std::uint64_t size, const std::string& base, std::string& error) {
    Location backup;
    if (!unused_path({false, base}, ".bak", backup)) {
        error = "could not choose a backup filename";
        return false;
    }
    const Location parent{false, backup.path.substr(0, backup.path.find_last_of('/'))};
    if (!make_card_dirs(parent.path) || !copy_file(source, backup, size, parent, Attributes{}, error)) return false;
    logf("Emulated NAND: kept a backup of %s at %s.\n", source.path.c_str(), backup.path.c_str());
    return true;
}

bool read_small(const Location& source, std::size_t limit, std::vector<std::uint8_t>& bytes) {
    std::uint64_t size = 0;
    if (inspect(source, size) != Kind::File || size > limit || !try_resize(bytes, static_cast<std::size_t>(size))) return false;
    if (!source.real) {
        FILE* file = std::fopen(source.path.c_str(), "rb");
        if (!file) return false;
        const bool ok = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
        return std::fclose(file) == 0 && ok;
    }
    const NandPath path(source.path);
    const s32 file = ISFS_Open(path.text, ISFS_OPEN_READ);
    if (file < 0) return false;
    void* buffer = memalign(32, (static_cast<std::size_t>(size) + 31) & ~std::size_t(31));
    const bool ok = buffer && ISFS_Read(file, buffer, static_cast<u32>(size)) == static_cast<s32>(size);
    if (ok) std::memcpy(bytes.data(), buffer, bytes.size());
    std::free(buffer);
    return ISFS_Close(file) >= 0 && ok;
}

// True when the card file reads back whole with `size` bytes and the SHA-1
// the title's TMD names: only then is a shared content already in the
// destination used instead of a fresh copy.
bool card_file_matches(const std::string& path, std::uint64_t size, const Sha1Digest& expected) {
    std::uint64_t listed_size = 0;
    if (inspect({false, path}, listed_size) != Kind::File || listed_size != size) return false;
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    constexpr std::size_t kChunk = 32 * 1024;
    std::unique_ptr<std::uint8_t[]> buffer(new (std::nothrow) std::uint8_t[kChunk]);
    Sha1 hash;
    std::uint64_t read_total = 0;
    bool ok = buffer != nullptr;
    while (ok && read_total < size) {
        const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, size - read_total));
        ok = std::fread(buffer.get(), 1, want, file) == want;
        if (ok) hash.update(buffer.get(), want);
        read_total += want;
    }
    const bool closed = std::fclose(file) == 0;
    return ok && closed && hash.finish() == expected;
}

// Installation is checked through ES again at write time. The ownership of
// a newly created data folder comes from this Wii's uid.sys, never the card.
bool real_title_attributes(std::uint64_t title, Attributes& attributes) {
    u32 size = 0;
    if (!emunand_manageable_title(title) || ES_GetStoredTMDSize(title, &size) < 0 || size < 0x1E4 || size > 0x10000) return false;
    void* buffer = memalign(32, (size + 31) & ~31u);
    if (!buffer) return false;
    TmdContents tmd;
    std::string why;
    const bool installed = ES_GetStoredTMD(title, static_cast<signed_blob*>(buffer), size) >= 0 &&
        parse_tmd_contents(static_cast<const u8*>(buffer), size, tmd, why) && tmd.title_id == title;
    const auto* tmd_bytes = static_cast<const u8*>(buffer);
    const u16 group = installed ? (static_cast<u16>(tmd_bytes[0x198]) << 8) | tmd_bytes[0x199] : 0;
    std::free(buffer);
    if (!installed) return false;
    // The owner always comes from this Wii's uid.sys, never from a folder
    // already there: one left by a failed import could have another.
    std::vector<std::uint8_t> uids;
    if (!read_small({true, "/sys/uid.sys"}, 12 * 8192, uids) || uids.size() % 12 != 0) return false;
    u32 owner = 0;
    for (std::size_t at = 0; at < uids.size() && owner == 0; at += 12) {
        std::uint64_t entry = 0;
        for (unsigned i = 0; i < 8; ++i) entry = (entry << 8) | uids[at + i];
        if (entry != title) continue;
        for (unsigned i = 8; i < 12; ++i) owner = (owner << 8) | uids[at + i];
    }
    if (owner < 0x1000) return false;
    EmuNandTitlePaths paths;
    emunand_title_paths(title, paths);
    Attributes existing;
    if (get_attributes(paths.data, existing)) {
        // The folder's own permissions, the title's owner and group: put
        // right first when they differ, or nothing is copied.
        attributes = existing;
        attributes.owner = owner;
        attributes.group = group;
        if (existing.owner == owner && existing.group == group) return true;
        logf("Emulated NAND: %s had another owner; it is set back to the title's.\n", paths.data.c_str());
        return set_attributes(paths.data, attributes);
    }
    attributes = {};
    attributes.owner = owner;
    attributes.group = group;
    return true;
}

std::string title_name(const Location& root, std::uint64_t title) {
    const std::string code = emunand_title_code(title);
    // The game-name list keys disc games by their 6-char ID (RSBE01): a
    // save's 4-char code finds its game there too.
    const TitleTable* names = LoadedTitles();
    if (names == nullptr) {
        // Not read yet (the manager opened before anything else asked for
        // a name): one lookup reads the list, as the game pages do.
        std::string ignored;
        GameTitleListed(code, ignored);
        names = LoadedTitles();
    }
    const std::string* listed = names ? names->find_code(code) : nullptr;
    if (listed && !listed->empty()) return *listed + " (" + code + ")";
    // Disc saves and add-on data have no channel banner. Avoid opening a
    // TMD and content file for every save in a large copied NAND.
    const std::uint32_t type = title >> 32;
    if (type == 0x00010000 || type == 0x00010005) return code;

    EmuNandTitlePaths paths;
    if (!emunand_title_paths(title, paths)) return code;
    std::vector<std::uint8_t> bytes;
    TmdContents tmd;
    std::string why;
    if (read_small(child(root, paths.title.substr(1) + "/content/title.tmd"), 0x10000, bytes) &&
        parse_tmd_contents(bytes.data(), bytes.size(), tmd, why) && tmd.title_id == title) {
        const TmdContent* banner = tmd_content_by_index(tmd, 0);
        if (banner) {
            const Location content = child(root, nand_content_path(title, banner->id).substr(1));
            std::uint8_t head[0x700] ATTRIBUTE_ALIGN(32);
            std::size_t got = 0;
            if (content.real) {
                const NandPath path(content.path);
                const s32 file = ISFS_Open(path.text, ISFS_OPEN_READ);
                if (file >= 0) {
                    const s32 read = ISFS_Read(file, head, sizeof(head));
                    if (read > 0) got = static_cast<std::size_t>(read);
                    ISFS_Close(file);
                }
            } else if (FILE* file = std::fopen(content.path.c_str(), "rb")) {
                got = std::fread(head, 1, sizeof(head), file);
                std::fclose(file);
            }
            std::array<std::string, kBnrLanguages> names;
            if (parse_banner_names(head, got, names)) {
                if (!names[1].empty()) return names[1];  // English, as the channel reader's fallback.
                for (const std::string& name : names) if (!name.empty()) return name;
            }
        }
    }
    return code;
}

bool perform(const std::string& root, EmuNandAction action, std::uint64_t title, std::string& error) {
    EmuNandTitlePaths paths;
    const bool title_action = action == EmuNandAction::DeleteTitle || action == EmuNandAction::DeleteSave ||
        action == EmuNandAction::SaveToEmulated || action == EmuNandAction::SaveToWii;
    if (title_action && !emunand_title_paths(title, paths)) {
        error = "system titles and unknown title types cannot be changed";
        return false;
    }
    if (action == EmuNandAction::DeleteTitle || action == EmuNandAction::DeleteSave) {
        if (emunand_delete_protected(title)) {
            error = "system titles cannot be deleted";
            return false;
        }
        const Location source{false, root + (action == EmuNandAction::DeleteTitle ? paths.title : paths.data)};
        if (action == EmuNandAction::DeleteTitle) {
            // An earlier delete that removed the title but not its ticket:
            // with the title folder surely gone, the ticket file goes now.
            std::uint64_t ignored = 0;
            const Location ticket{false, root + paths.ticket};
            if (inspect(source, ignored) == Kind::Missing && inspect(ticket, ignored) == Kind::File) {
                if (remove_one(ticket, false)) return true;
                error = "the title's ticket could not be removed";
                return false;
            }
        }
        Tree tree;
        if (!scan_tree(source, "", 0, action == EmuNandAction::DeleteSave, title, tree, error)) return false;
        // Check the ticket before deleting the title, so a directory posing
        // as a ticket cannot trigger recursive deletion outside the title.
        const Location ticket{false, root + paths.ticket};
        std::uint64_t size = 0;
        const Kind ticket_kind = inspect(ticket, size);
        if (action == EmuNandAction::DeleteTitle && ticket_kind != Kind::File && ticket_kind != Kind::Missing) {
            error = "the title's ticket could not be read";
            return false;
        }
        if (!delete_tree(source, tree, error)) return false;
        if (action == EmuNandAction::DeleteTitle && ticket_kind == Kind::File && !remove_one(ticket, false)) {
            error = "the title was deleted, but its ticket could not be removed";
            return false;
        }
        return true;
    }
    if (action == EmuNandAction::SaveToEmulated || action == EmuNandAction::SaveToWii) {
        const bool into_wii = action == EmuNandAction::SaveToWii;
        const Location source{!into_wii, (into_wii ? root : "") + paths.data};
        const Location destination{into_wii, (into_wii ? "" : root) + paths.data};
        Attributes attributes;
        if (into_wii && !real_title_attributes(title, attributes)) {
            error = "this title is not installed on the Wii, or its save permissions could not be read";
            return false;
        }
        Tree incoming;
        if (!scan_tree(source, "", 0, true, title, incoming, error) || incoming.nodes.empty() || !incoming.nodes[0].directory) {
            if (error.empty()) error = "the source is not a save folder";
            return false;
        }
        if (into_wii) {
            // RiftWii's clone marker is bookkeeping, not part of the game's
            // save. A folder containing only that marker is no save to import.
            incoming.nodes.erase(std::remove_if(incoming.nodes.begin(), incoming.nodes.end(), [](const Node& node) {
                return node.relative == "riftwii.cln";
            }), incoming.nodes.end());
        }
        const bool has_save = std::any_of(incoming.nodes.begin(), incoming.nodes.end(), [](const Node& node) {
            return !node.directory;
        });
        if (!has_save) {
            error = "the source folder contains no save files";
            return false;
        }
        std::uint64_t ignored = 0;
        const Kind existing = inspect(destination, ignored);
        Tree previous;
        if (existing == Kind::Directory) {
            if (!scan_tree(destination, "", 0, true, title, previous, error)) return false;
            // The save being replaced is backed up to the card (FAT) whichever
            // way it goes: two of its names the card can't tell apart would
            // leave a backup missing one file. Refused before anything changes.
            for (std::size_t i = 0; i < previous.nodes.size(); ++i) {
                for (std::size_t j = i + 1; j < previous.nodes.size(); ++j) {
                    const std::string& a = previous.nodes[i].relative;
                    const std::string& b = previous.nodes[j].relative;
                    if (a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                            return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
                        })) {
                        error = "the save being replaced has two names the SD card can't tell apart, so it can't be backed up";
                        return false;
                    }
                }
            }
            if (!backup_tree(destination, previous, root + "/riftwii-backups/" + emunand_title_text(title) +
                    (into_wii ? "/wii-save" : "/emulated-save"), error)) return false;
        } else if (existing != Kind::Missing) {
            error = "the destination save folder could not be read";
            return false;
        }
        // FAT (the card) ignores case: SAVE.BIN and save.bin are one file
        // there. The Wii's own memory doesn't.
        const auto same_name = [&](const std::string& a, const std::string& b) {
            if (destination.real) return a == b;
            return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
            });
        };
        // Two incoming names that would be one file on the card: refused
        // before anything is written.
        for (std::size_t i = 0; i < incoming.nodes.size(); ++i) {
            for (std::size_t j = i + 1; j < incoming.nodes.size(); ++j) {
                if (same_name(incoming.nodes[i].relative, incoming.nodes[j].relative)) {
                    error = "the save has two names the SD card can't tell apart";
                    return false;
                }
            }
        }
        // Fail before changing files if the two saves disagree about whether
        // a path is a file or a folder.
        for (const Node& old : previous.nodes) {
            const auto match = std::find_if(incoming.nodes.begin(), incoming.nodes.end(), [&](const Node& node) {
                return same_name(old.relative, node.relative);
            });
            if (match != incoming.nodes.end() && match->directory != old.directory) {
                error = "the saves use the same name for a file and a folder";
                return false;
            }
        }
        if (!copy_tree(source, destination, incoming, attributes, error)) return false;
        // A save is a complete folder, not a merge. Remove old-only entries
        // only after all incoming files are published and a backup is safe.
        for (auto old = previous.nodes.rbegin(); old != previous.nodes.rend(); ++old) {
            const bool kept = std::any_of(incoming.nodes.begin(), incoming.nodes.end(), [&](const Node& node) {
                return same_name(old->relative, node.relative);
            });
            if (!kept && !remove_one(child(destination, old->relative), old->directory)) {
                error = "the save was copied, but an old file could not be removed";
                return false;
            }
        }
        return true;
    }
    if (action == EmuNandAction::MiisToEmulated || action == EmuNandAction::MiisToWii) {
        const bool into_wii = action == EmuNandAction::MiisToWii;
        const Location source{!into_wii, (into_wii ? root : "") + kEmuNandMiis};
        const Location destination{into_wii, (into_wii ? "" : root) + kEmuNandMiis};
        std::uint64_t size = 0;
        if (inspect(source, size) != Kind::File || size == 0 || size > 1024 * 1024) {
            error = "the source Mii database is empty, too large or could not be read";
            return false;
        }
        std::uint64_t old_size = 0;
        const Kind existing = inspect(destination, old_size);
        Attributes attributes;
        if (existing == Kind::File) {
            // This is a byte-for-byte database transfer. Match this Wii's
            // existing database length before accepting an imported copy.
            if (into_wii && size != old_size) {
                error = "the imported Mii database has a different size from the Wii's database";
                return false;
            }
            if (old_size > 1024 * 1024) {
                error = "the old Mii database is too large to back up";
                return false;
            }
            if (!backup_file(destination, old_size, root + kEmuNandMiis + (into_wii ? ".wii" : ""), error)) return false;
            if (into_wii && !get_attributes(destination.path, attributes)) {
                error = "the Wii's Mii file permissions could not be read";
                return false;
            }
        } else if (existing != Kind::Missing || into_wii) {
            error = "the destination Mii database could not be read";
            return false;
        }
        const Location parent{into_wii, destination.path.substr(0, destination.path.find_last_of('/'))};
        if (!into_wii && !make_card_dirs(parent.path)) {
            error = "the Mii folder could not be created";
            return false;
        }
        return copy_file(source, destination, size, parent, attributes, error);
    }
    if (action == EmuNandAction::RemoveSettings) {
        // Back up both files before removing either one. These are the only
        // system-title files the manager can change, and only on the card.
        std::vector<Location> files;
        for (const char* relative : {kEmuNandSysconf, kEmuNandSettings}) {
            const Location file{false, root + relative};
            std::uint64_t size = 0;
            const Kind kind = inspect(file, size);
            if (kind == Kind::Missing) continue;
            if (kind != Kind::File || size > 1024 * 1024 || !backup_file(file, size, file.path, error)) {
                if (error.empty()) error = "the settings file could not be backed up";
                return false;
            }
            files.push_back(file);
        }
        if (files.empty()) {
            error = "the emulated NAND has no settings files";
            return false;
        }
        for (const Location& file : files) {
            if (!remove_one(file, false)) {
                error = "a settings file could not be removed; the backup was kept";
                return false;
            }
        }
        return true;
    }
    error = "the requested action is unknown";
    return false;
}

// Writes `bytes` as `to` on a card: a temporary file in `temp_root`, then
// renamed over the old one, as copy_file publishes.
bool write_card_file(const Location& to, const std::vector<std::uint8_t>& bytes, const Location& temp_root,
                     std::string& error) {
    Location temp;
    if (!temporary_path(temp_root, to.path, temp)) {
        error = "there is no unused temporary filename";
        return false;
    }
    FILE* file = std::fopen(temp.path.c_str(), "wb");
    bool ok = file != nullptr;
    if (ok && !bytes.empty()) ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    if (file && std::fclose(file) != 0) ok = false;
    std::uint64_t ignored = 0;
    const Kind existing = ok ? inspect(to, ignored) : Kind::Failed;
    Location old;
    bool moved_old = false;
    if (ok && existing == Kind::File) {
        ok = temporary_path(temp_root, to.path, old) && rename_one(to, old);
        moved_old = ok;
    } else if (ok && existing != Kind::Missing) {
        ok = false;
    }
    if (ok) ok = rename_one(temp, to);
    if (!ok && moved_old) rename_one(old, to);
    if (!ok) {
        if (file) remove_one(temp, false);
        error = "could not write " + to.path;
        return false;
    }
    if (moved_old) remove_one(old, false);
    return true;
}

// A shared content the title needs: its file in the source, and the name
// it takes in the destination (new, or one there already with its hash).
struct SharedCopy {
    std::string from;
    std::string name;
    std::uint64_t size = 0;
    bool present = false;  // the destination has it already
};

bool read_content_map(const std::string& root, std::vector<SharedContent>& map, std::string& error) {
    map.clear();
    std::vector<std::uint8_t> bytes;
    const Location file{false, root + "/shared1/content.map"};
    std::uint64_t size = 0;
    const Kind kind = inspect(file, size);
    if (kind == Kind::Missing) return true;
    if (kind != Kind::File || !read_small(file, 1u << 20, bytes) || bytes.size() % 28 != 0) {
        error = "the shared-content list in " + root + " is damaged or could not be read";
        return false;
    }
    map = parse_content_map(bytes.data(), bytes.size());
    if (map.size() * 28 != bytes.size()) {
        error = "the shared-content list in " + root + " is damaged";
        return false;
    }
    return true;
}

bool copy_title(const std::string& from, const std::string& to, std::uint64_t title, bool& already_there,
                std::string& error) {
    EmuNandTitlePaths paths;
    if (!emunand_title_paths(title, paths)) {
        error = "system titles and unknown title types cannot be copied";
        return false;
    }
    // Everything is read and checked first; nothing is written until then.
    std::vector<std::uint8_t> tmd_bytes;
    TmdContents tmd;
    std::string why;
    if (!read_small({false, from + paths.title + "/content/title.tmd"}, 0x10000, tmd_bytes) ||
        !parse_tmd_contents(tmd_bytes.data(), tmd_bytes.size(), tmd, why) || tmd.title_id != title) {
        error = "the title's TMD is missing or damaged, so its program can't be copied";
        return false;
    }
    const Location destination{false, to + paths.title};
    std::uint64_t size = 0;
    const Kind there = inspect(destination, size);
    if (there == Kind::Directory || there == Kind::File) {
        already_there = true;
        error = "the title is already in the destination; delete it there first to copy it again";
        return false;
    }
    if (there != Kind::Missing) {
        error = "the destination could not be read";
        return false;
    }
    Tree tree;
    if (!scan_tree({false, from + paths.title}, "", 0, false, title, tree, error)) return false;
    const Location ticket{false, from + paths.ticket};
    std::uint64_t ticket_size = 0;
    if (inspect(ticket, ticket_size) != Kind::File || ticket_size == 0 || ticket_size > 0x10000) {
        error = "the title's ticket is missing, so the copy could not start";
        return false;
    }
    std::vector<SharedContent> from_map, to_map;
    std::vector<SharedCopy> shared;
    bool map_changed = false;
    for (const TmdContent& content : tmd.contents) {
        if ((content.type & 0x8000) == 0) continue;
        if (from_map.empty() && !read_content_map(from, from_map, error)) return false;
        if (shared.empty() && to_map.empty() && !read_content_map(to, to_map, error)) return false;
        SharedCopy copy;
        const SharedContent* known = find_shared_content(to_map, content.sha1);
        if (known) {
            // Listed there: used as it is only when its file reads back whole.
            copy.name = known->name;
            copy.present = card_file_matches(to + "/shared1/" + copy.name + ".app", content.size, content.sha1);
        }
        if (!copy.present) {
            const SharedContent* source = find_shared_content(from_map, content.sha1);
            if (!source) {
                error = "a shared content the title uses is missing from the source";
                return false;
            }
            copy.from = from + "/shared1/" + source->name + ".app";
            if (inspect({false, copy.from}, copy.size) != Kind::File || copy.size != content.size) {
                error = "a shared content the title uses could not be read";
                return false;
            }
        }
        if (!known) {
            copy.name = next_shared_name(to_map);
            if (copy.name.size() != 8 || !try_push_back(to_map, SharedContent{copy.name, content.sha1})) {
                error = "the destination has too many shared contents";
                return false;
            }
            map_changed = true;
        }
        if (!try_push_back(shared, copy)) {
            error = "there is not enough memory to list the shared contents";
            return false;
        }
    }
    // ES lets a title use its save folder by this list's entry for it.
    const Location uid{false, to + "/sys/uid.sys"};
    std::vector<std::uint8_t> uid_bytes;
    const Kind uid_kind = inspect(uid, size);
    if (uid_kind == Kind::File && !read_small(uid, 12 * 8192, uid_bytes)) {
        error = "the destination's title ownership list could not be read";
        return false;
    }
    if (uid_kind != Kind::File && uid_kind != Kind::Missing) {
        error = "the destination's title ownership list could not be read";
        return false;
    }
    if (!add_wad_title_uid(uid_bytes, title, error)) return false;

    // The folders as they are mounted here: the caller's lines name them.
    logf("Emulated NAND: copying %s: %u files and folders, %llu bytes, %u shared content(s).\n",
         emunand_title_text(title).c_str(), static_cast<unsigned>(tree.nodes.size()),
         static_cast<unsigned long long>(tree.bytes), static_cast<unsigned>(shared.size()));
    // The title's own folder first, then what makes it usable. Cut short,
    // the half-copied title is taken out again; a shared content copied
    // before that stays unused, harmless, until the list names it.
    if (!copy_tree({false, from + paths.title}, destination, tree, Attributes{}, error)) {
        std::string ignored_error;
        Tree partial;
        if (scan_tree(destination, "", 0, false, title, partial, ignored_error)) delete_tree(destination, partial, ignored_error);
        return false;
    }
    const Location ticket_copy{false, to + paths.ticket};
    std::uint64_t ignored_size = 0;
    const bool ticket_was_there = inspect(ticket_copy, ignored_size) == Kind::File;
    bool ticket_copied = false;
    // Best effort: a drive taken out keeps what was written to it.
    const auto undo = [&](const std::string& why_failed) {
        error = why_failed;
        std::string ignored_error;
        delete_tree(destination, tree, ignored_error);
        if (ticket_copied && !ticket_was_there) remove_one(ticket_copy, false);
        return false;
    };
    const std::string ticket_dir = ticket_copy.path.substr(0, ticket_copy.path.find_last_of('/'));
    if (!make_card_dirs(ticket_dir) ||
        !copy_file(ticket, ticket_copy, ticket_size, {false, ticket_dir}, Attributes{}, error))
        return undo(error.empty() ? "the ticket could not be copied" : error);
    ticket_copied = true;
    if (!shared.empty() && !make_card_dirs(to + "/shared1")) return undo("the shared-content folder could not be made");
    for (const SharedCopy& copy : shared) {
        if (copy.present) continue;
        if (!copy_file({false, copy.from}, {false, to + "/shared1/" + copy.name + ".app"}, copy.size,
                       {false, to + "/shared1"}, Attributes{}, error))
            return undo(error);
    }
    if (map_changed) {
        const std::vector<std::uint8_t> encoded = serialize_content_map(to_map);
        if (encoded.size() != to_map.size() * 28 ||
            !write_card_file({false, to + "/shared1/content.map"}, encoded, {false, to + "/shared1"}, error))
            return undo(error.empty() ? "the shared-content list could not be written" : error);
    }
    if (!make_card_dirs(to + "/sys") || !write_card_file(uid, uid_bytes, {false, to + "/sys"}, error))
        return undo(error.empty() ? "the title ownership list could not be written" : error);
    return true;
}

}  // namespace

std::string emunand_copy_destination(const std::string& folder) {
    std::string checked;
    if (!emunand_root(folder, checked)) return std::string();
    // The folder Home lists channels from when it is on the SD card, else
    // sd:/nands/01.
    const SharedNandLookup channels = look_up_channel_nand();
    const bool channels_on_sd = channels.usable && channels.folder.compare(0, 4, "sd:/") == 0;
    const std::string sd_channels = channels_on_sd ? channels.folder : std::string(kSdSharedNand);
    if (shared_nand_on_usb(checked)) return sd_channels;
    if (checked.compare(0, 4, "sd:/") != 0) return std::string();
    // Another folder on the card (WiiFlow's nands/default, one a PC tool
    // filled): into that folder. That one itself: to the USB drive.
    return checked != sd_channels ? sd_channels : std::string(kUsbSharedNand);
}

bool emunand_manager_copy_title(const std::string& from, const std::string& to, std::uint64_t title,
                                bool& already_there, std::string& error) {
    error.clear();
    already_there = false;
    logf("Emulated NAND: asked to copy title %s from %s to %s.\n", emunand_title_text(title).c_str(), from.c_str(),
         to.c_str());
    std::string from_root, to_root;
    bool ok = emunand_root(from, from_root) && emunand_root(to, to_root) && from_root != to_root;
    if (!ok) error = "the two emulated NAND folders are not safe to use";
    // One drive's partition is mounted for writing at a time: the other
    // folder has to be on the SD card, which always is.
    if (ok && shared_nand_on_usb(from_root) && shared_nand_on_usb(to_root)) {
        error = "both folders are on the USB drive; copy through the SD card";
        ok = false;
    }
    CardRoot source;
    CardRoot target;
    if (ok) ok = source.Open(from_root, error) && target.Open(to_root, error);
    if (ok) ok = copy_title(source.path, target.path, title, already_there, error);
    if (ok) logf("Emulated NAND: copied title %s to %s; the original stays in %s.\n", emunand_title_text(title).c_str(),
                 to.c_str(), from.c_str());
    else logf("Emulated NAND: could not copy title %s because %s.\n", emunand_title_text(title).c_str(), error.c_str());
    return ok;
}

std::vector<std::string> emunand_manager_folders() {
    std::vector<std::string> folders;
    // `check`: look that the folder is there first (the default places are
    // offered whether or not the card or drive has one). The /nands loop
    // has just seen its folders on its own mount, and a second mount here
    // would unmount the drive under it when it closed.
    const auto add = [&](const std::string& candidate, bool check) {
        std::string checked;
        if (folders.size() >= kMaxFolders || !emunand_root(candidate, checked) ||
            std::find(folders.begin(), folders.end(), checked) != folders.end()) return;
        if (check) {
            CardRoot root;
            std::string error;
            std::uint64_t ignored = 0;
            if (!root.Open(checked, error) || inspect({false, root.path}, ignored) != Kind::Directory) return;
        }
        try_push_back(folders, checked);
    };
    add(look_up_shared_nand("").folder, true);
    add(look_up_channel_nand().folder, true);
    add(kSdSharedNand, true);
    add(kUsbSharedNand, true);
    for (const char* drive : {"sd:", "usb:"}) {
        CardRoot mounted;
        std::string error;
        if (!mounted.Open(std::string(drive) + "/nands", error)) continue;
        std::vector<std::string> names;
        if (!directory_names({false, mounted.path}, names)) continue;
        for (const std::string& name : names) {
            std::string checked;
            if (!emunand_root(std::string(drive) + "/nands/" + name, checked)) continue;
            std::uint64_t ignored = 0;
            if (inspect({false, mounted.path + "/" + name}, ignored) == Kind::Directory)
                add(std::string(drive) + "/nands/" + name, false);
        }
    }
    // A per-game folder can be outside /nands. Read only bounded whole lines
    // of the same settings file the launch lookup uses.
    // Collected first, checked after the file is closed: checking a USB
    // folder mounts its partition again, which would pull the drive out
    // from under the settings file still open on it.
    std::vector<std::string> per_game;
    for (const char* config : {"sd:/apps/usbloader_gx/GXGameSettings.cfg", "usb:/apps/usbloader_gx/GXGameSettings.cfg"}) {
        FILE* file = std::fopen(config, "rb");
        if (!file) continue;
        constexpr std::size_t kLine = 20 * 1024;
        std::unique_ptr<char[]> buffer(new (std::nothrow) char[kLine]);
        if (!buffer) {
            std::fclose(file);
            continue;
        }
        char* line = buffer.get();
        std::size_t total = 0;
        bool start = true;
        while (total < 4 * 1024 * 1024 && std::fgets(line, static_cast<int>(kLine), file)) {
            const std::size_t length = std::strlen(line);
            const bool whole_start = start;
            start = length > 0 && line[length - 1] == '\n';
            total += length;
            // Only a whole record: one that ends here (or at the file's
            // end) and started at a line's start. A longer one, cut by the
            // buffer, could give a shorter folder name than it has.
            bool whole_end = start || std::feof(file);
            if (!whole_end) {
                // A full buffer and no newline: the file may end right here.
                const int next = std::fgetc(file);
                // EOF also means a read error: only a real end counts.
                whole_end = next == EOF && std::feof(file) && !std::ferror(file);
                if (next != EOF) std::ungetc(next, file);
            }
            if (!whole_start || !whole_end || std::ferror(file)) continue;
            // As the launch reads it (shared_nand_game_line): a UTF-8 BOM
            // and leading blanks before "game:" are allowed.
            const char* at = line;
            if (std::strncmp(at, "\xEF\xBB\xBF", 3) == 0) at += 3;
            while (*at == ' ' || *at == '\t') ++at;
            if (std::strncmp(at, "game:", 5) != 0 || std::strlen(at) < 11) continue;
            std::string path;
            std::string checked;
            // Many games share a folder: each folder counts once, compared
            // as emunand_root writes it (no trailing or doubled slashes).
            if (shared_nand_game_line(line, std::string(at + 5, 6), path) && emunand_root(path, checked) &&
                per_game.size() < kMaxFolders && std::find(per_game.begin(), per_game.end(), checked) == per_game.end())
                try_push_back(per_game, checked);
        }
        std::fclose(file);
    }
    for (const std::string& path : per_game) add(path, true);
    return folders;
}

bool emunand_manager_list(const std::string& folder, std::vector<EmuNandTitle>& titles,
                          bool& sysconf, bool& settings, std::string& error) {
    titles.clear();
    error.clear();
    sysconf = settings = false;
    CardRoot root;
    if (!root.Open(folder, error)) return false;
    std::uint64_t ignored = 0;
    if (inspect({false, root.path}, ignored) != Kind::Directory) {
        error = "the emulated NAND folder is missing or could not be read";
        return false;
    }
    sysconf = inspect({false, root.path + kEmuNandSysconf}, ignored) == Kind::File;
    settings = inspect({false, root.path + kEmuNandSettings}, ignored) == Kind::File;
    std::vector<std::string> types;
    const Kind title_kind = inspect({false, root.path + "/title"}, ignored);
    if (title_kind != Kind::Missing && (title_kind != Kind::Directory || !directory_names({false, root.path + "/title"}, types))) {
        error = "the emulated NAND's title folder could not be read";
        return false;
    }
    for (const std::string& type : types) {
        std::uint64_t type_id = 0;
        if (type.size() != 8 || !emunand_parse_title(type + "00000001", type_id) ||
            !emunand_manageable_title(type_id)) continue;
        std::vector<std::string> names;
        if (!directory_names({false, root.path + "/title/" + type}, names)) continue;
        for (const std::string& name : names) {
            std::uint64_t title = 0;
            if (!emunand_parse_title(type + "-" + name, title) || !emunand_manageable_title(title)) continue;
            EmuNandTitlePaths paths;
            emunand_title_paths(title, paths);
            if (inspect({false, root.path + paths.title}, ignored) != Kind::Directory) continue;
            if (titles.size() >= kMaxTitles || !try_push_back(titles, EmuNandTitle{title, {}, true, false})) {
                error = "there are too many titles, or not enough memory to list them";
                return false;
            }
        }
    }
    // Tickets with no title folder: what a cut-short delete leaves.
    std::vector<std::string> ticket_types;
    if (inspect({false, root.path + "/ticket"}, ignored) == Kind::Directory)
        directory_names({false, root.path + "/ticket"}, ticket_types);
    for (const std::string& type : ticket_types) {
        std::uint64_t type_id = 0;
        if (type.size() != 8 || !emunand_parse_title(type + "00000001", type_id) ||
            !emunand_manageable_title(type_id)) continue;
        std::vector<std::string> names;
        if (!directory_names({false, root.path + "/ticket/" + type}, names)) continue;
        for (const std::string& name : names) {
            std::uint64_t title = 0;
            if (name.size() != 12 || name.compare(8, 4, ".tik") != 0 ||
                !emunand_parse_title(type + "-" + name.substr(0, 8), title) || !emunand_manageable_title(title)) continue;
            const bool listed = std::any_of(titles.begin(), titles.end(), [&](const EmuNandTitle& entry) { return entry.id == title; });
            EmuNandTitlePaths paths;
            emunand_title_paths(title, paths);
            if (listed || inspect({false, root.path + paths.ticket}, ignored) != Kind::File) continue;
            EmuNandTitle entry{title, {}, false, false};
            entry.ticket_only = true;
            if (titles.size() >= kMaxTitles || !try_push_back(titles, entry)) {
                error = "there are too many titles, or not enough memory to list them";
                return false;
            }
        }
    }
    NandSession nand;
    u32 count = 0;
    // The Wii's own titles only add the copy-to and copy-from choices: the
    // emulated NAND is still worth showing without them, but the log says
    // why those choices are missing.
    if (!nand.open || ES_GetNumTitles(&count) < 0 || count > kMaxTitles) {
        logf("Emulated NAND: the Wii's own titles could not be listed, so saves can't be copied to or from it now.\n");
    } else {
        u64* ids = static_cast<u64*>(memalign(32, (count * sizeof(u64) + 31) & ~std::size_t(31)));
        const bool listed = ids && ES_GetTitles(ids, count) >= 0;
        if (!listed)
            logf("Emulated NAND: the Wii's own titles could not be listed, so saves can't be copied to or from it now.\n");
        if (listed) {
            for (u32 index = 0; index < count; ++index) {
                if (!emunand_manageable_title(ids[index])) continue;
                auto found = std::find_if(titles.begin(), titles.end(), [&](const EmuNandTitle& entry) { return entry.id == ids[index]; });
                if (found != titles.end()) found->real = true;
                else if (titles.size() < kMaxTitles && !try_push_back(titles,
                    EmuNandTitle{ids[index], {}, false, true})) {
                    std::free(ids);
                    error = "there is not enough memory to list the Wii's titles";
                    return false;
                }
            }
        }
        std::free(ids);
    }
    for (EmuNandTitle& title : titles) {
        title.name = emunand_title_code(title.id);
        if (title.emulated || title.ticket_only) {
            title.name = title_name({false, root.path}, title.id);
        }
        if (title.real && nand.open && title.name == emunand_title_code(title.id)) {
            title.name = title_name({true, ""}, title.id);
        }
    }
    std::sort(titles.begin(), titles.end(), [](const EmuNandTitle& a, const EmuNandTitle& b) { return a.id < b.id; });
    return true;
}

bool emunand_manager_action(const std::string& folder, EmuNandAction action,
                            std::uint64_t title, std::string& error) {
    error.clear();
    const char* description = "unknown action";
    switch (action) {
        case EmuNandAction::DeleteTitle: description = "delete the emulated title and ticket"; break;
        case EmuNandAction::DeleteSave: description = "delete the emulated save"; break;
        case EmuNandAction::SaveToEmulated: description = "copy the Wii's save to the emulated NAND"; break;
        case EmuNandAction::SaveToWii: description = "copy the emulated save to the Wii"; break;
        case EmuNandAction::MiisToEmulated: description = "copy the Wii's Miis to the emulated NAND"; break;
        case EmuNandAction::MiisToWii: description = "copy the emulated Miis to the Wii"; break;
        case EmuNandAction::RemoveSettings: description = "remove the emulated Wii settings"; break;
    }
    // The Miis and the settings belong to no one title.
    if (title != 0) logf("Emulated NAND: asked to %s in %s for title %s.\n", description, folder.c_str(), emunand_title_text(title).c_str());
    else logf("Emulated NAND: asked to %s in %s.\n", description, folder.c_str());
    CardRoot root;
    NandSession nand;
    bool ok = root.Open(folder, error);
    if (ok && !nand.open && (action == EmuNandAction::SaveToWii || action == EmuNandAction::SaveToEmulated ||
        action == EmuNandAction::MiisToWii || action == EmuNandAction::MiisToEmulated)) {
        error = "the Wii's internal storage could not be opened";
        ok = false;
    }
    if (ok) ok = perform(root.path, action, title, error);
    if (ok) logf("Emulated NAND: finished the request to %s.\n", description);
    else logf("Emulated NAND: could not %s because %s.\n", description, error.c_str());
    return ok;
}

}  // namespace riftwii::wii
