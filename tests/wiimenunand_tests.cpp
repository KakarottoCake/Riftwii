// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// wii/menu/wiimenunand.cpp against a NAND of a few files: where each kind
// of System Menu content is found, and what makes a read fail.
#include "wiimenucontent.hpp"

#include <gccore.h>
#include <malloc.h>

#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "nandpermissions.hpp"
#include "nanduse.hpp"
#include "log.hpp"

using namespace riftwii;
using namespace riftwii::wii;

namespace {
int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << '\n'; ++failures; } } while (0)

std::map<std::string, std::vector<u8>> files;
std::vector<std::string> opened_files;  // by fd
std::string protected_path;  // refused (-102) until the permission check is open
bool permissions_open = false;
int permission_opens = 0;
int permission_closes = 0;
int nand_users = 0;
int open_fds = 0;
int unpatched_opens = 0;
bool permissions_available = true;
bool refuse_when_open = false;
std::string logs;
s32 short_read_by = 0;
std::vector<std::size_t> allocated_sizes;
bool allocation_fails = false;

void Reset() {
    files.clear();
    opened_files.clear();
    protected_path.clear();
    permissions_open = false;
    permission_opens = permission_closes = nand_users = open_fds = 0;
    unpatched_opens = 0;
    permissions_available = true;
    refuse_when_open = false;
    logs.clear();
    short_read_by = 0;
    allocated_sizes.clear();
    allocation_fails = false;
}

std::vector<u8> Bytes(std::size_t size, u8 value) { return std::vector<u8>(size, value); }

TmdContent Content(std::uint32_t id, std::uint16_t type, std::uint64_t size, u8 hash_byte) {
    TmdContent content;
    content.id = id;
    content.type = type;
    content.size = size;
    content.sha1.fill(hash_byte);
    return content;
}

// content.map: 8 name characters and a SHA-1 per shared content.
void AddToContentMap(const char* name, u8 hash_byte) {
    std::vector<u8>& map = files["/shared1/content.map"];
    map.insert(map.end(), name, name + 8);
    map.insert(map.end(), 20, hash_byte);
}

bool ReadContentFromNand(const TmdContent& content, MenuContentBuffer& buffer, const char* who,
                         std::string& why, NandPermissionSession* permissions = nullptr) {
    std::size_t size = 0;
    const auto allocate = [&buffer](std::size_t length, std::string& error) -> u8* {
        allocated_sizes.push_back(length);
        if (allocation_fails) {
            error = "no memory for the test content";
            return nullptr;
        }
        buffer = AllocateMenuContentBuffer(length, error);
        return buffer.get();
    };
    const bool ok = ReadMenuContentFromNand(content, allocate, size, who, why, permissions);
    if (ok) EXPECT(size == allocated_sizes.back());
    return ok;
}

void TestPrivateContent() {
    Reset();
    files["/title/00000001/00000002/content/0000002a.app"] = Bytes(64, 0x11);
    MenuContentBuffer buffer(nullptr, &std::free);
    std::string why;
    EXPECT(ReadContentFromNand(Content(0x2a, 1, 64, 0), buffer, "Menu font", why));
    EXPECT(buffer.get()[63] == 0x11);
    EXPECT(nand_users == 0);
    EXPECT(open_fds == 0);
}

void TestSharedContent() {
    Reset();
    AddToContentMap("0000000b", 0x77);
    AddToContentMap("0000000c", 0x99);
    files["/shared1/0000000c.app"] = Bytes(48, 0x22);
    MenuContentBuffer buffer(nullptr, &std::free);
    std::string why;
    EXPECT(ReadContentFromNand(Content(5, 0x8001, 48, 0x99), buffer, "Menu font", why));
    EXPECT(buffer.get()[0] == 0x22);

    // A shared content content.map does not list is not read.
    EXPECT(!ReadContentFromNand(Content(5, 0x8001, 48, 0x55), buffer, "Menu font", why));
    EXPECT(why == "content.map does not list the content");
    EXPECT(nand_users == 0);
    EXPECT(open_fds == 0);
}

void TestRealContentSize() {
    for (std::size_t length : {std::size_t(32), std::size_t(96), kMaxMenuContentSize}) {
        Reset();
        files["/title/00000001/00000002/content/00000022.app"] = Bytes(length, 0x66);
        MenuContentBuffer buffer(nullptr, &std::free);
        std::string why;
        EXPECT(ReadContentFromNand(Content(0x22, 1, 64, 0), buffer, "Wii Menu sounds", why));
        EXPECT(allocated_sizes == std::vector<std::size_t>{length});
        EXPECT(buffer.get()[length - 1] == 0x66);
        EXPECT(open_fds == 0 && nand_users == 0);
    }
}

void TestSizeAndAllocationFailures() {
    for (std::size_t length : {std::size_t(0), kMaxMenuContentSize + 1}) {
        Reset();
        files["/title/00000001/00000002/content/00000022.app"] = Bytes(length, 0x66);
        MenuContentBuffer buffer(nullptr, &std::free);
        std::string why;
        EXPECT(!ReadContentFromNand(Content(0x22, 1, 64, 0), buffer, "Wii Menu sounds", why));
        EXPECT(why == "/title/00000001/00000002/content/00000022.app is empty or exceeds the memory budget");
        EXPECT(allocated_sizes.empty());
        EXPECT(buffer == nullptr);
        EXPECT(open_fds == 0 && nand_users == 0);
    }

    Reset();
    files["/title/00000001/00000002/content/00000022.app"] = Bytes(96, 0x66);
    allocation_fails = true;
    MenuContentBuffer buffer(nullptr, &std::free);
    std::string why;
    EXPECT(!ReadContentFromNand(Content(0x22, 1, 64, 0), buffer, "Wii Menu sounds", why));
    EXPECT(why == "no memory for the test content");
    EXPECT(allocated_sizes == std::vector<std::size_t>{96});
    EXPECT(open_fds == 0 && nand_users == 0);
}

void TestPermissionCheck() {
    // IOS refuses the file until its permission check is open, and the
    // check is closed again after the read.
    Reset();
    files["/title/00000001/00000002/content/00000001.app"] = Bytes(32, 0x33);
    protected_path = "/title/00000001/00000002/content/00000001.app";
    MenuContentBuffer buffer(nullptr, &std::free);
    std::string why;
    EXPECT(ReadContentFromNand(Content(1, 1, 32, 0), buffer, "Wii Menu sounds", why));
    EXPECT(buffer.get()[31] == 0x33);
    EXPECT(permission_opens == 1);
    EXPECT(permission_closes == 1);
    EXPECT(!permissions_open);
    EXPECT(unpatched_opens == 0);

    // One reader session covers the shared map and contents across many
    // reads, and a refusal while patched says exactly that in the log.
    Reset();
    AddToContentMap("0000000c", 0x99);
    protected_path = "/shared1/content.map";
    files["/shared1/0000000c.app"] = Bytes(32, 0x44);
    {
        NandPermissionSession batch("Wii Menu sounds", false);
        EXPECT(ReadContentFromNand(Content(5, 0x8001, 32, 0x99), buffer, "Wii Menu sounds", why, &batch));
        EXPECT(ReadContentFromNand(Content(5, 0x8001, 32, 0x99), buffer, "Wii Menu sounds", why, &batch));
        EXPECT(permission_opens == 1 && permission_closes == 0 && unpatched_opens == 0);
        refuse_when_open = true;
        EXPECT(!ReadContentFromNand(Content(5, 0x8001, 32, 0x99), buffer, "Wii Menu sounds", why, &batch));
        EXPECT(logs.find("/shared1/content.map was still refused (-102) with the NAND permission check open") !=
               std::string::npos);
    }
    EXPECT(permission_closes == 1 && !permissions_open && open_fds == 0 && nand_users == 0);

    Reset();
    permissions_available = false;
    protected_path = "/shared1/content.map";
    {
        NandPermissionSession batch("unavailable", false);
        EXPECT(!ReadContentFromNand(Content(5, 0x8001, 32, 0x99), buffer, "unavailable", why, &batch));
        EXPECT(!ReadContentFromNand(Content(5, 0x8001, 32, 0x99), buffer, "unavailable", why, &batch));
    }
    EXPECT(permission_opens == 1 && permission_closes == 0 && nand_users == 0 && open_fds == 0);
    EXPECT(logs.empty());  // never claim the check opened when it did not
}

void TestFailures() {
    std::string why;
    MenuContentBuffer buffer(nullptr, &std::free);

    Reset();
    EXPECT(!ReadContentFromNand(Content(3, 1, 64, 0), buffer, "Menu font", why));
    EXPECT(why == "/title/00000001/00000002/content/00000003.app could not be opened (-106)");

    // A read that comes back short.
    Reset();
    files["/title/00000001/00000002/content/00000003.app"] = Bytes(64, 0x44);
    short_read_by = 4;
    EXPECT(!ReadContentFromNand(Content(3, 1, 64, 0), buffer, "Menu font", why));
    EXPECT(why == "/title/00000001/00000002/content/00000003.app read 60 of 64 bytes");

    // No content.map at all.
    Reset();
    EXPECT(!ReadContentFromNand(Content(5, 0x8001, 48, 0x99), buffer, "Menu font", why));
    EXPECT(why == "/shared1/content.map could not be opened (-106)");
    EXPECT(nand_users == 0);
    EXPECT(open_fds == 0);
}
}  // namespace

// What wiimenucontent.cpp gives the rest of the menu.
MenuContentBuffer riftwii::wii::AllocateMenuContentBuffer(std::size_t size, std::string&) {
    return MenuContentBuffer(static_cast<u8*>(memalign(32, size)), &std::free);
}

s32 riftwii::wii::NandOpen() {
    ++nand_users;
    return 0;
}
void riftwii::wii::NandClose() { --nand_users; }

bool riftwii::wii::open_nand_permissions(const char*) {
    ++permission_opens;
    permissions_open = permissions_available;
    return permissions_available;
}
void riftwii::wii::close_nand_permissions(const char*) {
    ++permission_closes;
    permissions_open = false;
}

s32 ISFS_Open(const char* path, u8 mode) {
    EXPECT(mode == ISFS_OPEN_READ);
    if (!permissions_open) ++unpatched_opens;
    if (path == protected_path && (!permissions_open || refuse_when_open)) return -102;
    if (!files.count(path)) return -106;
    opened_files.push_back(path);
    ++open_fds;
    return static_cast<s32>(opened_files.size() - 1);
}
s32 ISFS_Close(s32) {
    --open_fds;
    return 0;
}
s32 ISFS_GetFileStats(s32 fd, fstats* stats) {
    stats->file_length = static_cast<u32>(files[opened_files[static_cast<std::size_t>(fd)]].size());
    return 0;
}
s32 ISFS_Read(s32 fd, void* data, u32 size) {
    const std::vector<u8>& file = files[opened_files[static_cast<std::size_t>(fd)]];
    const u32 got = std::min<u32>(size, static_cast<u32>(file.size())) - static_cast<u32>(short_read_by);
    std::memcpy(data, file.data(), got);
    return static_cast<s32>(got);
}

void riftwii::wii::logf(const char* format, ...) {
    char line[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    logs += line;
}

int main() {
    TestPrivateContent();
    TestSharedContent();
    TestRealContentSize();
    TestSizeAndAllocationFailures();
    TestPermissionCheck();
    TestFailures();
    return failures == 0 ? 0 : 1;
}
