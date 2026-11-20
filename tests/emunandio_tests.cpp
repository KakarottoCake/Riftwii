// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gccore.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "emunandmanage.hpp"
#include "sharednandfind.hpp"
#include "riftwii/emunand.hpp"

namespace fs = std::filesystem;
using namespace riftwii;
using namespace riftwii::wii;

namespace {

constexpr u64 kTitle = 0x0001000157424D45ULL;
const std::string kData = "/title/00010001/57424d45/data";
const std::string kRoot = "sd:/nands/01";
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Line " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

struct Entry {
    bool directory = false;
    std::string bytes;
    u32 owner = 0x1001;
    u16 group = 0x3031;
};
struct Handle {
    std::string path;
    std::size_t offset = 0;
};
std::map<std::string, Entry> nand_files;
std::map<s32, Handle> handles;
bool installed = true;
bool fail_write = false;
bool refuse_data = false;
std::string fail_rename_to;
unsigned writes = 0;
bool escaped = false;

void check_write(const std::string& path) {
    ++writes;
    const bool save = path == kData || path.compare(0, kData.size() + 1, kData + "/") == 0;
    const bool miis = path.compare(0, 22, "/shared2/menu/FaceLib/") == 0;
    if (!save && !miis) escaped = true;
}

void card_file(const std::string& relative, const std::string& bytes) {
    const fs::path path = kRoot + relative;
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << bytes;
}

std::string card_read(const std::string& relative) {
    std::ifstream input(kRoot + relative, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}

void reset() {
    // The CTest working directory is a dedicated fixture in build-host.
    // Check containment before removing the previous run's fake SD card.
    const fs::path working = fs::current_path();
    const fs::path card = fs::absolute("sd:").lexically_normal();
    CHECK(card.parent_path() == working);
    if (card.parent_path() != working) std::abort();
    fs::remove_all(card);
    fs::create_directories(kRoot);
    nand_files.clear();
    handles.clear();
    nand_files[kData] = {true, ""};
    nand_files[kData + "/save.bin"] = {false, "old save"};
    nand_files[kData + "/obsolete"] = {false, "old extra"};
    nand_files[kEmuNandMiis] = {false, "old Miis"};
    installed = true;
    fail_write = refuse_data = escaped = false;
    fail_rename_to.clear();
    writes = 0;
}

void save_transfers() {
    reset();
    card_file(kData + "/save.bin", "incoming");
    card_file(kData + "/rw000000.tmp", "a game's own file, not a temporary copy");
    std::string error;
    CHECK(emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(nand_files[kData + "/save.bin"].bytes == "incoming");
    CHECK(nand_files[kData + "/save.bin"].owner == 0x1001);
    CHECK(nand_files[kData + "/rw000000.tmp"].bytes == "a game's own file, not a temporary copy");
    CHECK(nand_files.count(kData + "/obsolete") == 0);
    CHECK(card_read("/riftwii-backups/00010001-57424d45/wii-save.bak0/save.bin") == "old save");
    CHECK(!escaped && handles.empty());

    reset();
    card_file(kData + "/save.bin", "emulated old");
    CHECK(emunand_manager_action(kRoot, EmuNandAction::SaveToEmulated, kTitle, error));
    CHECK(card_read(kData + "/save.bin") == "old save");
    CHECK(card_read("/riftwii-backups/00010001-57424d45/emulated-save.bak0/save.bin") == "emulated old");
    CHECK(writes == 0);
}

void refused_and_failed_writes() {
    std::string error;
    reset();
    card_file(kData + "/save.bin", "incoming");
    installed = false;
    CHECK(!emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(writes == 0);

    reset();
    card_file(kData + "/save.bin", "incoming");
    card_file("/riftwii-backups", "a file blocks the backup folder");
    CHECK(!emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(writes == 0);
    CHECK(nand_files[kData + "/save.bin"].bytes == "old save");

    reset();
    card_file(kData + "/riftwii.cln", "");
    CHECK(!emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(writes == 0);

    reset();
    card_file(kData + "/save.bin", "incoming");
    refuse_data = true;
    CHECK(!emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(writes == 0);

    reset();
    card_file(kData + "/save.bin", "incoming");
    fail_write = true;
    CHECK(!emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(nand_files[kData + "/save.bin"].bytes == "old save");
    CHECK(nand_files.count(kData + "/obsolete") == 1);
    CHECK(handles.empty() && !escaped);

    reset();
    card_file(kData + "/save.bin", "incoming");
    fail_rename_to = kData + "/save.bin";
    CHECK(!emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(nand_files[kData + "/save.bin"].bytes == "old save");
    CHECK(card_read("/riftwii-backups/00010001-57424d45/wii-save.bak0/save.bin") == "old save");
    CHECK(!escaped);

    reset();
    card_file(kData + "/bad:name", "unsafe");
    CHECK(!emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(writes == 0);
    reset();
    card_file(kData + "/huge.bin", "");
    fs::resize_file(kRoot + kData + "/huge.bin", kEmuNandMaxSaveFile + 1);
    CHECK(!emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(writes == 0);
}

void deletion_and_system_files() {
    reset();
    std::string error;
    card_file("/title/00000001/00000002/data/setting.txt", "console settings");
    CHECK(!emunand_manager_action(kRoot, EmuNandAction::DeleteTitle, 0x100000002ULL, error));
    CHECK(card_read(kEmuNandSettings) == "console settings");
    card_file(kData + "/save.bin", "save");
    card_file("/title/00010001/57424d45/content/title.tmd", "metadata");
    card_file("/title/00010001/57424d45/content/.riftwii-wad", "marker");
    card_file("/ticket/00010001/57424d45.tik", "ticket");
    card_file("/shared1/00000000.app", "shared program");
    CHECK(emunand_manager_action(kRoot, EmuNandAction::DeleteSave, kTitle, error));
    CHECK(fs::exists(kRoot + "/title/00010001/57424d45/content/title.tmd"));
    CHECK(emunand_manager_action(kRoot, EmuNandAction::DeleteTitle, kTitle, error));
    CHECK(!fs::exists(kRoot + "/title/00010001/57424d45"));
    CHECK(!fs::exists(kRoot + "/ticket/00010001/57424d45.tik"));
    CHECK(card_read("/shared1/00000000.app") == "shared program");
    CHECK(writes == 0);

    card_file(kEmuNandSysconf, "sysconf");
    CHECK(emunand_manager_action(kRoot, EmuNandAction::RemoveSettings, 0, error));
    CHECK(card_read(std::string(kEmuNandSettings) + ".bak0") == "console settings");
    CHECK(card_read(std::string(kEmuNandSysconf) + ".bak0") == "sysconf");
    CHECK(!fs::exists(kRoot + kEmuNandSettings));
    CHECK(!fs::exists(kRoot + kEmuNandSysconf));
    CHECK(writes == 0);
}

void mii_transfers() {
    reset();
    card_file(kEmuNandMiis, "new Miis");
    std::string error;
    CHECK(emunand_manager_action(kRoot, EmuNandAction::MiisToWii, 0, error));
    CHECK(nand_files[kEmuNandMiis].bytes == "new Miis");
    CHECK(card_read(std::string(kEmuNandMiis) + ".wii.bak0") == "old Miis");
    CHECK(!escaped);
    CHECK(emunand_manager_action(kRoot, EmuNandAction::MiisToEmulated, 0, error));
    CHECK(card_read(std::string(kEmuNandMiis) + ".bak0") == "new Miis");
    CHECK(emunand_manager_action(kRoot, EmuNandAction::MiisToEmulated, 0, error));
    CHECK(card_read(std::string(kEmuNandMiis) + ".bak1") == "new Miis");
    card_file(kEmuNandMiis, "truncated");
    CHECK(!emunand_manager_action(kRoot, EmuNandAction::MiisToWii, 0, error));
    CHECK(nand_files[kEmuNandMiis].bytes == "new Miis");
}

}  // namespace

s32 ES_GetStoredTMDSize(u64 title, u32* size) {
    *size = 0x1E4;
    return installed && title == kTitle ? 0 : -106;
}
s32 ES_GetStoredTMD(u64 title, signed_blob* data, u32 size) {
    if (!installed || title != kTitle || size != 0x1E4) return -106;
    auto* bytes = reinterpret_cast<u8*>(data);
    std::memset(bytes, 0, size);
    for (unsigned i = 0; i < 8; ++i) bytes[0x18C + i] = static_cast<u8>(title >> (56 - i * 8));
    return 0;
}
s32 ES_GetNumTitles(u32* count) { *count = installed ? 1 : 0; return 0; }
s32 ES_GetTitles(u64* titles, u32 count) { if (count) titles[0] = kTitle; return 0; }

s32 ISFS_ReadDir(const char* path, char* names, u32* count) {
    if (refuse_data && std::string(path).compare(0, kData.size(), kData) == 0) return -102;
    const auto entry = nand_files.find(path);
    if (entry == nand_files.end()) return -106;
    if (!entry->second.directory) return -101;
    const std::string prefix = std::string(path) + "/";
    std::vector<std::string> children;
    for (const auto& item : nand_files) {
        if (item.first.compare(0, prefix.size(), prefix) != 0) continue;
        const std::string name = item.first.substr(prefix.size());
        if (name.find('/') == std::string::npos) children.push_back(name);
    }
    if (names) {
        if (*count < children.size()) return -101;
        for (const std::string& name : children) {
            std::memcpy(names, name.c_str(), name.size() + 1);
            names += name.size() + 1;
        }
    }
    *count = static_cast<u32>(children.size());
    return 0;
}
s32 ISFS_Open(const char* path, u8) {
    if (refuse_data && std::string(path).compare(0, kData.size(), kData) == 0) return -102;
    const auto found = nand_files.find(path);
    if (found == nand_files.end()) return -106;
    if (found->second.directory) return -101;
    const s32 fd = handles.empty() ? 1 : handles.rbegin()->first + 1;
    handles[fd] = {path, 0};
    return fd;
}
s32 ISFS_Close(s32 fd) { return handles.erase(fd) ? 0 : -101; }
s32 ISFS_GetFileStats(s32 fd, fstats* stats) {
    if (!handles.count(fd)) return -101;
    stats->file_length = static_cast<u32>(nand_files[handles[fd].path].bytes.size());
    return 0;
}
s32 ISFS_Read(s32 fd, void* data, u32 size) {
    if (!handles.count(fd)) return -101;
    Handle& handle = handles[fd];
    const std::string& bytes = nand_files[handle.path].bytes;
    const std::size_t take = std::min<std::size_t>(size, bytes.size() - handle.offset);
    std::memcpy(data, bytes.data() + handle.offset, take);
    handle.offset += take;
    return static_cast<s32>(take);
}
s32 ISFS_Write(s32 fd, const void* data, u32 size) {
    if (!handles.count(fd) || fail_write) return -101;
    Handle& handle = handles[fd];
    check_write(handle.path);
    nand_files[handle.path].bytes.append(static_cast<const char*>(data), size);
    return static_cast<s32>(size);
}
s32 ISFS_GetAttr(const char* path, u32* owner, u16* group, u8* flags, u8* owner_perm, u8* group_perm, u8* other_perm) {
    if (refuse_data && std::string(path).compare(0, kData.size(), kData) == 0) return -102;
    const auto entry = nand_files.find(path);
    if (entry == nand_files.end()) return -106;
    *owner = entry->second.owner;
    *group = entry->second.group;
    *flags = 0;
    *owner_perm = *group_perm = 3;
    *other_perm = 0;
    return 0;
}
s32 ISFS_SetAttr(const char* path, u32 owner, u16 group, u8, u8, u8, u8) {
    check_write(path);
    if (!nand_files.count(path)) return -106;
    nand_files[path].owner = owner;
    nand_files[path].group = group;
    return 0;
}
s32 ISFS_CreateFile(const char* path, u8, u8, u8, u8) {
    check_write(path);
    if (nand_files.count(path)) return -105;
    nand_files[path] = {false, ""};
    return 0;
}
s32 ISFS_CreateDir(const char* path, u8 flags, u8 owner, u8 group, u8 other) {
    const s32 result = ISFS_CreateFile(path, flags, owner, group, other);
    if (result >= 0) nand_files[path].directory = true;
    return result;
}
s32 ISFS_Delete(const char* path) {
    check_write(path);
    return nand_files.erase(path) ? 0 : -106;
}
s32 ISFS_Rename(const char* from, const char* to) {
    check_write(from);
    check_write(to);
    if (to == fail_rename_to) {
        fail_rename_to.clear();  // The rollback must still be allowed.
        return -101;
    }
    if (!nand_files.count(from) || nand_files.count(to)) return -101;
    nand_files[to] = nand_files[from];
    nand_files.erase(from);
    return 0;
}

namespace riftwii::wii {
s32 NandOpen() { return 0; }
void NandClose() {}
bool open_nand_permissions(const char*) { return true; }
void close_nand_permissions(const char*) {}
void logf(const char*, ...) {}
SharedNandLookup look_up_shared_nand(const std::string&) { return {}; }
SharedNandLookup look_up_channel_nand() { return {}; }
bool mount_usb_nand_folder(const std::string&, std::string&, std::string&) { return false; }
void unmount_usb_nand_folder() {}
}  // namespace riftwii::wii

int main() {
    save_transfers();
    refused_and_failed_writes();
    deletion_and_system_files();
    mii_transfers();
    CHECK(!escaped && handles.empty());
    return failures ? 1 : 0;
}
