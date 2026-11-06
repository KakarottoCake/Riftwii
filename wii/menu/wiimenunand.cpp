// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiimenucontent.hpp"

#include <gccore.h>

#include <cstdio>

#include "nandpermissions.hpp"
#include "nanduse.hpp"
#include "log.hpp"
#include "riftwii/sysfont.hpp"

namespace riftwii::wii {
namespace {

constexpr std::size_t kMaxContentMap = 64 * 1024;  // 28 bytes a shared content

// The System Menu's contents are IOS's own files: an app is refused them
// (-102) until IOS's NAND permission check is opened (nandpermissions.hpp),
// which needs hardware access. The reader opens it before any protected
// request and keeps it open for the entire content search: retrying after
// a refusal may leave the Starlet executing its cached, unpatched code.
class NandFile {
public:
    NandFile(const char* path, const char* who, bool permissions_open) {
        fd_ = ISFS_Open(path, ISFS_OPEN_READ);
        if (fd_ == -102 && permissions_open) {
            logf("%s: %s was still refused (-102) with the NAND permission check open.\n", who, path);
        }
    }
    ~NandFile() {
        if (fd_ >= 0) ISFS_Close(fd_);
    }
    NandFile(const NandFile&) = delete;
    NandFile& operator=(const NandFile&) = delete;

    s32 fd() const { return fd_; }

    // The file's length, or 0 when IOS cannot say.
    std::size_t Length() const {
        static fstats stats ATTRIBUTE_ALIGN(32);
        return ISFS_GetFileStats(fd_, &stats) < 0 ? 0 : stats.file_length;
    }

private:
    s32 fd_ = -1;
};

// Where the NAND keeps `content`: a shared one under the name content.map
// gives its hash, any other under the System Menu's own title.
bool ContentPath(const TmdContent& content, char* path, std::size_t room, const char* who,
                 bool permissions_open, std::string& why) {
    constexpr std::uint16_t kShared = 0x8000;
    if ((content.type & kShared) == 0) {
        std::snprintf(path, room, "/title/00000001/00000002/content/%08x.app", static_cast<unsigned>(content.id));
        return true;
    }
    NandFile map("/shared1/content.map", who, permissions_open);
    if (map.fd() < 0) {
        why = "/shared1/content.map could not be opened (" + std::to_string(map.fd()) + ")";
        return false;
    }
    const std::size_t size = map.Length();
    if (size == 0 || size > kMaxContentMap) {
        why = "/shared1/content.map has no size we can read";
        return false;
    }
    MenuContentBuffer bytes = AllocateMenuContentBuffer(size, why);
    if (!bytes) return false;
    const s32 got = ISFS_Read(map.fd(), bytes.get(), static_cast<u32>(size));
    if (got != static_cast<s32>(size)) {
        why = "/shared1/content.map read " + std::to_string(got) + " of " + std::to_string(size) + " bytes";
        return false;
    }
    const std::string name = shared_content_name(bytes.get(), size, content.sha1.data());
    if (name.empty()) {
        why = "content.map does not list the content";
        return false;
    }
    std::snprintf(path, room, "/shared1/%s.app", name.c_str());
    return true;
}

}  // namespace

bool ReadMenuContentFromNand(const TmdContent& content, void* buffer, const char* who, std::string& why,
                             NandPermissionSession* permissions) {
    if (NandOpen() < 0) {
        why = "the NAND could not be opened";
        return false;
    }
    struct NandUse {
        ~NandUse() { NandClose(); }
    } nand_use;
    NandPermissionSession local_permissions(who, false);
    if (!permissions) permissions = &local_permissions;
    const bool permissions_open = permissions->Open();
    char path[64];
    if (!ContentPath(content, path, sizeof path, who, permissions_open, why)) return false;
    NandFile file(path, who, permissions_open);
    if (file.fd() < 0) {
        why = std::string(path) + " could not be opened (" + std::to_string(file.fd()) + ")";
        return false;
    }
    if (file.Length() != content.size) {
        why = std::string(path) + " is not the size its title metadata gives";
        return false;
    }
    const s32 got = ISFS_Read(file.fd(), buffer, static_cast<u32>(content.size));
    if (got != static_cast<s32>(content.size)) {
        why = std::string(path) + " read " + std::to_string(got) + " of " + std::to_string(content.size) + " bytes";
        return false;
    }
    return true;
}

}  // namespace riftwii::wii
