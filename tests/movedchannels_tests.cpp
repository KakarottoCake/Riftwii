// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the production startup cleanup with real card files and mocked NAND.
#include "movedchannels.hpp"

#include <gccore.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
namespace fs = std::filesystem;
using namespace riftwii;
using namespace riftwii::wii;

unsigned failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Line " << __LINE__ << ": " #condition "\n"; ++failures; } } while (0)

constexpr u64 kTitle = 0x0001000157424d45ull;
constexpr const char* kList = "sd:/riftwii/staged_channels.txt";
constexpr const char* kOld = "sd:/riftwii/staged_channels.old";
constexpr const char* kNew = "sd:/riftwii/staged_channels.new";
constexpr const char* kReady = "sd:/riftwii/staged_channels.ready";
constexpr const char* kCardFile = "sd:/private/wii/title/WBME/content.bin";
const std::string first_path = nand_content_path(kTitle, 0x10);
const std::string second_path = nand_content_path(kTitle, 0x11);
std::vector<u8> stored_tmd;
std::vector<std::string> deleted;
s32 nand_result = 0;
s32 delete_result = 0;
bool fail_rename = false;
bool have_key = true;
u8 nand_attribute = 0x52;
unsigned create_calls = 0;

void put_be(std::vector<u8>& bytes, std::size_t offset, u64 value, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        bytes[offset + count - i - 1] = static_cast<u8>(value >> (8 * i));
    }
}

void write_file(const char* path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    CHECK(file.good());
}

std::string read_file(const char* path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void reset_files() {
    // CTest gives this suite a private directory; only its fixtures live here.
    fs::remove_all("sd:");
    fs::create_directories("sd:/riftwii");
    fs::create_directories("sd:/private/wii/title/WBME");
    deleted.clear();
    nand_result = 0;
    delete_result = 0;
    fail_rename = false;
    have_key = true;
    nand_attribute = 0x52;
    create_calls = 0;
}

void make_card_file(bool include_second = true) {
    stored_tmd.assign(0x1e4 + 2 * 36, 0);
    put_be(stored_tmd, 0x18c, kTitle, 8);
    put_be(stored_tmd, 0x1de, 2, 2);
    for (unsigned i = 0; i < 2; ++i) {
        const std::size_t record = 0x1e4 + i * 36;
        put_be(stored_tmd, record, 0x10 + i, 4);
        put_be(stored_tmd, record + 4, i, 2);
        put_be(stored_tmd, record + 6, 1, 2);
        put_be(stored_tmd, record + 8, 16, 8);
    }
    constexpr std::size_t bk = 0x640;
    constexpr std::size_t tmd_offset = bk + 0x80;
    const std::size_t data_offset = (tmd_offset + stored_tmd.size() + 63) & ~std::size_t(63);
    const std::size_t data_size = include_second ? 0x80 : 0x40;
    std::vector<u8> bytes(data_offset + data_size + 0x340, 0);
    put_be(bytes, bk, 0x70426b0001ull, 8);
    put_be(bytes, bk + 8, 1, 4);
    put_be(bytes, bk + 0x14, stored_tmd.size(), 4);
    put_be(bytes, bk + 0x18, data_size, 4);
    put_be(bytes, bk + 0x1c, bytes.size() - bk, 4);
    bytes[bk + 0x20] = include_second ? 3 : 1;
    std::memcpy(bytes.data() + tmd_offset, stored_tmd.data(), stored_tmd.size());
    // Cleanup checks the records, not the encrypted payload. No console key
    // or game data is needed for this fixture.
    write_file(kCardFile, std::string(bytes.begin(), bytes.end()));
}

void test_missing_and_unreadable_card() {
    reset_files();
    write_file(kList, first_path + "\n");
    UnstageMovedChannels();
    CHECK(deleted.empty());
    CHECK(read_file(kList) == first_path + "\n");

    write_file(kCardFile, "damaged");
    UnstageMovedChannels();
    CHECK(deleted.empty());
    CHECK(read_file(kList) == first_path + "\n");

    make_card_file();
    have_key = false;
    UnstageMovedChannels();
    CHECK(deleted.empty());
    CHECK(read_file(kList) == first_path + "\n");

    have_key = true;
    UnstageMovedChannels();
    CHECK(deleted == std::vector<std::string>{first_path});
    CHECK(!fs::exists(kList));
}

void test_verified_content_only() {
    reset_files();
    make_card_file(false);
    write_file(kList, first_path + "\n" + second_path + "\n");
    UnstageMovedChannels();
    CHECK(deleted == std::vector<std::string>{first_path});
    CHECK(!fs::exists(kList));

    write_file(kList, first_path + "\n");
    deleted.clear();
    nand_attribute = 0;
    UnstageMovedChannels();
    CHECK(deleted.empty());
    CHECK(!fs::exists(kList));
}

void test_nand_failures_keep_list() {
    reset_files();
    make_card_file();
    write_file(kList, first_path + "\n");
    nand_result = -1;
    UnstageMovedChannels();
    CHECK(deleted.empty());
    CHECK(read_file(kList) == first_path + "\n");

    nand_result = 0;
    delete_result = -1;
    UnstageMovedChannels();
    CHECK(read_file(kList) == first_path + "\n");
}

void test_rewrite_failure_keeps_both_lists() {
    reset_files();
    write_file(kList, first_path + "\n");
    write_file(kOld, second_path + "\n");
    fs::create_directory(kNew);  // the replacement cannot be opened
    write_file("sd:/riftwii/staged_channels.new/block", "keep the directory nonempty");
    UnstageMovedChannels();
    CHECK(deleted.empty());
    CHECK(read_file(kList) == first_path + "\n");
    CHECK(read_file(kOld) == second_path + "\n");

    fs::remove_all(kNew);
    UnstageMovedChannels();
    CHECK(read_file(kList) == first_path + "\n" + second_path + "\n");
    CHECK(!fs::exists(kOld));
    CHECK(!fs::exists(kReady));
    make_card_file();
    UnstageMovedChannels();
    CHECK(deleted == (std::vector<std::string>{first_path, second_path}));
    CHECK(!fs::exists(kList));
    CHECK(!fs::exists(kOld));
}

void test_merge_and_legacy_overflow() {
    reset_files();
    std::string large;
    while (large.size() <= 64 * 1024) large += first_path + "\r\n";
    large += second_path + "\n";
    write_file(kList, large);
    write_file(kOld, second_path + "\n" + first_path + "\n");
    UnstageMovedChannels();
    CHECK(deleted.empty());
    CHECK(read_file(kList) == first_path + "\n" + second_path + "\n");
    CHECK(!fs::exists(kOld));
    CHECK(!fs::exists(kReady));
}

void test_ready_publication_recovery() {
    // A closed replacement can survive with both originals, or after the
    // main list was removed. It supersedes their already-cleaned entries.
    for (bool have_main : {false, true}) {
        reset_files();
        if (have_main) write_file(kList, first_path + "\n");
        write_file(kOld, first_path + "\n");
        write_file(kReady, second_path + "\n");
        write_file(kNew, "unfinished");
        UnstageMovedChannels();
        CHECK(read_file(kList) == second_path + "\n");
        CHECK(!fs::exists(kOld));
        CHECK(!fs::exists(kReady));
        CHECK(!fs::exists(kNew));
    }
}

void test_unreadable_list_is_preserved() {
    reset_files();
    const std::string too_large(4 * 1024 * 1024 + 1, '\n');
    write_file(kList, too_large);
    write_file(kOld, first_path + "\n");
    UnstageMovedChannels();
    CHECK(deleted.empty());
    CHECK(read_file(kList) == too_large);
    CHECK(read_file(kOld) == first_path + "\n");
}

void test_append_limit_and_unfinished_line() {
    constexpr std::size_t limit = 64 * 1024;
    const std::size_t entry_bytes = first_path.size() + 1;
    for (std::size_t spare : {entry_bytes, entry_bytes - 1, std::size_t(0)}) {
        reset_files();
        make_card_file();
        const std::string original(limit - spare, '\n');
        write_file(kList, original);
        std::string error;
        CHECK(!stage_moved_channel(kTitle, {}, error));
        if (spare == entry_bytes) {
            CHECK(create_calls == 1);
            CHECK(fs::file_size(kList) == limit);
        } else {
            CHECK(create_calls == 0);
            CHECK(error.find("cleanup list is full") != std::string::npos);
            CHECK(read_file(kList) == original);
        }
    }

    reset_files();
    make_card_file();
    write_file(kList, "unfinished");
    std::string error;
    CHECK(!stage_moved_channel(kTitle, {}, error));
    CHECK(create_calls == 0);
    CHECK(error.find("unfinished line") != std::string::npos);
    CHECK(read_file(kList) == "unfinished");

    write_file(kList, "");
    write_file(kReady, first_path + "\n");
    CHECK(!stage_moved_channel(kTitle, {}, error));
    CHECK(create_calls == 0);
    CHECK(error.find("could not be published") != std::string::npos);
}

#ifdef RIFTWII_TEST_WRAP_RENAME
void test_publication_and_rollback_failure() {
    reset_files();
    write_file(kList, first_path + "\n");
    fail_rename = true;
    UnstageMovedChannels();
    CHECK(read_file(kReady) == first_path + "\n");
    CHECK(!fs::exists(kList));

    fail_rename = false;
    UnstageMovedChannels();
    CHECK(read_file(kList) == first_path + "\n");
    CHECK(!fs::exists(kOld));
    CHECK(!fs::exists(kReady));
}
#endif

}  // namespace

// Simulate FAT rename failures without depending on host permissions.
#ifdef RIFTWII_TEST_WRAP_RENAME
extern "C" int __real_rename(const char* from, const char* to);
extern "C" int __wrap_rename(const char* from, const char* to) {
    if (fail_rename && std::strcmp(to, kList) == 0) return -1;
    return __real_rename(from, to);
}
#endif

s32 ES_GetStoredTMDSize(u64, u32* size) {
    *size = static_cast<u32>(stored_tmd.size());
    return 0;
}
s32 ES_GetStoredTMD(u64, signed_blob* data, u32 size) {
    CHECK(size == stored_tmd.size());
    std::memcpy(data, stored_tmd.data(), size);
    return 0;
}
s32 ISFS_Open(const char*, u8) { return -106; }
s32 ISFS_Close(s32) { return 0; }
s32 ISFS_GetFileStats(s32, fstats*) { return -1; }
s32 ISFS_GetAttr(const char*, u32*, u16*, u8* attribute, u8*, u8*, u8*) {
    *attribute = nand_attribute;
    return 0;
}
s32 ISFS_Delete(const char* path) {
    deleted.emplace_back(path);
    return delete_result;
}
s32 ISFS_CreateFile(const char*, u8, u8, u8, u8) {
    ++create_calls;
    return -1;
}
s32 ISFS_Write(s32, const void*, u32) { return -1; }

namespace riftwii::wii {
s32 NandOpen() { return nand_result; }
void NandClose() {}
bool open_nand_permissions(const char*) { return true; }
void close_nand_permissions(const char*) {}
void logf(const char*, ...) {}
bool ConsoleBackupKey(BackupKey& out) {
    out.console_id = 1;
    out.prng[0] = 1;
    return have_key;
}
}  // namespace riftwii::wii

int main() {
    test_missing_and_unreadable_card();
    test_verified_content_only();
    test_nand_failures_keep_list();
    test_rewrite_failure_keeps_both_lists();
    test_merge_and_legacy_overflow();
    test_ready_publication_recovery();
    test_unreadable_list_is_preserved();
    test_append_limit_and_unfinished_line();
#ifdef RIFTWII_TEST_WRAP_RENAME
    test_publication_and_rollback_failure();
#endif
    fs::remove_all("sd:");
    return failures ? 1 : 0;
}
