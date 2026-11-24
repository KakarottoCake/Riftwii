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
#include "riftwii/titles.hpp"
#include "sharednandfind.hpp"
#include "riftwii/emunand.hpp"
#include "riftwii/sha1.hpp"

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
std::vector<u64> real_titles;
std::map<std::string, std::string> title_names;
std::vector<std::string> opened_paths;
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
    real_titles = {kTitle};
    title_names.clear();
    opened_paths.clear();
    nand_files[kData] = {true, ""};
    nand_files[kData + "/save.bin"] = {false, "old save"};
    nand_files[kData + "/obsolete"] = {false, "old extra"};
    nand_files[kEmuNandMiis] = {false, "old Miis"};
    // The title's owner, 0x1001, as the Wii's uid.sys gives it.
    nand_files["/sys/uid.sys"] = {false, std::string("\x00\x01\x00\x01\x57\x42\x4D\x45\x00\x00\x10\x01", 12)};
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
    // An owner the mock never gives a new file: the published copy must
    // keep the old file's.
    nand_files[kData + "/save.bin"].owner = 0x2002;
    CHECK(emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(nand_files[kData + "/save.bin"].bytes == "incoming");
    CHECK(nand_files[kData + "/save.bin"].owner == 0x2002);
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

    // A data folder left with another owner (a failed import whose cleanup
    // failed too) is put back to the title's owner from uid.sys.
    reset();
    card_file(kData + "/save.bin", "incoming");
    nand_files[kData].owner = 0x2222;
    CHECK(emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(nand_files[kData].owner == 0x1001);

    // The same for a folder inside the save.
    reset();
    card_file(kData + "/sub/inner.bin", "incoming");
    nand_files[kData + "/sub"] = {true, ""};
    nand_files[kData + "/sub"].owner = 0x2222;
    CHECK(emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(nand_files[kData + "/sub"].owner == 0x1001);
    CHECK(nand_files[kData + "/sub/inner.bin"].bytes == "incoming");

    // A Wii save with two names the card can't tell apart is refused before
    // any backup is written.
    reset();
    card_file(kData + "/save.bin", "incoming");
    nand_files[kData + "/SAVE.BIN"] = {false, "other case"};
    CHECK(!emunand_manager_action(kRoot, EmuNandAction::SaveToWii, kTitle, error));
    CHECK(!fs::exists(kRoot + "/riftwii-backups"));
    CHECK(nand_files[kData + "/save.bin"].bytes == "old save");

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
    {
        // A ticket left with no title folder is listed and can be deleted.
        card_file("/ticket/00010001/57424d45.tik", "ticket");
        std::vector<EmuNandTitle> titles;
        bool sysconf = false;
        bool settings = false;
        CHECK(emunand_manager_list(kRoot, titles, sysconf, settings, error));
        const auto found = std::find_if(titles.begin(), titles.end(), [](const EmuNandTitle& t) { return t.id == kTitle; });
        CHECK(found != titles.end() && found->ticket_only && !found->emulated);
        CHECK(emunand_manager_action(kRoot, EmuNandAction::DeleteTitle, kTitle, error));
        CHECK(!fs::exists(kRoot + "/ticket/00010001/57424d45.tik"));
        reset();
    }
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

void protected_deletes() {
    const std::uint64_t protected_titles[] = {
        0x0001000248414341ULL, 0x0001000848414C45ULL,
        0x0001000148414241ULL, 0x0001000148435445ULL,
        0x0001000148435641ULL, 0x000100014843584AULL,
        0x000100014843594AULL,
    };
    for (const std::uint64_t title : protected_titles) {
        reset();
        EmuNandTitlePaths paths;
        CHECK(emunand_title_paths(title, paths));
        card_file(paths.data + "/save.bin", "keep save");
        card_file(paths.title + "/content/title.tmd", "keep program");
        card_file(paths.ticket, "keep ticket");
        std::string error;
        for (const EmuNandAction action : {EmuNandAction::DeleteSave, EmuNandAction::DeleteTitle}) {
            CHECK(!emunand_manager_action(kRoot, action, title, error));
            CHECK(card_read(paths.data + "/save.bin") == "keep save");
            CHECK(card_read(paths.title + "/content/title.tmd") == "keep program");
            CHECK(card_read(paths.ticket) == "keep ticket");
        }
        // The ticket-only cleanup path must enforce the same protection.
        fs::remove_all(kRoot + paths.title);
        CHECK(!emunand_manager_action(kRoot, EmuNandAction::DeleteTitle, title, error));
        CHECK(card_read(paths.ticket) == "keep ticket");
        CHECK(writes == 0 && handles.empty());
    }
}

// Small synthetic metadata and name headers, without console assets.
std::string channel_metadata(std::uint64_t title) {
    std::string tmd(0x1E4 + 36, '\0');
    for (int i = 0; i < 8; ++i) tmd[0x18C + i] = static_cast<char>(title >> (56 - 8 * i));
    tmd[0x1DF] = 1;
    tmd[0x1E7] = 1;  // Content 1, index 0.
    return tmd;
}

std::string channel_name_header(const std::string& name) {
    std::string banner(0x700, '\0');
    banner.replace(0x40, 4, "IMET");
    const std::size_t english_name = 0x40 + 0x1C + 84;
    for (std::size_t i = 0; i < name.size(); ++i) banner[english_name + 2 * i + 1] = name[i];
    return banner;
}

void listed_title_names() {
    reset();
    constexpr u64 real_channel = 0x0001000154455354ULL;  // TEST.
    constexpr u64 disc_save = 0x0001000052365245ULL;     // R6RE.
    real_titles = {kTitle, real_channel, disc_save};
    title_names["WBME"] = "Listed game";
    // A disc game is listed under its 6-character ID; its save's 4-letter code finds it.
    title_names["R6RE01"] = "Listed disc game";
    card_file(kData + "/save.bin", "save");
    card_file("/title/00010000/52365245/data/save.bin", "disc save");
    nand_files["/title/00010001/54455354/content/title.tmd"] = {false, channel_metadata(real_channel)};
    nand_files["/title/00010001/54455354/content/00000001.app"] = {false, channel_name_header("Wii banner name")};
    // A listed title must not open its banner when the title table is cheaper.
    nand_files["/title/00010001/57424d45/content/title.tmd"] = {false, channel_metadata(kTitle)};

    std::vector<EmuNandTitle> titles;
    bool sysconf = false;
    bool settings = false;
    std::string error;
    CHECK(emunand_manager_list(kRoot, titles, sysconf, settings, error));
    CHECK(titles.size() == 3);
    for (const EmuNandTitle& title : titles) {
        if (title.id == kTitle) {
            CHECK(title.name == "Listed game (WBME)");
            CHECK(title.emulated && title.real);
        } else if (title.id == real_channel) {
            CHECK(title.name == "Wii banner name");
            CHECK(!title.emulated && title.real);
        } else if (title.id == disc_save) {
            CHECK(title.name == "Listed disc game (R6RE)");
            CHECK(title.emulated && title.real);
        }
    }
    CHECK(std::find(opened_paths.begin(), opened_paths.end(),
        "/title/00010001/57424d45/content/title.tmd") == opened_paths.end());
    CHECK(std::find(opened_paths.begin(), opened_paths.end(),
        "/title/00010000/52365245/content/title.tmd") == opened_paths.end());
    CHECK(handles.empty() && writes == 0);

    // The same bounded header read also names channels only on the card.
    real_titles.clear();
    card_file("/title/00010001/54455354/content/title.tmd", channel_metadata(real_channel));
    card_file("/title/00010001/54455354/content/00000001.app", channel_name_header("Card banner name"));
    CHECK(emunand_manager_list(kRoot, titles, sysconf, settings, error));
    const auto found = std::find_if(titles.begin(), titles.end(), [&](const EmuNandTitle& title) {
        return title.id == real_channel;
    });
    CHECK(found != titles.end() && found->name == "Card banner name" && found->emulated && !found->real);
    CHECK(handles.empty() && writes == 0);
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
    // The title's group, as the mock's folders have it.
    bytes[0x198] = 0x30;
    bytes[0x199] = 0x31;
    return 0;
}
s32 ES_GetNumTitles(u32* count) {
    *count = installed ? static_cast<u32>(real_titles.size()) : 0;
    return 0;
}
s32 ES_GetTitles(u64* titles, u32 count) {
    for (u32 i = 0; i < count; ++i) titles[i] = real_titles[i];
    return 0;
}

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
    opened_paths.push_back(path);
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
// The list is always loaded here, so the manager's read-on-demand call
// has nothing to do.
bool GameTitleListed(const std::string&, std::string& title) {
    title.clear();
    return true;
}
// The game-name list as the menu has loaded it, made from title_names.
const riftwii::TitleTable* LoadedTitles() {
    static riftwii::TitleTable table;
    table = riftwii::TitleTable{};
    std::string text;
    for (const auto& entry : title_names) text += entry.first + " = " + entry.second + "\n";
    table.add_text(text);
    return &table;
}
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

void gx_folders() {
    // USB Loader GX's per-game settings: a last record exactly the reader's
    // buffer long (20 KiB less its terminator), with no newline, is still
    // whole, so its folder is offered.
    reset();
    fs::create_directories("sd:/custom");
    const std::string tail = "NandEmuPath:sd:/custom;";
    std::string record = "game:RMCE01 = Pad:";
    record += std::string(20 * 1024 - 1 - record.size() - tail.size() - 1, 'a');
    record += ";" + tail;
    CHECK(record.size() == 20 * 1024 - 1);
    fs::create_directories("sd:/apps/usbloader_gx");
    {
        std::ofstream file("sd:/apps/usbloader_gx/GXGameSettings.cfg", std::ios::binary);
        file << "game:RSBE01 = Pad:a;\n" << record;
    }
    const std::vector<std::string> folders = emunand_manager_folders();
    CHECK(std::count(folders.begin(), folders.end(), std::string("sd:/custom")) == 1);
    fs::remove_all("sd:/apps");
    fs::remove_all("sd:/custom");
}

// A WiiWare title with one shared content, in kRoot, for the copy tests.
const std::string kShared = std::string(20 * 1024, 'S');

void shared_title_in_source() {
    const Sha1Digest digest = sha1(reinterpret_cast<const std::uint8_t*>(kShared.data()), kShared.size());
    std::string tmd(0x1E4 + 36, '\0');
    for (int i = 0; i < 8; ++i) tmd[0x18C + i] = static_cast<char>(kTitle >> (56 - 8 * i));
    tmd[0x1DF] = 1;                                     // one content
    tmd[0x1E4 + 6] = '\x80';                            // type 0x8001: shared
    tmd[0x1E4 + 7] = '\x01';
    tmd[0x1E4 + 8 + 6] = static_cast<char>(kShared.size() >> 8);
    std::memcpy(&tmd[0x1E4 + 16], digest.data(), digest.size());
    card_file("/title/00010001/57424d45/content/title.tmd", tmd);
    card_file("/ticket/00010001/57424d45.tik", std::string(0x2A4, 'T'));
    card_file("/shared1/00000005.app", kShared);
    card_file("/shared1/content.map", "00000005" + std::string(reinterpret_cast<const char*>(digest.data()), 20));
}

void dest_file(const std::string& relative, const std::string& bytes) {
    const fs::path path = "sd:/nands/02" + relative;
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << bytes;
}

std::string dest_read(const std::string& relative) {
    std::ifstream input("sd:/nands/02" + relative, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}

void shared_content_copies() {
    const Sha1Digest digest = sha1(reinterpret_cast<const std::uint8_t*>(kShared.data()), kShared.size());
    const std::string listed = "00000000" + std::string(reinterpret_cast<const char*>(digest.data()), 20);
    std::string error;
    bool already = false;

    // The destination lists the content, but its file has the right size
    // and the wrong bytes (a damaged card): it is copied again, under the
    // same name, and the list is left alone.
    reset();
    shared_title_in_source();
    dest_file("/shared1/content.map", listed);
    dest_file("/shared1/00000000.app", std::string(kShared.size(), 'X'));
    CHECK(emunand_manager_copy_title(kRoot, "sd:/nands/02", kTitle, already, error));
    CHECK(!already);
    CHECK(dest_read("/shared1/00000000.app") == kShared);
    CHECK(dest_read("/shared1/content.map") == listed);
    CHECK(fs::exists("sd:/nands/02/ticket/00010001/57424d45.tik"));

    // The listed file is there whole: used as it is, nothing else added.
    fs::remove_all("sd:/nands/02/title");
    CHECK(emunand_manager_copy_title(kRoot, "sd:/nands/02", kTitle, already, error));
    CHECK(dest_read("/shared1/content.map") == listed);
    CHECK(!fs::exists("sd:/nands/02/shared1/00000001.app"));

    // A second copy is refused and says so.
    CHECK(!emunand_manager_copy_title(kRoot, "sd:/nands/02", kTitle, already, error));
    CHECK(already);

    // The listed name can't be read as a file (a folder in its place) nor
    // written over: the copy fails, and the title folder and the ticket it
    // had made are taken out again.
    reset();
    shared_title_in_source();
    dest_file("/shared1/content.map", listed);
    fs::create_directories("sd:/nands/02/shared1/00000000.app/inside");
    CHECK(!emunand_manager_copy_title(kRoot, "sd:/nands/02", kTitle, already, error));
    CHECK(!already);
    CHECK(!fs::exists("sd:/nands/02/title/00010001/57424d45"));
    CHECK(!fs::exists("sd:/nands/02/ticket/00010001/57424d45.tik"));
    CHECK(dest_read("/shared1/content.map") == listed);
}

int main() {
    save_transfers();
    refused_and_failed_writes();
    deletion_and_system_files();
    protected_deletes();
    listed_title_names();
    mii_transfers();
    gx_folders();
    shared_content_copies();
    CHECK(!escaped && handles.empty());
    return failures ? 1 : 0;
}
